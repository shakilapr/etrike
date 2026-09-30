# Waveshare ESP32-S3 4.3" Cockpit HMI (SKU: 28141)

Dedicated, standalone in-cabin instrument cluster and diagnostic HMI running on the **Waveshare ESP32-S3-Touch-LCD-4.3B-BOX**.

---

## 1. System Architecture: Dual-Display Topology

```
+-----------------------------------------------------------------------------------+
|                                 VEHICLE CAN BUS                                   |
|       (500 kbps Classic CAN 2.0 - SYS, RT, MTR, SEB, SES, HOST Protocol Frames)    |
+--------------------------+------------------------------------+-------------------+
                           |                                    |
                           | CANH / CANL                        | CANH / CANL
                           v                                    v
+------------------------------------------+  +------------------------------------+
|  Waveshare ESP32-S3-Touch-LCD-4.3B-BOX   |  |          Vehicle Host PC           |
|            (Small Display HMI)           |  |       (Main Computer Display)      |
|                                          |  |                                    |
| - Standalone Driver Dashboard Cluster    |  | - tools/can_watcher                |
| - Instant Glance Speed, Gear & Telltales |  | - Full 3D Road Simulation & Mesh   |
| - Hardware TJA1051 Onboard Transceiver   |  | - Complete 64-bit Bit Matrix View  |
| - 7-36V Vehicle Power Direct Input       |  | - High-level Autoware Mission Plan |
+------------------------------------------+  +------------------------------------+
```

---

## 2. Hardware Specifications (Waveshare SKU 28141)

| Parameter | Value |
| :--- | :--- |
| **Model** | Waveshare ESP32-S3-Touch-LCD-4.3B-BOX |
| **MCU** | ESP32-S3-WROOM-1-N16R8 (Dual-core Xtensa LX7 @ 240 MHz) |
| **Memory** | 16 MB Quad SPI Flash, 8 MB Octal PSRAM, 512 KB SRAM |
| **Display** | 4.3-inch IPS, 800 × 480 pixels, 65K colors (ST7262 RGB interface) |
| **Touch** | Capacitive 5-point multitouch (Goodix GT911) |
| **CAN Interface** | Onboard TJA1051 transceiver connected to ESP32-S3 GPIO15 (TX) & GPIO16 (RX) |
| **CAN Physical** | 16-pin removable terminal (`CANH`, `CANL`), switchable 120Ω termination |
| **Power Input** | 7–36 V DC wide-range vehicle power input |
| **Isolated I/O** | 2x Isolated Inputs (DI0, DI1, 5-36V), 2x Isolated Outputs (DO0, DO1, 450mA) |
| **RTC** | PCF85063A with CR927 battery backup |
| **Storage** | MicroSD / TF slot via SPI |

---

## 3. Terminal Block Wiring (16-Pin Green Connector)

```
+----+----+-----+-----+------+------+------+------+----+----+-----+-----+-----+-----+
| VIN| GND| 3V3 | GND | CANH | CANL | 485A | 485B | DI0| DI1| DO0 | DO1 | SDA | SCL |
+----+----+-----+-----+------+------+------+------+----+----+-----+-----+-----+-----+
   |    |                |      |                   |    |    |
   +----+                +------+                   |    |    +---> Buzzer / Relay
     |                      |                       |    +--------> Aux / Sensor
  12V Vehicle            Vehicle CAN               Ignition (DI0)
  Power Input            High / Low
```

> [!IMPORTANT]
> The onboard CAN termination switch is disabled by default. If this display unit sits at the physical end of your vehicle CAN trunk, toggle the switch to enable the 120Ω resistor.

---

## 4. LVGL Pro Editor Project Structure

The UI layout is defined in declarative **LVGL XML**, fully compatible with **LVGL Pro Editor**:

```
lvgl_project/
├── project.json                 # Project descriptor (800x480, 16-bit, dark theme)
├── styles/
│   └── styles.xml               # Automotive dark carbon theme, neon accents & telltales
├── components/
│   ├── telltale.xml             # ISO 2575 warning telltales (Park, Brake, EPS, Estop, etc.)
│   ├── turn_signal.xml          # 52px high-visibility circular turn indicators
│   ├── prnds_bar.xml            # PRNDS gear selector buttons with cyan neon glow
│   ├── center_steer_bar.xml     # Bi-directional center-detent steering gauge
│   └── metric_bar.xml           # Rounded bar gauge for brake pressure & motor throttle
└── views/
    ├── cockpit_main.xml         # Primary 800x480 instrument cluster screen
    └── diagnostics_view.xml     # Secondary hardware & CAN error statistics screen
```

### Opening in LVGL Pro Editor
1. Open **LVGL Pro Editor**.
2. Select **Open Project** and navigate to `tools/esp32_cockpit_hmi/lvgl_project/project.json`.
3. The editor renders the live 800×480 preview with interactive widgets, style inspector, and Figma sync.

---

## 5. Firmware Architecture (ESP-IDF v5.x + FreeRTOS)

* **Dual-Core Processing**:
  - **CPU Core 0**: Real-time TWAI (CAN 2.0) receiver task running at 500 kbps, zero-copy packet ingestion.
  - **CPU Core 1**: LVGL v9 GUI task rendering at 60 FPS to the 800×480 RGB LCD via DMA in Octal PSRAM.
* **CAN Frame Decoding**:
  - `0x011` (`SYS_SAFETY_STS`): Feedback lights (left/right/hazard/highbeam/brake) and ESTOP.
  - `0x121` (`RT_MOTION_RPT`): Actual speed (km/h), gear, yaw rate (mrad/s), motor RPM.
  - `0x210` (`RT_STATE_RPT`): Driving mode (`MANUAL`, `AUTO`, `ESTOP`).
  - `0x211` (`RT_DIAG_RPT`): Bus-off status, Transmit (TEC) and Receive (REC) error counters.
  - `0x300` (`HOST_DRIVE_CMD`): Commanded speed and gear.
  - `0x301` (`HOST_BRAKE_REQ`): Commanded brake pressure.
  - `0x303` (`HOST_STEER_CMD`): Commanded steering angle (-45° to +45°).
  - `0x310` (`STEER_DIAG`): Actual steering angle, EPS motor current, ECU temperature, fault status.
  - `0x311` (`BRAKE_DIAG`): Actual hydraulic brake pressure, ECU temperature, fault status.

---

## 6. Building and Flashing Firmware

### Prerequisites
* ESP-IDF v5.1 or later installed.
* USB-C cable connected to the ESP32-S3 USB port.

### Build and Flash
```bash
cd tools/esp32_cockpit_hmi/firmware

# Set target to ESP32-S3
idf.py set-target esp32s3

# Build firmware
idf.py build

# Flash and monitor over USB-C
idf.py -p COM_PORT flash monitor
```
*(Replace `COM_PORT` with your serial device, e.g. `COM5` on Windows or `/dev/ttyUSB0` on Linux).*
