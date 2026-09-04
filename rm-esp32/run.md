# RM-ESP32 — Build, Upload, and Operation Guide

Target hardware: **Classic ESP32** (`board = esp32dev`, Xtensa LX6 dual-core @ 240 MHz, not ESP32-S3).  
Framework: **ESP-IDF 5.5** with **C++17** (`-std=gnu++17`).

---

## 1. Prerequisites

1. **Python 3.10+** and **PlatformIO Core**:
   ```bash
   pip install platformio
   # or verify installation
   pio --version
   ```
2. **USB-UART Driver** for ESP32 (CP2102, CH340, or FTDI depending on development board).

---

## 2. Hardware Pinout Quick Reference

| Signal | ESP32 GPIO | Direction | Purpose |
| :--- | :--- | :--- | :--- |
| **CAN TX** | **GPIO 21** | Output | Low CAN bus transceiver TXD (500 kbit/s) |
| **CAN RX** | **GPIO 22** | Input | Low CAN bus transceiver RXD (500 kbit/s) |
| **CH0 (Steer)** | **GPIO 18** | Input | RMT Right Stick Horizontal ($\pm 450.0^\circ$ $\rightarrow$ `0x169`) |
| **CH1 (Brake)** | **GPIO 19** | Input | RMT Left Stick Vertical ($0\dots 27\text{ mm}$ $\rightarrow$ `0x7B9`) |
| **CH2 (Throttle/Trim)**| **GPIO 14** | Input | RMT VRA Dial ($0\dots 100\%$ speed trim / throttle $\rightarrow$ `0x204` / `0x0AA`) |
| **CH3 (Aux Pass)**| **GPIO 32** | Input | RMT VRB Rotary Dial |
| **CH4 (Ignition)** | **GPIO 13** | Input | RMT SWB 2-Pos Switch (Ignition ON / OFF $\rightarrow$ `0x112`) |
| **CH5 (Gear)** | **GPIO 4** | Input | RMT SWC 3-Pos Switch (UP=Rev, MID=Neutral, DOWN=Drive $\rightarrow$ `0x111` / `0x0BB`) |

---

## 3. Building Firmware

From repository root or `rm-esp32/` directory:

```bash
cd rm-esp32

# Build production vehicle environment (default)
pio run -e vehicle

# Build bench simulation environment
pio run -e bench

# Clean build artifacts if needed
pio run -e vehicle -t clean
```

> **Note on Build Duration**: The very first build takes ~2-3 minutes as ESP-IDF compiles the FreeRTOS kernel and HAL components from scratch. Subsequent incremental builds are cached and complete in **under 15 seconds**.

---

## 4. Flashing / Uploading Firmware

1. Connect the ESP32 to your PC via micro-USB or USB-C.
2. Check available serial ports:
   ```bash
   pio device list
   ```
3. Flash the binary:
   ```bash
   # Auto-detect COM port and upload
   pio run -e vehicle -t upload

   # Or specify COM port explicitly
   pio run -e vehicle -t upload --upload-port COM4
   ```

---

## 5. Serial Monitoring & Telemetry

Monitor serial logs at 115200 baud with automatic exception backtrace decoding:

```bash
# Upload and immediately open monitor
pio run -e vehicle -t upload -t monitor

# Or monitor separately
pio device monitor -b 115200
```

Expected startup output:
```text
I (310) rm: ========================================
I (310) rm:   RM-ESP32 Receiver Gateway (C++17)
I (310) rm:   Version: v0.8.0-alpha-rm
I (310) rm: ========================================
I (320) rm: CAN driver initialized successfully (TX=21, RX=22, 500 kbps)
I (330) rm: RC Receiver RMT channels 0-5 armed
```

---

## 6. Running Native Tests

You can run signal decoding and fail-safe deadman tests directly on your host PC without hardware:

```bash
cd rm-esp32
g++ -static -std=c++17 -I .. -I ../shared test/test_rm_receiver.cpp -o test_rm.exe
.\test_rm.exe
Remove-Item test_rm.exe
```
