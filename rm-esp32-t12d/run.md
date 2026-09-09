# RM-ESP32-T12D — Build, Upload, and Operation Guide

Target hardware: **ESP32** (`board = esp32dev`) or **ESP32-S3** (`board = esp32-s3-devkitc-1-n16r8`).  
Framework: **ESP-IDF 5.5** with **C++17** (`-std=gnu++17`).  
Receiver: **RadioLink R16F V1.0** via single-wire **SBUS** (100k, 8E2, inverted).

---

## 1. Prerequisites

1. **Python 3.10+** and **PlatformIO Core**:
   ```bash
   pip install platformio
   pio --version
   ```
2. **USB-UART Driver** for ESP32 (CP2102, CH340, or FTDI depending on board).

---

## 2. Hardware Pinout

| Signal | ESP32 GPIO | Direction | Purpose |
| :--- | :--- | :--- | :--- |
| **CAN TX** | **GPIO 5** | Output | Low CAN transceiver TXD (500 kbit/s) |
| **CAN RX** | **GPIO 4** | Input | Low CAN transceiver RXD (500 kbit/s) |
| **SBUS RX** | **GPIO 16** | Input | R16F CH16 S.BUS Signal (100 kbps, 8E2, inverted) |
| **5V Supply** | **5V Pin** | Power | R16F Power (~50 mA @ 5V) |
| **GND** | **GND Pin** | Ground | Common system ground |

---

## 3. Building Firmware

From repository root or `rm-esp32-t12d/` directory:

```powershell
cd rm-esp32-t12d

# Build production vehicle environment (Classic ESP32)
pio run -e vehicle

# Build bench simulation environment (emulates SYS safety authority 0x011)
pio run -e bench

# Build for ESP32-S3 board
pio run -e esp32s3

# Clean build artifacts if needed
pio run -e vehicle -t clean
```

---

## 4. Flashing / Uploading Firmware

```powershell
cd rm-esp32-t12d

# Auto-detect COM port and upload
pio run -e vehicle -t upload

# Or specify COM port explicitly
pio run -e vehicle -t upload --upload-port COM4
```

---

## 5. Serial Monitoring & Telemetry

```powershell
# Open serial monitor at 115200 baud
pio run -e vehicle -t upload -t monitor
```

Expected startup banner:
```text
I (310) rm_t12d: =================================================
I (310) rm_t12d:   RM-ESP32-T12D Receiver Gateway (RadioLink SBUS)
I (310) rm_t12d:   Version: v0.8.0-clean-rm-t12d
I (310) rm_t12d: =================================================
I (320) can: TWAI TX=5 RX=4 @ 500 kbit/s
I (330) rc_rx: Initialized SBUS UART1 on RX GPIO 16 (100k, 8E2, inverted)
I (340) rm_t12d: All tasks created successfully. RM-ESP32-T12D operational.
```

1 Hz Telemetry Log Example:
```text
I (1340) rm_t12d: STATUS | Link=OK Valid=1 Enable=1 Gear=D Steer=+12.4 deg Brk=0.0 mm Throt=45% Spd=1350 | CAN ok=120 fail=0
```

---

## 6. Running Native Unit Tests

Run the complete native test suite directly on your host PC:

```powershell
cd rm-esp32-t12d
& C:\TDM-GCC-64\bin\g++.exe -std=c++17 -I src -I .. -I ../shared src/sbus_parser.cpp test/test_rm_t12d_suite.cpp -o test_rm_t12d.exe
.\test_rm_t12d.exe
Remove-Item test_rm_t12d.exe
```
