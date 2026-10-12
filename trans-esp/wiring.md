### System Overview

* **MCU Board:** ESP32-S3 DevKit (DevKitC-1 in socket or headers)
* **Module 1 (Low CAN):** WCMCU-230 (SN65HVD230 module) — **3.3V powered from ESP32-S3 onboard LDO**
* **Module 2 (High CAN):** MCP2515 SPI CAN module (with 8 MHz crystal & TJA1050 5V transceiver)
* **Connectors:** Screw Terminals + JST-XH 2.50mm / 2.54mm ports in parallel on both CAN buses
* **Power Input:** 2-Pin JST-XH Power Input (`5V_SYS` / `GND`) or USB
* **Auxiliary Header:** 4 general-purpose pins + Power + Ground (1×6 2.54 mm header)
* **Hardware & Diagrams:** Placed in `trans-esp/hardware/` and `trans-esp/hardware/diagrams/`

---

### Power Architecture

* **5V Supply:** 5V DC enters through `J6` (JST-XH 2-pin connector) or the ESP32-S3 USB port, supplying the `5V / VBUS` rail.
* **3.3V Generation:** The ESP32-S3's onboard high-current low-dropout (LDO) regulator steps 5V down to 3.3V.
* **3.3V Distribution:** Sourced directly from the ESP32-S3 DevKit's **`3V3`** pin:
  * Supplies `WCMCU-230` Low-CAN transceiver VCC (strictly 3.3V only, cannot tolerate 5V).
  * Supplies `J3` Auxiliary Header Pin 1 (`3V3`) to power sensors, buttons, or auxiliary peripherals.

---

### 1. Module 1: WCMCU-230 Transceiver Module (Low-CAN)

This 4-pin breakout module connects directly to the ESP32-S3 built-in TWAI controller:

| WCMCU-230 Pin | ESP32-S3 DevKit Pin | Function / Notes |
|:---|:---:|:---|
| **VCC / 3V3** | **3V3** | **Sourced from ESP32-S3 onboard 3.3V regulator** (strictly $\le 3.3\,\text{V}$) |
| **GND** | **GND** | Common system ground |
| **CTX / TX** | **GPIO 5** | TWAI Transmit line from ESP32-S3 |
| **CRX / RX** | **GPIO 4** | TWAI Receive line to ESP32-S3 |

* **Bus Screw Terminal (J2) & JST-XH Port (J5) in Parallel:**
  * `CAN_H` $\longrightarrow$ Low-CAN Bus Backbone (J2 Pin 1 / J5 Pin 1)
  * `CAN_L` $\longrightarrow$ Low-CAN Bus Backbone (J2 Pin 2 / J5 Pin 2)
  * `GND` $\longrightarrow$ Shield / Common Ground (J2 Pin 3)
  * *Termination jumper on module:* **ON (Short / Closed)** (120 $\Omega$ populated)

---

### 2. Module 2: MCP2515 SPI CAN Module (High-CAN)

This 7-pin SPI breakout module handles the second CAN bus:

| MCP2515 Module Pin | ESP32-S3 DevKit Pin | Function / Notes |
|:---|:---:|:---|
| **VCC** | **5V / VBUS** | Powers onboard TJA1050 5V transceiver |
| **GND** | **GND** | Common ground |
| **SCK** | **GPIO 15** | SPI Clock |
| **SI (MOSI)** | **GPIO 16** | SPI Data (MCU $\rightarrow$ Module) |
| **SO (MISO)** | **GPIO 17** | SPI Data via 5V $\rightarrow$ 3.3V divider ($R_1 = 1\,\text{k}\Omega$, $R_2 = 2\,\text{k}\Omega$) |
| **CS** | **GPIO 18** | Active-low Chip Select |
| **INT** | **GPIO 47** | Active-low Interrupt via 5V $\rightarrow$ 3.3V divider ($R_3 = 1\,\text{k}\Omega$, $R_4 = 2\,\text{k}\Omega$) |

* **Bus Screw Terminal (J1) & JST-XH Port (J4) in Parallel:**
  * `CAN_H` $\longrightarrow$ High-CAN Bus Backbone (J1 Pin 1 / J4 Pin 1)
  * `CAN_L` $\longrightarrow$ High-CAN Bus Backbone (J1 Pin 2 / J4 Pin 2)
  * `GND` $\longrightarrow$ Shield / Common Ground (J1 Pin 3)
  * *Termination jumper on module:* **ON** (120 $\Omega$ populated)

> [!WARNING]
> **5V Module Logic Level Safety:**  
> The MCP2515 module is powered from 5V to run its onboard TJA1050 transceiver. While ESP32-S3 3.3V outputs (`SCK`, `SI`, `CS`) drive into the module safely, the module's 5V outputs (**`SO`** and **`INT`**) are passed through resistor dividers ($1\,\text{k}\Omega$ series, $2\,\text{k}\Omega$ to GND) to guarantee signal levels $\le 3.3\,\text{V}$ into the ESP32-S3.

---

### 3. The 4 Extra Auxiliary Pins (Header J3)

Break these out to a standard **1×6 header (0.1" / 2.54 mm)** for buttons, sensors, or auxiliary devices:

| Header Pin | Signal Name | ESP32-S3 Connection | Function / Capabilities |
|:---:|:---|:---:|:---|
| **1** | **3V3** | **3V3 (ESP32-S3 Reg Out)** | Clean logic power for sensors |
| **2** | **GND** | **GND Rail** | Common ground |
| **3** | **AUX 1** | **GPIO 1** | Digital I/O, ADC, or PWM |
| **4** | **AUX 2** | **GPIO 2** | Digital I/O, ADC, or PWM |
| **5** | **AUX 3** | **GPIO 7** | Digital I/O (or I²C SCL) |
| **6** | **AUX 4** | **GPIO 8** | Digital I/O (or I²C SDA) |

---

### 4. Hardware Connectors Summary

| Reference | Connector Type | Pins | Signal Connections | Description |
|:---:|:---|:---:|:---|:---|
| **J1** | Screw Terminal 3.81/5.08 mm | 3 | Pin 1: HIGH_CAN_H, Pin 2: HIGH_CAN_L, Pin 3: GND | High-CAN Bus Terminal (Jetson/Autoware) |
| **J4** | JST-XH 2.50/2.54 mm | 2 | Pin 1: HIGH_CAN_H, Pin 2: HIGH_CAN_L | High-CAN JST Port (parallel with J1) |
| **J2** | Screw Terminal 3.81/5.08 mm | 3 | Pin 1: LOW_CAN_H, Pin 2: LOW_CAN_L, Pin 3: GND | Low-CAN Bus Terminal (Actuators) |
| **J5** | JST-XH 2.50/2.54 mm | 2 | Pin 1: LOW_CAN_H, Pin 2: LOW_CAN_L | Low-CAN JST Port (parallel with J2) |
| **J6** | JST-XH 2.50/2.54 mm | 2 | Pin 1: +5V, Pin 2: GND | System 5V DC Power Input |
| **J3** | Pin Header 1×6 (2.54 mm) | 6 | Pin 1: 3V3, Pin 2: GND, Pin 3-6: AUX1..AUX4 | Auxiliary Expansion Header |

---

### 5. Summary Wiring Diagram

```text
 ┌────────────────────────────────────────────────────────┐
 │                   ESP32-S3 DevKit                      │
 │                                                        │
 │  [GPIO 4]  [GPIO 5]  [3V3 OUT]  [GND]                  │
 └─────┬─────────┬──────────┬────────┬────────────────────┘
       │         │          │        │
       ▼         ▼          ▼        ▼
    ┌───────────────────────────────────┐
    │       WCMCU-230 Module (Low)      │───► [Low-CAN Screw Terminal (J2)]
    │   CRX     CTX    3V3 (EXT)   GND  │───► [Low-CAN JST-XH 2-Pin Port (J5)]
    └───────────────────────────────────┘

 ┌────────────────────────────────────────────────────────┐
 │                   ESP32-S3 DevKit                      │
 │                                                        │
 │  [GPIO 15] [GPIO 16] [GPIO 17] [GPIO 18] [GPIO 47] [5V]│
 └─────┬──────────┬──────────┬─────────┬─────────┬───────┬┘
       │          │          │         │         │       │
       ▼          ▼          ▼         ▼         ▼       ▼
    ┌────────────────────────────────────────────────────┐
    │               MCP2515 Module (High)                │───► [High-CAN Screw Terminal (J1)]
    │   SCK       SI         SO        CS       INT  VCC │───► [High-CAN JST-XH 2-Pin Port (J4)]
    │                   (via divider)       (via divider)│
    └────────────────────────────────────────────────────┘

 ┌────────────────────────────────────────────────────────┐
 │                   ESP32-S3 DevKit                      │
 │                                                        │
 │  [GPIO 1]  [GPIO 2]  [GPIO 7]  [GPIO 8]  [3V3]   [GND] │
 └─────┬──────────┬─────────┬─────────┬───────┬───────┬───┘
       │          │         │         │       │       │
       ▼          ▼         ▼         ▼       ▼       ▼
    ┌────────────────────────────────────────────────────┐
    │             1×6 Auxiliary Header (J3)              │
    │  AUX1      AUX2      AUX3      AUX4    3V3     GND │
    └────────────────────────────────────────────────────┘
```
