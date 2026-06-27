# Hardware Safety Characteristics

Fail-safe behavior of each component at power-up, during reset, and under fault conditions.

## MCP4725 DAC (Throttle)

- **Power-up:** Output = 0V (PD=1, 1kΩ pulldown to GND per datasheet §6.3)
- **I2C bus stuck:** If SDA held LOW, DAC retains last value. Next power cycle restores 0V
- **MCU in reset:** I2C bus released (GPIOs float), DAC holds last value
- **Fail-safe verdict:** Motor controller sees 0V at power-up = safe. During operation, loss of I2C communication keeps last value — MTR staleness detection (200ms) must catch this

## TLP281 Gear Sensors (Optocoupler)

- **Power-up:** Output HIGH (LED off = transistor off = pull-up to 3.3V)
- **Active-low:** Triggered when 72V gear line energizes → LED on → transistor on → output LOW
- **Fail-safe:** All sensors HIGH = no gear detected = software treats as NEUTRAL

## Gear Relay Outputs

- **MCU GPIO during reset:** Input/floating (not driven)
- **Relay module:** Requires active HIGH drive from GPIO
- **Fail-safe:** Floating GPIOs → all relays OFF → motor ECU sees no gear selected = NEUTRAL

## SYNTREE EPS-C (Steering)

- **Internal boot delay:** ~2 seconds before entering centering routine
- **During boot:** Does not accept CAN commands. Standalone centering.
- **CAN timeout:** 20ms without valid 0x169 → holds last angle (internal watchdog)

## SYNTREE SEB (Brake)

- **Internal boot delay:** ~2 seconds before reporting aligned status
- **During boot:** Does not accept CAN commands
- **CAN timeout:** 20ms without valid 0x7B9 → holds last position

## ESTOP Button

- **Type:** NC (normally-closed), active-low
- **Wired to:** SYS GPIO1, MTR PA1 (dual-path, independent MCUs)
- **Latency:** Button press → GPIO edge <1ms (mechanical bounce ~5ms)
- **Hardware path (Level 3):** Button → MTR PA1 → immediate DAC=0V + relays OFF (no CAN dependency)
- **Software path (Level 2):** Button → SYS GPIO1 → CAN 0x001 → all nodes

## TPS3850 Watchdog

- **Type:** External window watchdog IC
- **Window:** ~100ms (requires toggle in that window)
- **Toggled by:** RT: t_control at 100 Hz, SYS: task_safety at 20 Hz
- **On timeout:** Asserts RST, holds MCU in reset until WDT is re-armed

## Power-Up Sequence

1. 72V traction battery → DC-DC converter (72V→12V) → 12V rail
2. 12V → ESP32-S3 + MTR STM32 regulators → 3.3V MCU rails
3. 3.3V stable → MCUs boot → CAN init → 500ms internal boot
4. MCP4725 powered from 5V rail (derived from 12V) — outputs 0V at power-up
5. Relays require active HIGH from initialized GPIOs — safe during MCU boot

## Brownout Behavior

- If 12V rail drops below ~9V, DC-DC converter may brown out
- 3.3V rail drops → MCU brownout detector triggers reset
- During reset: DAC holds last value, relays OFF (GPIOs float)
- On recovery: MCU reboots, CAN reinitializes, 3000ms grace period
