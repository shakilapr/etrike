# Architecture Change Specification: Adapting `mtr-stm32` (v1) for `mtr-stm32-v2` Hardware

**Document Version:** 1.0.0  
**Target Board:** WeAct Studio STM32G431CBU6 Core Board  
**Base Repository:** `mtr-stm32/` (PlatformIO C++ / FreeRTOS)  
**Target Hardware Parity:** `mtr-stm32-v2/` (STM32CubeIDE / FDCAN1 / Decoder Relays)

---

## 1. Executive Summary

This specification outlines the architectural changes required to bring the original **`mtr-stm32` (v1)** PlatformIO C++/FreeRTOS codebase into full compatibility with the **`mtr-stm32-v2` hardware specification**. 

The migration retains the multi-threaded FreeRTOS safety architecture of v1 while upgrading the peripheral hardware drivers (FDCAN1, Software Bit-Banged I2C, 2-to-4 Gear Decoder, and ADC Pedestal Scaling) to achieve 100% hardware interchangeability with v2 boards.

---

## 2. Hardware & Pinout Mapping Changes

### 2.1 Peripheral & Pin Re-allocation Matrix

| Peripheral Function | `mtr-stm32` (v1 Legacy) | `mtr-stm32-v2` (Target Hardware) | Migration Action in `config.h` |
| :--- | :--- | :--- | :--- |
| **MCU Microcontroller** | `STM32F103C8` / `STM32G431CB` | **STM32G431CBU6** | Update `platformio.ini` target board |
| **CAN Transceiver Pins** | `PB8` (RX), `PB9` (TX) (bxCAN) | **`PA11` (RX), `PA12` (TX) (FDCAN1)** | Update GPIO alternate functions for FDCAN1 |
| **Throttle DAC I2C SCL** | `PB6` | **`PA5`** | Update `kThrottleI2cScl = 5` (GPIOA) |
| **Throttle DAC I2C SDA** | `PB7` | **`PA7`** | Update `kThrottleI2cSda = 7` (GPIOA) |
| **Throttle ADC Input** | `PA0` (ADC1_IN0) | **`PA0` (ADC1_IN1)** | Maintain `PA0` ADC channel configuration |
| **Gear Decoder Line A** | `PA3` (Discrete MOSFET) | **`PA0` (`DEC_A`)** | Reallocate PA0 to Decoder Bit 0 |
| **Gear Decoder Line B** | `PA4` (Discrete MOSFET) | **`PA1` (`DEC_B`)** | Reallocate PA1 to Decoder Bit 1 |
| **Gear Decoder Enable** | `PA5` (Discrete MOSFET) | **`PB0` (`DEC_EN` Active Low)** | Reallocate PB0 to Active-Low Enable |
| **Hardware ESTOP Pin** | `PA1` | **`PA1` (NC, Active Low)** | Configure PA1 GPIO Input with Pull-Up |
| **Status LED** | N/A | **`PC6` (Active Low)** | Add PC6 Output status pin |

---

## 3. Peripheral Driver & Module Architectural Changes

### 3.1 CAN Bus Driver Migration (`can_driver.h`)

* **Legacy Behavior**: Used standard bxCAN API (`CAN1`) with manual frame unpacking.
* **Target Architecture**:
  1. Upgrade peripheral initialization to use STM32G4 **FDCAN1** (`HAL_FDCAN_*`).
  2. Implement hardware RX FIFO 0 acceptance filters for:
     * `0x204` (VCU Drive Command)
     * `0x001` (Safety ESTOP)
     * `0x205` (Brake Command)
  3. Integrate VCU drive command **byte-sum checksum verification** before accepting CAN speed and gear targets:
     $$\text{Calculated Checksum} = \sum_{i=0}^{3} \text{speed\_byte}_i + \sum_{j=4}^{6} \text{payload\_byte}_j$$
  4. Track VCU command rolling counter (`rollCnt`) to detect repeated or dropped frames.

---

### 3.2 Throttle DAC Driver Upgrade (`mcp4725_dac.h`)

* **Voltage Pedestal Scaling**:
  * Implement output voltage bounds to match motor controller input range (0.8V to 4.8V):
    * `DAC_MIN_VAL = 655` (0.8 V pedestal at 0 mm/s)
    * `DAC_MAX_VAL = 3931` (4.8 V ceiling at 3000 mm/s)
  * Formula for linear DAC output:
    $$\text{DAC Count} = 655 + \left\lfloor \frac{\text{speed\_mmps} \times (3931 - 655)}{3000} \right\rfloor$$
* **Emergency Stop Zeroing**:
  * In `MODE_ESTOP`, DAC output must be explicitly driven to **`0.0 V` (`0` count)**.
* **I2C Bus Scanner**:
  * Add startup I2C address scanner probing standard 7-bit addresses (`0x08` to `0x77`). Store the detected MCP4725 address (`0x60` or `0x61`) and publish via debug CAN frame `0x700`.

---

### 3.3 Gear Actuation Decoder Driver (`gear_control.h`)

* **Decoder Logic Map**:
  Replace discrete MOSFET outputs with 2-to-4 decoder hardware logic:

  ```
  targetGear == 0 (Neutral) -> DEC_A = 0, DEC_B = 0, DEC_EN = HIGH (Disabled)
  targetGear == 1 (Drive)   -> DEC_A = 1, DEC_B = 0, DEC_EN = LOW  (Channel Y1)
  targetGear == 2 (Sport)   -> DEC_A = 0, DEC_B = 1, DEC_EN = LOW  (Channel Y2)
  targetGear == 3 (Reverse) -> DEC_A = 1, DEC_B = 1, DEC_EN = LOW  (Channel Y3)
  ```

* **Hardware ESTOP Isolation**:
  When `g_estop_active` is `true`, `DEC_EN` must be pulled **`HIGH`**, disabling all gear contactor relays at the hardware level.

---

### 3.4 Safety Watchdog & State Machine Refactoring (`main.cpp` & `config.h`)

1. **Watchdog Timeout Reduction**:
   * Change command staleness timeout from 200 ms to **100 ms** (`kCmdStaleTimeoutMs = 100`).
2. **Auto-Recovery Logic**:
   * If a watchdog timeout occurs (`g_fault_flags |= kFaultCmdTimeout`), automatically recover to `MODE_AUTO` when valid `0x204` CAN commands resume, provided no latched hardware ESTOP flag is set.
3. **Dual Control Mode Support (`MODE_MANUAL` vs `MODE_AUTO`)**:
   * Read mode selection pin (`PB4`).
   * In `MODE_MANUAL`, sample local ADC grip (`PA0`) and optocoupler gear switches (`PB1`–`PB3`) to drive DAC and decoder lines directly.

---

## 4. Telemetry & Heartbeat Protocol Alignment

The FreeRTOS `task_can_tx` task must be updated to broadcast the following standardized messages:

1. **`0x206` `MTR_MOTOR_FBK`** (50 Hz / 20 ms period):
   * Payload: `{actual_speed_mmps (i16), gear_state (u8), fault_flags (u8), motor_temp (i8), controller_temp (i8), motor_current_a (i16)}`
2. **`0x7FE` System Heartbeat** (2 Hz / 500 ms period):
   * Payload: `{heartbeat_alive_ctr (u8), system_uptime_sec (u32)}`
3. **`0x700` Debug I2C Address** (2 Hz / 500 ms period):
   * Payload: `{detected_i2c_addr (u8)}`

---

## 5. Verification & Test Plan

1. **Unit Verification (`pio run -e native`)**:
   * Verify checksum calculation routines and ADC pedestal mapping math.
2. **Hardware-in-the-Loop (HIL) Verification**:
   * **Oscilloscope / Probe**: Verify DAC pin outputs 0.8V at 0 mm/s, 4.8V at 3000 mm/s, and drops to 0.0V within 5 ms of hardware ESTOP activation.
   * **Logic Analyzer**: Verify FDCAN1 baud rate (500 kbps) and decoder pin timing (`DEC_A`, `DEC_B`, `DEC_EN`).
