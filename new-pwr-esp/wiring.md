# new-pwr-esp Hardware Wiring Specification

Dedicated DCDC Power Supply Keepalive node using **ESP32-WROOM-32UE** and **WCMCU-230** (SN65HVD230) CAN transceiver at **500 kbps**.

---

## 1. ESP32-WROOM-32UE to WCMCU-230 Module

Using the **printed TX and RX header pins** on the ESP32 board:

| ESP32 Pin | Silkscreen Label | WCMCU-230 Pin | Direction | Signal Function | Notes |
|:---|:---:|:---:|:---:|:---|:---|
| **GPIO 1** | **`TX`** (or `TX0`) | **`CTX`** (or `TX`) | Output | CAN Transmit | ESP-IDF TWAI TX output to CAN transceiver |
| **GPIO 3** | **`RX`** (or `RX0`) | **`CRX`** (or `RX`) | Input | CAN Receive | ESP-IDF TWAI RX input from CAN transceiver |
| **3V3** | **`3V3`** | **`3V3`** (or `VCC`) | Power | 3.3V DC Rail | Powers the 3.3V SN65HVD230 transceiver |
| **GND** | **`GND`** | **`GND`** | Ground | Logic Ground | Common ground reference |
| — | — | **`RS`** | Input | Mode Pin | Tie to `GND` for high-speed CAN mode (or leave floating if grounded onboard) |

> **Important**: UART0 console output is automatically disabled in `sdkconfig.defaults` and firmware so that UART serial prints do not interfere with the CAN transceiver on GPIO 1 (`TX`) and GPIO 3 (`RX`).

---

## 2. WCMCU-230 to Vehicle / DCDC CAN Bus

| WCMCU-230 Pin | Wire / Terminal | Bus Destination | Notes |
|:---:|:---:|:---|:---|
| **`CANH`** | CAN High | DCDC Power Supply CAN_H | Differential CAN High line |
| **`CANL`** | CAN Low | DCDC Power Supply CAN_L | Differential CAN Low line |
| **`GND`** | Bus Ground | Common Vehicle Ground | Ensures common-mode noise immunity |

### Termination Resistor
- The WCMCU-230 board typically has a $120\ \Omega$ terminating resistor across CANH and CANL (often with a jumper or solder pads labeled `R2` / `120R`).
- If this node is at the physical end of the CAN bus trunk, ensure the $120\ \Omega$ termination is present/enabled.
- If it connects into an already terminated vehicle CAN bus, disable or remove redundant termination.

---

## 3. Emitted CAN Frame

- **Cycle Rate:** 10 Hz / 100 ms
- **Bitrate:** **500 kbps**
- **CAN ID:** `0x10262B27` (29-bit Extended Frame)
- **DLC:** 8
- **Payload:** `[0xFD, 0x78, 0x00, 0xC8, 0x00, 0xFF, 0xFF, 0xFC]`
- **Status LED:** Onboard LED (GPIO 2) toggles on each 100 ms transmission to indicate heartbeat activity.
