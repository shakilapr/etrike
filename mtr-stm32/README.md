# MTR STM32G431 — Motor & Relay Actuator Node

Dedicated STM32G431CBU6 motor actuation and relay control board for the E-Trike platform. Interfaces with the **Low-CAN bus (500 kbit/s, Classic CAN 2.0A)**, drives active-low gear/ignition relays, and outputs an analog throttle voltage ($0.8\text{ V}\dots 2.4\text{ V}$) via a 12-bit MCP4725 DAC.

---

## 1. Overview & Capabilities

- **Relay Actuation & Hardware Mutual Exclusion**:
  - 3 active-low relay drivers: **Ignition (PA4)**, **Drive (PA2)**, **Reverse (PA0)**.
  - Strict mutual exclusion ensures Drive and Reverse relays can **never be energized simultaneously**.
  - Default power-up state is strictly all relays OFF (`SET`).
- **Software I2C MCP4725 DAC (PA5 SCL / PA7 SDA)**:
  - Dynamically probes candidate 7-bit addresses `0x60`, `0x61`, `0x62`.
  - In Neutral, Park, ESTOP, or when throttle is $\le 0$, DAC is commanded to **$0.0\text{ V}$** (Code 0).
  - In Drive/Reverse, commanded linear speed maps to a clamped voltage window:
    $$\text{Code} \in [655, 1966] \implies \approx 0.8\text{ V}\dots 2.4\text{ V}$$
- **Canonical CAN Integration**:
  - **RX**: Decodes `0x204` (`RT_DRIVE_CMD`), `0x110` (`SYS_MODE_CMD`), and `0x001` (`SAFETY_ESTOP`).
  - **TX**: Broadcasts `0x120` (`SYS_THROTTLE_STS`) at 100 Hz and `0x206` (`MTR_MOTOR_FBK`) at 50 Hz.
  - **Gap #15 ESTOP Confirmation**: When ESTOP occurs, `0x206` asserts `kMtrFaultEstopActive`, providing SYS with redundant hardware confirmation.
- **500 ms Comms Watchdog**:
  - Automatically de-energizes all relays and forces DAC to $0.0\text{ V}$ on CAN silence.

---

## 2. Hardware Pinout (100% Retained)

| Pin | Function | Mode | Logic / Polarity | Description |
| :--- | :--- | :--- | :--- | :--- |
| **PA0** | Mode Reverse Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (72V Rev), SET = Relay OFF |
| **PA2** | Mode Drive Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (72V Drive), SET = Relay OFF |
| **PA4** | Ignition Relay | Output Push-Pull | **Active-Low** | RESET = Relay ON (Ignition ON), SET = Relay OFF |
| **PA5** | SW-I2C SCL | Output Open-Drain | Pull-Up | MCP4725 DAC Clock line |
| **PA7** | SW-I2C SDA | Output Open-Drain | Pull-Up | MCP4725 DAC Data line |
| **PA11** | FDCAN1_RX | AF9 Alternate Function | — | CAN receiver input from transceiver |
| **PA12** | FDCAN1_TX | AF9 Alternate Function | — | CAN transmitter output to transceiver |
| **PC6** | Status LED | Output Push-Pull | **Active-Low** | Toggles on relay/mode transitions |

---

## 3. Dual-Toolchain Workflows

### Option A: STM32CubeIDE (GUI)
1. Open STM32CubeIDE.
2. Choose **`File -> Open Projects from File System...`**, browse to `mtr-stm32/`, and click **Finish**.
3. Pre-configured files include:
   - [`.project`](.project) and [`.cproject`](.cproject): Configured for C++17 (`arm-none-eabi-g++ -std=gnu++17`).
   - [`STM32G431CBUX_FLASH.ld`](STM32G431CBUX_FLASH.ld): 128 KB Flash / 32 KB RAM linker script.
   - [`VCU_2 Debug.launch`](VCU_2%20Debug.launch): Pre-configured ST-LINK debug launch configuration.
4. Click **Build** (Hammer icon) or **Run / Debug** to flash via ST-LINK.

### Option B: PlatformIO (CLI)
```bash
cd mtr-stm32
pio run -e vehicle -t upload
```

### Run Native Host Tests
```bash
g++ -static -std=c++17 -I .. -I ../shared -o test_mtr_runner.exe test/test_mtr_motor.cpp
./test_mtr_runner.exe
```

---

## 4. Codebase Structure

```
mtr-stm32/
├── platformio.ini         # Multi-env PlatformIO configuration
├── new-architecture.md    # Detailed technical architecture specification
├── STM32G431CBUX_FLASH.ld # Linker script
├── Core/                  # ST Vendor Drivers & Startup
│   ├── Inc/               # main.h, stm32g4xx_hal_conf.h, stm32g4xx_it.h
│   ├── Src/               # HAL MSP, IRQ dispatchers, system clock, newlib stubs
│   └── Startup/           # startup_stm32g431cbux.s
├── src/                   # Modern C++ Actuator Stack
│   ├── config.h           # Pin assignments, DAC voltage limits, timings
│   ├── can_driver.h       # FDCAN1 Classic CAN 500 kbps driver with FIFO0 ringbuffer
│   ├── relay_controller.h # Active-low relay controller with mutual exclusion
│   ├── dac_controller.h   # Bit-banged I2C MCP4725 driver with address scanning
│   ├── motor_manager.h    # Speed-to-DAC conversion, gear interlocking, CAN telemetry
│   └── main.cpp           # 16 MHz HSI boot, 500 ms watchdog, 5 ms execution loop
└── test/
    └── test_mtr_motor.cpp # Native unit test suite (36 assertions)
```
