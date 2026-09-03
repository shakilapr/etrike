# VCU_2 / MTR — STM32G431 Motor & Relay Actuator Node: Architecture

> **ECU Role:** Motor Control Unit (MTR / Actuator Node)  
> **Silicon:** STMicroelectronics STM32G431CBU6 (Arm® Cortex®-M4F @ 16 MHz, 128 KB Flash, 32 KB SRAM)  
> **Bus:** CAN Low Bus (Classic CAN 2.0A @ 500 kbit/s via internal FDCAN1 + external transceiver)  
> **Language & Standard:** C++17 (`arm-none-eabi-g++ -std=gnu++17`)  
> **Toolchains Supported:** Dual-workflow: **STM32CubeIDE** (GUI project / build / debug) & **PlatformIO** (`pio run -e vehicle -t upload`)

---

## 1. System Overview & Dual Toolchain Workflow

The `mtr-stm32` firmware functions as the primary vehicle traction actuator and relay controller. It interfaces with:
1. **Low-CAN Bus (500 kbps)**: Receives authoritative drive commands (`0x204 RT_DRIVE_CMD`), system mode state (`0x110 SYS_MODE_CMD`), and global emergency stops (`0x001 SAFETY_ESTOP`). Broadcasts motor telemetry (`0x120 SYS_THROTTLE_STS` at 100 Hz, `0x206 MTR_MOTOR_FBK` at 50 Hz).
2. **Relay Driver Outputs**: 3 active-low relay switches for **Ignition** (PA4), **Drive** (PA2), and **Reverse** (PA0).
3. **Software I2C DAC Output**: Drives MCP4725 12-bit DAC on PA5 (SCL) and PA7 (SDA) to output an analog throttle voltage ($0.8\text{ V}\dots 2.4\text{ V}$) directly to the motor controller.

### Dual Toolchain Execution
- **Workflow A: STM32CubeIDE (GUI)**
  - Open `mtr-stm32/` directly via `File -> Open Projects from File System...`.
  - The project files (`.project`, `.cproject`, `STM32G431CBUX_FLASH.ld`, and `VCU_2 Debug.launch`) configure the GCC ARM C++ compiler and linker.
  - Full ST-LINK / SWD single-step debugging, live variable watch, and memory inspect.
- **Workflow B: PlatformIO / Terminal**
  - Run `pio run -e vehicle -t upload` from the repository root or terminal.
  - Automatically invokes `framework-stm32cubeg4` with `toolchain-gccarmnoneeabi`.

---

## 2. Hardware Pin Map & Actuator Interfaces

All pin assignments from `mtr-stm/architecture.md` §6 and physical wiring specifications are 100% strictly preserved:

| Pin | Function | Mode | Logic / Polarity | Description |
|---|---|---|---|---|
| **PA0** | Mode Reverse Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (72V Rev), SET = Relay OFF |
| **PA2** | Mode Drive Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (72V Drive), SET = Relay OFF |
| **PA4** | Ignition Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (Ignition ON), SET = Relay OFF |
| **PA5** | SW-I2C SCL | Output Open-Drain | Idle HIGH (pull-up) | Clock line for MCP4725 DAC |
| **PA7** | SW-I2C SDA | Output Open-Drain | Idle HIGH (pull-up) | Data line for MCP4725 DAC (ACK sampled low) |
| **PA11** | FDCAN1_RX | Alternate Function (AF9) | — | CAN differential receiver input from transceiver |
| **PA12** | FDCAN1_TX | Alternate Function (AF9) | — | CAN differential transmitter output to transceiver |
| **PC6** | Status LED | Output Push-Pull | **Active-Low** | Toggles on mode/relay state transitions |

### Power-Up Safe State
On reset, prior to enabling GPIO drivers:
- PA0, PA2, PA4 are immediately forced to `GPIO_PIN_SET` (all relays de-energized).
- PA5, PA7 are forced to `GPIO_PIN_SET` (I2C bus floating high).
- MCP4725 DAC is explicitly commanded to code `0` ($0.0\text{ V}$).

---

## 3. Clock & Bus Configuration

- **Core Voltage**: `PWR_REGULATOR_VOLTAGE_SCALE1`.
- **Clock Source**: 16 MHz High-Speed Internal (HSI) oscillator direct (`RCC_PLL_NONE`).
- **Buses**: `SYSCLK = HCLK = PCLK1 = PCLK2 = 16 MHz`. Flash latency 0 wait states.
- **FDCAN1 Bit Timing (500 kbit/s, 87.5% sample point)**:
  - FDCAN Kernel Clock = `PCLK1 = 16 MHz`.
  - Nominal Prescaler = 2 $\rightarrow$ Time quantum ($t_q$) = 125 ns.
  - Nominal Sync Jump Width = 2.
  - Nominal TimeSeg1 = 13 ($t_q$).
  - Nominal TimeSeg2 = 2 ($t_q$).
  - Total bit time = $1 + 13 + 2 = 16\times 125\text{ ns} = 2.0\,\mu\text{s} \rightarrow 500\text{ kbit/s}$.

---

## 4. Canonical CAN Protocol Specification

### 4.1 Received Messages (RX Path via FIFO0)
Hardware filter acceptance list in FDCAN message RAM:
1. `0x001` — **`SAFETY_ESTOP`** (DLC 0):
   - Global emergency stop. Immediate de-energization of all relays and DAC to $0.0\text{ V}$.
2. `0x110` — **`SYS_MODE_CMD`** (DLC 1):
   - `0 = MANUAL`, `1 = AUTO`, `2 = ESTOP`.
3. `0x204` — **`RT_DRIVE_CMD`** (DLC 5, 50 Hz):
   - `motor_speed_mmps` (int32, big-endian, $[-500, 3000]$ mm/s).
   - `gear` (uint8, `0 = N`, `1 = D`, `2 = S`, `3 = R`).
4. `0x0BB` — **Legacy Relay State** (DLC 8, fallback compatibility):
   - `0x00` = OFF, `0x03` = Park, `0x05` = Drive, `0x09` = Reverse.
5. `0x0AA` — **Legacy Raw Throttle** (DLC 8, fallback compatibility):
   - `rxData[0:1]` = 16-bit raw analog throttle.

### 4.2 Transmitted Messages (TX Path via TX FIFO)
1. `0x120` — **`SYS_THROTTLE_STS`** (DLC 2, 100 Hz / 10 ms period):
   - Signals: `speed_mmps` (int16). Reflects commanded motor speed.
2. `0x206` — **`MTR_MOTOR_FBK`** (DLC 4, 50 Hz / 20 ms period):
   - Signals:
     - `actual_speed_mmps` (int16): Estimated / commanded vehicle linear velocity.
     - `gear_state` (uint8): Actual engaged relay gear state (`0=N`, `1=D`, `2=S`, `3=R`).
     - `fault_flags` (uint8):
       - `Bit 0` (`0x01`): `kMtrFaultEstopActive` (Asserted during ESTOP; fulfills Gap #15 redundant acknowledgment to SYS).
       - `Bit 4` (`0x10`): `kMtrFaultStartupReady` (Firmware initialized and armed).

---

## 5. Software Architecture & Class Hierarchy

Structured with the standard modular design from `sys-esp32` and `rt-esp32`:

```
mtr-stm32/src/
├── config.h               # Pin assignments, DAC limits, timings
├── can_driver.h / .cpp    # Handle-based FDCAN1 driver, FIFO0 ringbuffer, TX queue
├── relay_controller.h/.cpp# Active-low relay state machine with mutual exclusion
├── dac_controller.h/.cpp  # Bit-bang I2C MCP4725 driver with address scan & voltage clamps
├── motor_manager.h/.cpp   # Speed-to-DAC conversion, gear interlocking, CAN telemetry
└── main.cpp               # SysTick timebase, 500ms comms watchdog, 5ms main loop
```

### 5.1 Relay Interlock Rules
- **Safe State / Neutral / Park**: Ignition is ON, but **both** Drive (PA2) and Reverse (PA0) relays are de-energized (SET).
- **Drive**: Drive relay is energized (RESET), Reverse relay is strictly de-energized (SET).
- **Reverse**: Reverse relay is energized (RESET), Drive relay is strictly de-energized (SET).
- **Drive and Reverse relays can NEVER be energized simultaneously** in hardware or software.

### 5.2 Throttle Mapping & Clamping (MCP4725)
- **Idle / Zero Condition**:
  - When in Neutral/Park, ESTOP, or when commanded speed $\le 0$: DAC is set to **0 V** (Code `0`).
- **Active Drive Window**:
  - Commanded speed maps linearly across the active throttle window:
    $$\text{DAC Code} = \text{clamp}\left(655 + \frac{\text{speed\_mmps}}{3000} \times (1966 - 655), 655, 1966\right)$$
  - Lower floor: `DAC_MIN_VAL = 655` ($\approx 0.8\text{ V}$ at 5.0 V reference).
  - Upper ceiling: `DAC_MAX_VAL = 1966` ($\approx 2.4\text{ V}$ at 5.0 V reference).

### 5.3 Comms Watchdog & Fail-Safe Deadman
- Watchdog timer: **500 ms**.
- Armed after reception of the first valid CAN drive command.
- If no valid CAN frame arrives within 500 ms:
  - All relays are immediately de-energized (PA0, PA2, PA4 = SET).
  - MCP4725 DAC is reset to 0 V.
  - Motor feedback `fault_flags` asserts `kMtrFaultEstopActive`.
