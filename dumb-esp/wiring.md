# DUMB-ESP Hardware Wiring & Dual-Bus Interface Specification

This document provides the exact pinout, electrical connections, and wiring diagrams for the standalone autonomous bridge (**DUMB-ESP**) running on **Classic ESP32 (`esp32dev`)** and **ESP32-S3** operating in the **Dual-Bus Architecture**.

---

## 1. System Interconnect Overview

```
+-------------------------------------------------------------+
|                     JETSON HOST (Autoware)                  |
|                 autoware_vehicle_bridge (can0)              |
+-------------------------------------------------------------+
                |                                |
             CAN_H                            CAN_L
                |   [120Ω Termination Resistor]  |
                +----------------(120Ω)----------+
                |                                |
+-------------------------------------------------------------+
|               EXTERNAL MCP2515 CAN MODULE                   |
|                   (High CAN Bus @ 500 kbps)                 |
|             Connected to Canalyst-II Port 0 (CAN 0)         |
+-------------------------------------------------------------+
    VCC    GND    SCK     SI(MOSI)  SO(MISO)   CS     INT
     |      |      |         |         |        |      |
    5V     GND   GPIO18   GPIO23    GPIO19   GPIO21  GPIO22   <-- Classic ESP32 (esp32dev)
    (5V)  (GND) (GPIO15) (GPIO16)  (GPIO17) (GPIO18)(GPIO47) <-- ESP32-S3
     |      |      |         |         |        |      |
+=============================================================+
|                      DUMB-ESP BOARD                         |
|                 Classic ESP32 (esp32dev) / S3               |
|                                                             |
| Status LED: GPIO 2 (Classic onboard LED) / GPIO 48 (S3 RGB) |
+=============================================================+
                  |         |         |        |
                 3V3       GND      GPIO5    GPIO4
                  |         |         |        |
                 VCC       GND       TXD      RXD
+-------------------------------------------------------------+
|               3.3V TWAI CAN TRANSCEIVER                     |
|           (e.g., SN65HVD230 / VP230 Module)                 |
|                   (Low CAN Bus @ 500 kbps)                  |
|             Connected to Canalyst-II Port 1 (CAN 1)         |
+-------------------------------------------------------------+
                |                                |
                +----------------(120Ω)----------+
                |   [120Ω Termination Resistor]  |
             CAN_H                            CAN_L
                |                                |
  +-------------+--------------------------------+-------------+
  |                              |                             |
+-------------------+   +-------------------+   +-------------------+
|   SES STEERING    |   |    SEB BRAKING    |   |    MTR MOTOR      |
|     ACTUATOR      |   |     ACTUATOR      |   |    CONTROLLER     |
|   (CAN 500k)      |   |   (CAN 500k)      |   |    (CAN 500k)     |
+-------------------+   +-------------------+   +-------------------+
```

---

## 2. Complete Pin Mapping Table

### A. Classic ESP32 (`esp32dev`, NodeMCU-32S, ESP32-WROOM-32) — Default Target

| ESP32 Pin | Signal Name | Direction | Connected Device Pin | Description & Wiring Notes |
|:---|:---|:---:|:---|:---|
| **GPIO 18** | `SPI_SCK` | Output | MCP2515 `SCK` | High CAN Hardware VSPI Clock (8 MHz) |
| **GPIO 23** | `SPI_MOSI` | Output | MCP2515 `SI` | High CAN Hardware VSPI Master-Out-Slave-In |
| **GPIO 19** | `SPI_MISO` | Input | MCP2515 `SO` | High CAN Hardware VSPI Master-In-Slave-Out |
| **GPIO 21** | `SPI_CS` | Output | MCP2515 `CS` | High CAN Active-LOW Chip Select |
| **GPIO 22** | `MCP_INT` | Input | MCP2515 `INT` | High CAN Active-LOW Interrupt (internal pull-up enabled) |
| **GPIO 5** | `TWAI_TX` | Output | Transceiver `TXD` / `CTX` | Low CAN Transmit to Actuators (500 kbps) |
| **GPIO 4** | `TWAI_RX` | Input | Transceiver `RXD` / `CRX` | Low CAN Receive from Actuators (500 kbps) |
| **GPIO 2** | `STATUS_LED` | Output | Onboard Blue LED | Solid=Auto Active, Blink=Standby, Fast Blink=Fault |
| **3V3** | `VCC_3V3` | Power | Transceiver `VCC` | 3.3V Power for TWAI Transceiver (SN65HVD230) |
| **5V / VIN** | `VCC_5V` | Power | MCP2515 `VCC` | 5V Power for MCP2515 Module (TJA1050 requires 5V) |
| **GND** | `GND` | Ground | All `GND` Pins | Common Ground reference across both modules & Jetson |

> [!IMPORTANT]
> **Canalyst-II Adapter Channel Mapping for Control Toolkit:**
> - **Canalyst Port 0 (CAN 0)** must connect to the **High CAN Bus** (MCP2515 / Jetson).
> - **Canalyst Port 1 (CAN 1)** must connect to the **Low CAN Bus** (TWAI / Actuators).

---

### B. ESP32-S3 (`esp32-s3-devkitc-1-n16r8`) Target

| ESP32-S3 Pin | Signal Name | Direction | Connected Device Pin | Description & Wiring Notes |
|:---|:---|:---:|:---|:---|
| **GPIO 15** | `SPI_SCK` | Output | MCP2515 `SCK` | SPI Clock (8 MHz, `SPI2_HOST`) |
| **GPIO 16** | `SPI_MOSI` | Output | MCP2515 `SI` | SPI Master-Out-Slave-In |
| **GPIO 17** | `SPI_MISO` | Input | MCP2515 `SO` | SPI Master-In-Slave-Out |
| **GPIO 18** | `SPI_CS` | Output | MCP2515 `CS` | Active-LOW Chip Select |
| **GPIO 47** | `MCP_INT` | Input | MCP2515 `INT` | Active-LOW Interrupt (internal pull-up enabled) |
| **GPIO 5** | `TWAI_TX` | Output | Transceiver `TXD` / `CTX` | Low CAN Transmit to Actuators (500 kbps) |
| **GPIO 4** | `TWAI_RX` | Input | Transceiver `RXD` / `CRX` | Low CAN Receive from Actuators (500 kbps) |
| **GPIO 48** | `RGB_LED` | Output | Onboard WS2812 | Status LED (Red=Fault/ESTOP, Blue=Manual, Green=Auto) |

---

## 3. High CAN Bus Wiring (Jetson Host Interface via MCP2515)

The **High CAN Bus** is an isolated channel connecting DUMB-ESP directly to the Jetson Host (`autoware_vehicle_bridge`).

### Connections:
1. **Classic ESP32 to MCP2515 SPI Module**:
   - `GPIO 18` $\to$ MCP2515 `SCK`
   - `GPIO 23` $\to$ MCP2515 `SI` (MOSI)
   - `GPIO 19` $\to$ MCP2515 `SO` (MISO)
   - `GPIO 21` $\to$ MCP2515 `CS`
   - `GPIO 22` $\to$ MCP2515 `INT`
   - `5V / VIN` $\to$ MCP2515 `VCC`
   - `GND` $\to$ MCP2515 `GND`
2. **MCP2515 to Jetson / CANalyst-II**:
   - `CAN_H` $\to$ Jetson `can0` CAN_H (or CANalyst-II Channel 1 CAN_H)
   - `CAN_L` $\to$ Jetson `can0` CAN_L (or CANalyst-II Channel 1 CAN_L)
   - `GND` $\to$ Jetson CAN Ground (ensures shared ground reference)
3. **Termination**:
   - Verify that **$120\ \Omega$ termination resistance** is present between `CAN_H` and `CAN_L`.

---

## 4. Low CAN Bus Wiring (Physical Actuator Interface via TWAI)

The **Low CAN Bus** connects DUMB-ESP's on-chip TWAI controller directly to the vehicle actuators (**SES**, **SEB**, and **MTR STM32**).

### Connections:
1. **ESP32 to TWAI CAN Transceiver (SN65HVD230 / VP230)**:
   - `GPIO 5` $\to$ Transceiver `TXD` (or `CTX`)
   - `GPIO 4` $\to$ Transceiver `RXD` (or `CRX`)
   - `3V3` $\to$ Transceiver `VCC` (SN65HVD230 is natively 3.3V)
   - `GND` $\to$ Transceiver `GND`
2. **Transceiver to Vehicle Actuator Harness**:
   - `CAN_H` $\to$ Vehicle Actuator Bus `CAN_H` (twisted pair)
   - `CAN_L` $\to$ Vehicle Actuator Bus `CAN_L` (twisted pair)
   - `GND` $\to$ Vehicle Logic Ground / Actuator Shield Ground
3. **Actuator Connections on Low Bus**:
   - **SES Steering Actuator**: Connect SES CAN_H and CAN_L to Low Bus.
   - **SEB Braking Actuator**: Connect SEB CAN_H and CAN_L to Low Bus.
   - **MTR Motor Controller**: Connect MTR STM32 CAN_H and CAN_L to Low Bus.
4. **Termination**:
   - Ensure a **$120\ \Omega$ termination resistor** is connected across `CAN_H` and `CAN_L` at each physical end of the bus (total bus resistance across CAN_H and CAN_L with power off should measure approximately **$60\ \Omega$**).
