# DUMB-ESP Hardware Wiring Specification (Classic ESP32)

Standalone bench wiring reference for **DUMB-ESP** on **Classic ESP32 (`esp32dev`)**, interfacing two CAN modules (**MCP2515** and **WCMCU-230**), 4 pilot switches, power rails, and **two JST CAN connectors**.

---

## 1. Classic ESP32 (`esp32dev`) to MCP2515 Module (High CAN)

| ESP32 Pin (`esp32dev`) | Board Silkscreen | MCP2515 Pin | Signal Name | Direction | Function \& Wiring Notes |
|:---|:---:|:---:|:---|:---:|:---|
| **GPIO 18** | **`D18`** | **`SCK`** | `SPI_SCK` | Output | 8 MHz Hardware VSPI Clock |
| **GPIO 23** | **`D23`** | **`SI`** (MOSI) | `SPI_MOSI` | Output | Master-Out-Slave-In data line |
| **GPIO 19** | **`D19`** | **`SO`** (MISO) | `SPI_MISO` | Input | Master-In-Slave-Out data line |
| **GPIO 21** | **`D21`** | **`CS`** | `SPI_CS` | Output | Active-LOW Hardware Chip Select (`SPI3_HOST`) |
| **GPIO 22** | **`D22`** | **`INT`** | `MCP_INT` | Input | Active-LOW Interrupt, Level-3 ISR, internal pull-up |
| **VIN / 5V** | **`VIN`** / **`5V`** | **`VCC`** | `VCC_5V` | Power | **5.0V DC Rail** (Powers onboard TJA1050 transceiver) |
| **GND** | **`GND`** | **`GND`** | `GND` | Ground | Common system logic ground |
| — | — | **`J1` Jumper** | `TERM` | Passive | $120\ \Omega$ termination jumper across bus |

> **Crystal Note**: Driver code defaults to **8 MHz crystal** (`CNF1=0x00`). If using a **16 MHz crystal** module, compile with `-D MCP2515_16MHZ` (`CNF1=0x01`).

---

## 2. Classic ESP32 (`esp32dev`) to WCMCU-230 Module (Low CAN)

| ESP32 Pin (`esp32dev`) | Board Silkscreen | WCMCU-230 Pin | Signal Name | Direction | Function \& Wiring Notes |
|:---|:---:|:---:|:---|:---:|:---|
| **GPIO 5** | **`D5`** | **`CTX`** (or `TX`) | `TWAI_TX` | Output | Internal TWAI CAN Transmit (500 kbps) |
| **GPIO 4** | **`D4`** | **`CRX`** (or `RX`) | `TWAI_RX` | Input | Internal TWAI CAN Receive (500 kbps) |
| **3V3** | **`3V3`** | **`3V3`** (or `VCC`) | `VCC_3V3` | Power | **Strictly 3.3V DC** from ESP32 (**Never connect 5V**) |
| **GND** | **`GND`** | **`GND`** | `GND` | Ground | Common system logic ground |
| **GND** | **`GND`** | **`RS`** | `MODE` | Input | Tie to `GND` (pull LOW) for High-Speed CAN mode |
| — | — | **`R2` (120Ω)** | `TERM` | Passive | Onboard $120\ \Omega$ SMD resistor across CANH & CANL |

---

## 3. Classic ESP32 (`esp32dev`) to 4 Pilot Switches

All switches connect between the respective GPIO pin and **Common `GND`** using ESP32 internal pull-up resistors:

| ESP32 Pin (`esp32dev`) | Board Silkscreen | Switch Function | Switch Contact Type | External Connection | Active Logic State | Safety / Fail-Safe Behavior |
|:---|:---:|:---|:---:|:---|:---:|:---|
| **GPIO 27** | **`D27`** | **ESTOP** (Emergency Stop) | **NC** (Normally Closed) Mushroom | Switch $\longleftrightarrow$ **`GND`** | **HIGH (Open = Cut / Pressed)** | **Fail-Safe**: Cut wire or pressed button opens to 3.3V, instantly tripping ESTOP. |
| **GPIO 26** | **`D26`** | **START / RUN** (Enable) | **NO** (Normally Open) Button | Switch $\longleftrightarrow$ **`GND`** | **LOW (Closed to GND)** | Pressing connects pin to GND to enable run loop. |
| **GPIO 25** | **`D25`** | **MODE** (Manual $\leftrightarrow$ Auto) | **NO** Momentary Pushbutton | Switch $\longleftrightarrow$ **`GND`** | **LOW (Closed to GND)** | Pressing shorts to GND to toggle Manual / Auto. |
| **GPIO 14** | **`D14`** | **RESET / AUX** (Fault Reset) | **NO** Momentary Pushbutton | Switch $\longleftrightarrow$ **`GND`** | **LOW (Closed to GND)** | Pressing shorts to GND to clear latched blockers. |

---

## 4. Power Distribution & Ground Rails

| Power Rail | Nominal Voltage | Source | Connected Device Pins | Max Current | Wiring & Isolation Rules |
|:---|:---:|:---|:---|:---:|:---|
| **`5V_SYS`** | **5.0V DC** ($\pm 5\%$) | Regulated 5V Buck / USB | ESP32 **`VIN`**, MCP2515 **`VCC`** | ~250 mA peak | Main supply; powers ESP32 onboard LDO and MCP2515 5V transceiver |
| **`3V3_SYS`** | **3.3V DC** ($\pm 2\%$) | ESP32 Onboard 3.3V LDO | WCMCU-230 **`3V3`** | ~25 mA peak | **Dedicated 3.3V supply**. Never connect 5V to WCMCU-230 |
| **`GND_LOGIC`** | **0.0V Ref** | System DC Supply Ground | ESP32 `GND`, MCP2515 `GND`, WCMCU `GND`, Switch Grounds | Shared Return | Common star-point logic ground reference |

---

## 5. CAN Bus JST Connector Pinouts (Two 2-Pin JST Ports)

Two dedicated **2-pin JST connectors** (e.g. JST-XH 2.54mm or JST-PH 2.0mm) export the two differential CAN pairs:

### JST Connector 1: High CAN Bus (from MCP2515 Module)
| JST Pin | Net Name | Connected Origin Pin | Wire Color Code | Signal Description |
|:---:|:---|:---|:---:|:---|
| **Pin 1** | **`HIGH_CAN_H`** | MCP2515 Module **`CAN_H`** | Yellow | High CAN Differential High line (500 kbps) |
| **Pin 2** | **`HIGH_CAN_L`** | MCP2515 Module **`CAN_L`** | Green | High CAN Differential Low line (500 kbps) |

### JST Connector 2: Low CAN Bus (from WCMCU-230 Module)
| JST Pin | Net Name | Connected Origin Pin | Wire Color Code | Signal Description |
|:---:|:---|:---|:---:|:---|
| **Pin 1** | **`LOW_CAN_H`** | WCMCU-230 Module **`CANH`** | Yellow | Low CAN Differential High line (500 kbps) |
| **Pin 2** | **`LOW_CAN_L`** | WCMCU-230 Module **`CANL`** | Green | Low CAN Differential Low line (500 kbps) |
