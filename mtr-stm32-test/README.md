# MTR STM32G431 — Standalone Hardware Test Firmware (`mtr-stm32-test`)

Standalone bench test firmware for the **STM32G431CBU6** MTR board. Designed for hardware bringup, wiring verification, and sanity testing without requiring CAN communication, masters (RT/SYS/RM/Jetson), or complex safety state machines.

> **Full step-by-step bench procedure** (what to flash, what to observe, pass
> criteria, and a results log): see [`TESTING.md`](TESTING.md).

---

## 1. What This Firmware Does

1. **Relay Verification (Orderly On/Off Cycling)**:
   - Sequentially exercises the 3 active-low relays one by one:
     1. **Ignition Relay (PA4)** ON for 2.0 s $\to$ OFF for 1.0 s
     2. **Drive Relay (PA2)** ON for 2.0 s $\to$ OFF for 1.0 s
     3. **Reverse Relay (PA0)** ON for 2.0 s $\to$ OFF for 1.0 s
   - Cycles continuously in this order.
   - Enforces strict break-before-make and mutual exclusion between Drive and Reverse.
   - **PC6 Status LED** lights up when any relay is energized and turns off during pauses.

2. **DAC Throttle Verification (Smooth Sine Sweep)**:
   - Bit-banged I2C driver for **MCP4725** (SCL on PA5, SDA on PA7).
   - Automatically probes I2C addresses `0x60`, `0x61`, `0x62`.
   - Generates a continuous half-sine wave modulation:
     - **0 to max in 15 seconds**
     - **max to 0 in 15 seconds**
     - Period: 30 seconds total, continuously repeating.
   - Updated at 50 Hz (every 20 ms).
   - Default max amplitude: `1966` (~2.4 V at 5.0 V VCC reference, matching the safe motor controller throttle window).
   - Can be set to `4095` (full-scale 5.0 V) via `DAC_MAX_CODE` in `src/config.h`.

3. **No CAN Bus Dependency (default mode)**:
   - Default `vehicle` mode needs zero CAN traffic, no watchdog timeouts, no external masters.
   - Dedicated CAN test modes (`can_tx`, `can_rx`; see section 3) exercise the CAN paths explicitly.

---

## 2. Hardware Pinout

| Pin | Peripheral | Polarity / Mode | Target Actuator | Bench Verification Check |
| :--- | :--- | :--- | :--- | :--- |
| **PA4** | GPIO Output PP | **Active-Low** (RESET=ON) | Ignition Relay | Audible click & contact closure (2s ON, 1s OFF) |
| **PA2** | GPIO Output PP | **Active-Low** (RESET=ON) | Drive Relay | Audible click & contact closure (2s ON, 1s OFF) |
| **PA0** | GPIO Output PP | **Active-Low** (RESET=ON) | Reverse Relay | Audible click & contact closure (2s ON, 1s OFF) |
| **PC6** | GPIO Output PP | **Active-Low** (RESET=ON) | Status LED | LED lights during relay ON steps |
| **PA5** | GPIO Output OD | Open-Drain Pull-Up | MCP4725 I2C SCL | Clock pulses on oscilloscope / logic analyzer |
| **PA7** | GPIO Output OD | Open-Drain Pull-Up | MCP4725 I2C SDA | Data pulses on oscilloscope / logic analyzer |
| **DAC OUT** | MCP4725 Pin | Analog Voltage | Controller Throttle | 0.0 V $\to$ 2.4 V in 15s, 2.4 V $\to$ 0.0 V in 15s |

---

## 3. Build & Flash

### Test Modes

| Environment | TEST_MODE | What it tests |
| :--- | :--- | :--- |
| `vehicle` (default) | 0 | Relay sequencing + DAC sine sweep (sections 1 & 2) |
| `can_tx` | 1 | **CAN transmit path** — 1 Hz `0x206` frame, verdict on PC6 LED |
| `can_rx` | 2 | **CAN receive path** — passive listener, activity on PC6 LED |

### CAN Clock Experiment (`env:can_tx_hse` / `env:can_rx_hse`)

Same tests as `can_tx` / `can_rx`, but with the FDCAN kernel clock taken from the **external 8 MHz HSE crystal** instead of the 16 MHz internal HSI (ST recommends an accurate clock for real CAN; HSI is fine for loopback). Wire timing is identical (500 kbit/s, 16 Tq, 87.5 % sample point) — only clock accuracy changes (crystal ppm vs HSI ±1 %).

**Decision rule:**
- Both HSI and HSE variants work → adopt **HSE** in production (robust for the real vehicle).
- HSE fails (init failure LED) but HSI works → crystal missing or faulty on this board.
- HSI fails but HSE works → clock-integrity issue found by the experiment.

### Option A: PlatformIO CLI
```bash
# Relay + DAC hardware test (default)
pio run -d mtr-stm32-test -e vehicle -t upload

# CAN transmit path test
pio run -d mtr-stm32-test -e can_tx -t upload

# CAN receive path test
pio run -d mtr-stm32-test -e can_rx -t upload
```

### CAN Transmit Path Test (`env:can_tx`)

Sends one `0x206` **Classic CAN 2.0A** frame per second (DLC 4, payload zeros) with production-identical bit timing (500 kbit/s, 87.5 % sample point, matching `mtr-stm32`), then reads the FDCAN transmit outcome (`TXBRP` drained, `TEC`, `LEC`) and reports it on the **PC6 LED** — the only output channel available while the TX path itself is broken, since a failing frame cannot carry its own error report onto the bus. Judge **3+ seconds after power-on**:

> **Wire format note:** "FDCAN" is the STM32G431's only CAN controller (there is no bxCAN on this silicon) — the bus itself runs **Classic CAN 2.0A**, matching the ESP32 TWAI peers. No FD frames, no bit-rate switching.

| PC6 LED | Meaning |
| :--- | :--- |
| **Solid ON** | Frame transmitted (`TXBRP` drained, `TEC` flat, `LEC` clean) — **TX path proven** |
| **Slow blink** (1 Hz) | `LEC = 3` — ACK error: frame reached the bus, nobody acknowledged (partner missing / listen-only partner / termination) |
| **Fast blink** (5 Hz) | `LEC = 1/2/4/5/6` — bus-level error: PA12 / transceiver TX stage dead, silent, or miswired |
| **Dark** | Bus-Off latched (controller disconnected itself) |

Notes:
- Relays are held OFF and the DAC is untouched in this mode; the LED is owned entirely by the tester.
- Keep at least one ACKing node on the bus (Canalyst-II in normal mode counts) — otherwise a *correct* TX still reads as an ACK error.
- When the LED shows solid ON, the physical TX path is proven; production publishing can then be re-enabled in `mtr-stm32` (`kEnableCanBroadcast = true`).
- Expected on Canalyst-II when working: `0x206` frames at 1 Hz.

### CAN Receive Path Test (`env:can_rx`)

Passive listener (accept-all filter): verifies PA11 / transceiver RX stage / wiring independently of the production firmware.

| PC6 LED | Meaning |
| :--- | :--- |
| **Solid ON** | Frames are being received (traffic within the last 500 ms) |
| **Slow blink** (1 Hz) | FDCAN up and listening, but no frames received |
| **Fast blink** (5 Hz) | FDCAN init failure |

### Binary Locations
The compiled binaries are located at:
- `mtr-stm32-test/.pio/build/<env>/firmware.bin`
- `mtr-stm32-test/.pio/build/<env>/firmware.elf`
