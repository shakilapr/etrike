# MTR STM32G431 — Standalone Hardware Test Firmware (`mtr-stm32-test`)

Standalone bench test firmware for the **STM32G431CBU6** MTR board. Designed for hardware bringup, wiring verification, and sanity testing without requiring CAN communication, masters (RT/SYS/RM/Jetson), or complex safety state machines.

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

3. **No CAN Bus Dependency**:
   - Zero CAN traffic, no watchdog timeouts, no external masters required.

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

### Option A: PlatformIO CLI
```bash
# Build firmware
pio run -d mtr-stm32-test -e vehicle

# Upload via DFU
pio run -d mtr-stm32-test -e vehicle -t upload

# Upload via ST-Link (if using ST-Link programmer)
pio run -d mtr-stm32-test -e vehicle -t upload --upload-port stlink
```

### Option B: Binary Flashing
The compiled binary will be located at:
- `mtr-stm32-test/.pio/build/vehicle/firmware.bin`
- `mtr-stm32-test/.pio/build/vehicle/firmware.elf`
