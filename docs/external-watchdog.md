# External Watchdog IC

The ESP32-S3 has a built-in watchdog timer, but it runs on the same silicon as the application firmware. If the chip enters a latch-up state, browns out, or the crystal oscillator fails, the internal watchdog may not fire.

An **external watchdog IC** is an independent hardware timer on a separate chip. The firmware must toggle a GPIO periodically to "pet" it. If the toggling stops, the watchdog asserts a reset signal to the MCU.

---

## Why an external watchdog?

| Failure mode | Internal watchdog catches? | External watchdog catches? |
|-------------|:--------------------------:|:---------------------------:|
| Task deadlock (priority inversion) | ✓ (if idle task starved) | ✓ (safety task stops toggling) |
| Infinite loop in ISR | ✗ (ISR preempts watchdog) | ✓ (safety task never runs) |
| Crystal oscillator failure | ✗ (no clock = no watchdog) | ✓ (independent RC oscillator) |
| Flash corruption (bad code) | ✗ (watchdog is code too) | ✓ (independent silicon) |
| Voltage brownout | ✗ (watchdog browns out too) | ✓ (separate power-on-reset) |
| Latch-up (ESD, overvoltage) | ✗ (silicon locked) | ✓ (external IC unaffected) |

The external watchdog is the **last-resort** safety layer. If everything else fails — ESTOP layers, heartbeat monitoring, command staleness — the watchdog ensures the MCU reboots into a safe state within a bounded time.

---

## Implementation on the E-Trike

### Hardware

The SYS ESP32-S3 uses an external watchdog IC connected to GPIO21.

```
ESP32-S3 GPIO21 ──► WDI (Watchdog Input) pin on external watchdog IC
                         │
Watchdog IC RST ────────► EN (Enable) pin on ESP32-S3
```

The `safety_task` (Core 0, priority 5, 20 Hz) toggles GPIO21 every iteration:

```cpp
void safety_task() {
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // Toggle watchdog GPIO every iteration
        gpio_set_level(WATCHDOG_GPIO, 1);
        vTaskDelay(1);  // brief pulse
        gpio_set_level(WATCHDOG_GPIO, 0);

        // ... ESTOP GPIO check, heartbeat check ...

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50));  // 20 Hz = 50 ms
    }
}
```

### Timeout selection

The watchdog timeout is configured in hardware (typically via a resistor or pin strapping on the watchdog IC). The timeout must be:

- **Long enough** that a transient spike in FreeRTOS tick latency (e.g., flash erase, WiFi calibration burst) doesn't trigger it.
- **Short enough** that a genuine firmware hang cuts power to the motor before the vehicle can accelerate dangerously.

For the E-Trike: **timeout = 100 ms**. This means:
- The `safety_task` toggles every 50 ms (20 Hz).
- Two missed toggles (100 ms) triggers reset.
- At 25 km/h (7 m/s), the vehicle moves ~0.7 m during the timeout window.

### Startup sequencing

On reset, the watchdog IC holds the ESP32 in reset for a startup delay (~200 ms) to allow the power supply to stabilize. After release, the ESP32 boots and the `safety_task` begins toggling within ~50 ms (FreeRTOS scheduler start + task creation).

The watchdog **does not arm until the first toggle** — most watchdog ICs start with their timer disarmed and arm on the first WDI edge. This prevents a reset loop during boot.

---

## Safe state on watchdog reset

When the watchdog fires:

1. ESP32-S3 EN pin is pulled LOW → MCU resets.
2. All GPIOs go to their reset state (high-impedance input).
3. Motor controller sees 0 V on throttle (MCP4725 loses I2C, output goes to 0 V).
4. All gear relays de-energize (GPIOs float → transistor off → relay open).
5. Brake: SEB continues its internal control loop with the last received `0x7B9` command. After 20 ms of no new frame, SEB enters comm-fault. **This is a known gap — behavior is empirically unverified.** If SEB holds pressure on comm-fault, the window is ~20ms (acceptable). If SEB releases, the vehicle coasts without brake for ~2.5s (SYS reboot + brake LBS). A hardware brake-hold relay gated by the TPS3850 RST line is recommended to make brake behavior deterministic during MCU reset. See [[emergency-safety-analysis]] §3 for full causal trace and risk quantification.
6. After ~200 ms, the ESP32 reboots and restores control.

### Post-reset behavior

After a watchdog reset, the firmware must:
1. Not assume any previous state is valid.
2. Start in MANUAL mode (safest default).
3. Re-run the full LBS sequence for all actuators (see [[listen-before-speaking]]).
4. Log the reset cause (ESP32 provides `esp_reset_reason()`).

```cpp
void app_main() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_PANIC || reason == ESP_RST_WDT) {
        // External watchdog likely triggered
        // Log to persistent storage if available
    }

    // ... normal startup (MANUAL mode, LBS sequences) ...
}
```

---

## Common external watchdog ICs

| Part | Timeout | Features |
|------|---------|----------|
| TPS3823-33 | Fixed 1.6 s | 3.3 V VCC, push-pull RST, WDI |
| MAX823 | 1.6 s typical | 5-pin SOT23, very common |
| STM6823 | 1.6 s | Low power, small SOT23-5 |
| TPS3431 | Programmable | Capacitor-programmable timeout, windowed mode |
| ADM8323 | 1.6 s | Ultra-low 6 µA supply current |

For the E-Trike, a **programmable-timeout part** (e.g., TPS3431) is preferred — the 100 ms target is shorter than most fixed-timeout off-the-shelf watchdogs (which typically start at 200–400 ms minimum).

---

## Testing the watchdog

1. **Force a hang:** Comment out the GPIO toggle in `safety_task`. Confirm the MCU resets within 100 ms.
2. **Verify safe state during reset:** Probe the MCP4725 output with an oscilloscope — it should fall to 0 V within microseconds of the reset line asserting.
3. **Verify gear relays open:** Probe the relay coil driver — it should de-energize immediately.
4. **Measure reset-boot-recovery time:** From watchdog fire to first valid CAN transmission. Should be <500 ms.

---

## Architecture gap note

The current `architecture.md` does **not** describe the external watchdog. This note fills that gap. The watchdog should be added to the architecture's SYS ESP32-S3 hardware section (§8.8) and the RTOS task layout table (§8.7).

---

*Primary reference: [[emergency-system]] for the complete ESTOP system including watchdog's role in the 8-layer defense and post-reset recovery procedures.*

*See also: [[defense-in-depth-safety]] for the layered safety approach that the watchdog is part of, [[listen-before-speaking]] for the LBS sequence that runs after every watchdog reset, [[architecture]] §8 for SYS ESP32-S3 details.*
