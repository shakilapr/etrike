# Firmware Flashing Guide

How to compile and upload firmware to each ECU in the E-Trike system.

## Prerequisites

- **PlatformIO CLI** (`pio`) — installed and on PATH
- **USB drivers** — Silicon Labs CP210x or similar for ESP32-S3 USB-serial
- **STM32 programmer** (MTR only) — ST-Link/V2 or USB-TTL serial adapter

## Quick Reference

| Module | Board | Framework | Default Env |
|--------|-------|-----------|-------------|
| `rt-esp32/` | ESP32-S3-DevKitC-1 | ESP-IDF | `vehicle` |
| `sys-esp32/` | ESP32-S3-DevKitC-1 | ESP-IDF | `vehicle` |
| `pwt-esp32/` | ESP32-S3-DevKitC-1 | ESP-IDF | `vehicle` |
| `mtr-stm32/` | STM32F103C8 (Blue Pill) | STM32Cube | `vehicle` |
| `debug-tool/debug-esp32/` | ESP32-S3-DevKitC-1 | ESP-IDF | `esp32-s3` |
| `rt-test/` | ESP32-S3-DevKitC-1 | ESP-IDF | `esp32-s3-devkitc-1` |

## Identify Connected Boards

Each ESP32-S3 will appear as a COM port. To tell them apart, check the USB serial number or MAC address shown during upload. Alternatively, connect one at a time and note the port:

```powershell
# Windows: list serial ports
Get-WmiObject Win32_SerialPort | Select-Object DeviceID, Description
```

Or use PlatformIO's device list:
```bash
pio device list
```

## Compile Only (No Hardware Required)

```bash
cd rt-esp32   && pio run          # RT kinematics + steering
cd sys-esp32  && pio run          # Safety + body control
cd pwt-esp32  && pio run          # Powertrain gateway
cd mtr-stm32  && pio run          # Motor control
cd rt-test    && pio run          # RT test harness
```

To build a specific environment:
```bash
cd rt-esp32   && pio run -e bench    # Bench test environment
cd sys-esp32  && pio run -e bench    # Bench test environment
cd mtr-stm32  && pio run -e bench    # Bench test environment
```

## Compile and Upload

Plug in the target board, then run the full build+flash from its directory:

```bash
# RT — Realtime kinematics + steering (ESP32-S3)
cd rt-esp32 && pio run --target upload

# SYS — Safety + body control (ESP32-S3)
cd sys-esp32 && pio run --target upload

# PWT — Powertrain gateway (ESP32-S3)
cd pwt-esp32 && pio run --target upload

# MTR — Motor control (STM32F103, requires ST-Link)
cd mtr-stm32 && pio run --target upload

# Debug tool CAN bridge (ESP32-S3)
cd debug-tool/debug-esp32 && pio run --target upload

# RT test harness (ESP32-S3)
cd rt-test && pio run --target upload
```

For bench-test environments, add `-e bench`:
```bash
cd rt-esp32  && pio run -e bench --target upload
cd sys-esp32 && pio run -e bench --target upload
cd mtr-stm32 && pio run -e bench --target upload
```

## Monitor Serial Output

After flashing, open the serial monitor (115200 baud):

```bash
cd rt-esp32  && pio device monitor
cd sys-esp32 && pio device monitor
```

Exit with `Ctrl+C`.

## Common Flash Layout

Each ESP32-S3 target uses the same flash regions:

| Address | Size | Content |
|---------|------|---------|
| `0x000000` | 24 KB | Bootloader |
| `0x008000` | 4 KB | Partition table |
| `0x010000` | ~7.9 MB | Application firmware |

STM32 targets flash to `0x08000000` (internal flash).

## Troubleshooting

### Wrong port / multiple boards connected
Pass the port explicitly with `--upload-port`:
```bash
pio run --target upload --upload-port COM5
```

### Flash size mismatch warning
`Warning! Flash memory size mismatch detected. Expected 8MB, found 2MB!`

This is cosmetic for current firmware sizes (~250-270 KB). If your board truly has 2 MB flash, adjust in `sdkconfig.defaults` or ignore while usage stays under 2 MB.

### Permission denied / access denied (Windows)
Make sure no other serial terminal (PuTTY, Arduino IDE, PlatformIO Home monitor) has the COM port open. Only one application can open a serial port at a time.

### MTR STM32 upload fails
Ensure you are using an ST-Link/V2 programmer, not USB-serial. For serial upload, configure `upload_protocol = serial` in `platformio.ini` and set the correct COM port.

### Build fails with missing CAN headers
The pre-build script regenerates CAN data from YAML. If it fails, run manually:
```bash
cd shared/can && python pio_prebuild.py
```
