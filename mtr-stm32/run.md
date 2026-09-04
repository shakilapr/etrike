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

### Option A: Direct USB Type-C (Built-in DFU — Recommended)
Upload directly through the onboard USB Type-C port without an external debugger.

1. **Enter ROM Bootloader Mode**:
   - Hold down the **`BOOT0`** button on the STM32 board.
   - Press and release the **`NRST`** (Reset) button once.
   - Release the **`BOOT0`** button.
   - *(The board enumerates in Windows as `DFU in FS Mode` with USB ID `0483:DF11`).*

2. **One-Time Windows Driver Setup (First-Time Only)**:
   - If Windows shows a driver warning for `DFU in FS Mode`:
     - Open [Zadig](https://zadig.akeo.ie/) $\to$ **Options $\to$ List All Devices**.
     - Select **`DFU in FS Mode`** (`0483:DF11`).
     - Choose **`WinUSB`** and click **Install Driver** / **Replace Driver**.

3. **Upload via PlatformIO**:
   ```bash
   cd mtr-stm32
   pio run -e vehicle -t upload
   ```
   *PlatformIO invokes `dfu-util` with target address `0x08000000:leave` to erase, download, verify, and immediately boot into the new firmware.*

> **Note on `Error during download get_status`**: When `dfu-util` finishes writing and submits the `"leave"` request, the STM32 hardware instantly jumps out of DFU mode and severs the USB DFU pipe to boot your firmware. The download is 100% complete and verified. Press **`NRST`** once for a clean hardware boot if needed.

### Option B: External ST-LINK (SWD Programmer)
If using an external ST-LINK V2 / V3 hardware debugger (connected to `SWCLK`, `SWDIO`, `GND`, `3.3V`):

Update `platformio.ini` to use ST-Link:
```ini
upload_protocol = custom
upload_command = $PROJECT_PACKAGES_DIR/tool-openocd/bin/openocd -s $PROJECT_PACKAGES_DIR/tool-openocd/openocd/scripts -f interface/stlink.cfg -f target/stm32g4x.cfg -c "program {$SOURCE} verify reset 0x08000000; shutdown"
```

Then run:
```bash
cd mtr-stm32
pio run -e vehicle -t upload
```

### Option C: STM32CubeProgrammer GUI / CLI
- **GUI**: Select **USB** or **ST-LINK** port $\to$ **Connect** $\to$ Load `.pio/build/vehicle/firmware.bin` at `0x08000000` $\to$ **Download**.
- **CLI**:
  ```bash
  # Over USB DFU
  STM32_Programmer_CLI -c port=usb1 -w .pio/build/vehicle/firmware.bin 0x08000000 -v -rst
  # Over ST-LINK SWD
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
