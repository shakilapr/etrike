# MTR-STM32 — Build, Upload, and Operation Guide

Target hardware: **STM32G431CBU6** (Arm® Cortex®-M4F @ 16 MHz, 128 KB Flash, 32 KB SRAM).  
Framework: **STM32Cube HAL** with **C++17** (`arm-none-eabi-g++ 10.3`).

---

## 1. Prerequisites

1. **Python 3.10+** and **PlatformIO Core**:
   ```bash
   pip install platformio
   # or verify installation
   pio --version
   ```
2. **ST-LINK V2 / V3 Programmer**:
   - Hardware connections:
     - `SWCLK` $\rightarrow$ Target PA14
     - `SWDIO` $\rightarrow$ Target PA13
     - `GND` $\rightarrow$ Target GND
     - `3.3V` / `5V` $\rightarrow$ Target VCC
3. **ST-LINK USB Drivers**: Installed via STM32CubeProgrammer or Zadig.

---

## 2. Hardware Pinout Quick Reference

| Signal | STM32 Pin | Mode / Polarity | Purpose |
| :--- | :--- | :--- | :--- |
| **CAN RX** | **PA11** | Alternate Function AF9 | FDCAN1 Classic CAN differential RX (500 kbps) |
| **CAN TX** | **PA12** | Alternate Function AF9 | FDCAN1 Classic CAN differential TX (500 kbps) |
| **Mode Reverse Relay**| **PA0** | Active-Low Output | RESET = Relay ON (72V Reverse), SET = OFF |
| **Mode Drive Relay**  | **PA2** | Active-Low Output | RESET = Relay ON (72V Drive), SET = OFF |
| **Ignition Relay**    | **PA4** | Active-Low Output | RESET = Relay ON (Ignition ON), SET = OFF |
| **MCP4725 SCL**       | **PA5** | Open-Drain SW-I2C | Software I2C Clock to MCP4725 DAC |
| **MCP4725 SDA**       | **PA7** | Open-Drain SW-I2C | Software I2C Data to MCP4725 DAC |
| **Status LED**        | **PC6** | Active-Low Output | Toggles on state/mode transitions |

---

## 3. Building Firmware

From repository root or `mtr-stm32/` directory:

```bash
cd mtr-stm32

# Build production vehicle environment
pio run -e vehicle

# Clean build artifacts if needed
pio run -e vehicle -t clean
```

The compiled binary and ELF images are generated at:
- `.pio/build/vehicle/firmware.bin`
- `.pio/build/vehicle/firmware.elf`

---

## 4. Flashing / Uploading Firmware

### Option A: PlatformIO CLI (Recommended)
Connect your ST-LINK programmer to the board and PC:

```bash
cd mtr-stm32
pio run -e vehicle -t upload
```

PlatformIO automatically invokes OpenOCD/ST-Link and writes the firmware to flash address `0x08000000`.

### Option B: STM32CubeProgrammer GUI
1. Open **STM32CubeProgrammer**.
2. Set interface to **ST-LINK** (SWD, Normal mode) and click **Connect**.
3. Click **Open file** and browse to:
   `e:\work\etrike\mtr-stm32\.pio\build\vehicle\firmware.bin`
4. Set Start address: `0x08000000`.
5. Click **Download**.

### Option C: STM32CubeProgrammer CLI
```bash
STM32_Programmer_CLI -c port=SWD -w .pio/build/vehicle/firmware.bin 0x08000000 -v -rst
```

---

## 5. Dual-Toolchain: STM32CubeIDE (GUI Debugging)

To debug using STM32CubeIDE:
1. Open STM32CubeIDE.
2. Select **File $\rightarrow$ Open Projects from File System...** and choose the `mtr-stm32/` directory.
3. The `.project`, `.cproject`, and linker script `STM32G431CBUX_FLASH.ld` are pre-configured.
4. Click **Debug** (`VCU_2 Debug.launch`) for full single-step breakpoint debugging, register inspection, and live variable watch via ST-LINK.

---

## 6. CAN Command Acceptance

MTR accepts motor and relay commands from two operational sources on Low CAN (500 kbps):
1. **Mode 1 (Autonomous)**: Sourced from RT gateway (`0x204 RT_DRIVE_CMD`).
2. **Mode 2 (Remote Manual)**: Sourced directly from RM gateway (`0x204` canonical or fallback `0x0BB` relay + `0x0AA` throttle).
3. **Emergency Stop (`0x001 SAFETY_ESTOP`)**: De-energizes all relays immediately and clamps DAC to 0.0 V.

---

## 7. Running Native Tests

You can run the complete subsystem test suite (relays active-low mutual exclusion, MCP4725 software I2C multi-address probing and voltage window clamping, ESTOP and mode recovery, 50 ms direction shift arc-protection dwell, 500 ms comms watchdog timeout, legacy fallback frames, and FDCAN ringbuffer mechanics) directly on your host PC without hardware:

```powershell
cd mtr-stm32
g++ -std=c++17 -I . -I .. -I ../shared -I test/stub test/test_mtr_full_suite.cpp -o test_mtr_suite.exe
.\test_mtr_suite.exe
Remove-Item test_mtr_suite.exe
```
