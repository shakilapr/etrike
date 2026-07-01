# E-Trike Complete Wiring Reference

Pin-to-pin wiring for all five ECUs, three CAN buses, power distribution, and every peripheral. Derived from `config.h` files, `architecture.md`, `can-bench-test.md`, and firmware source.

**Vehicle:** Autonomous electric tricycle, distributed drive-by-wire
**ECUs:** RT ESP32-S3 · SYS ESP32-S3 · MTR STM32 · PWT ESP32-S3 · Jetson Orin (Host)
**CAN Buses:** High (500 kbit/s) · Low (500 kbit/s) · Powertrain (250 kbit/s)
**Voltage Domains:** 72 V traction · 12 V accessory · 5 V DAC · 3.3 V logic

---

## 1. System Topology

```
                         ┌── HIGH CAN BUS (500 kbit/s) ──┐
                         │                                │
    ┌────────────────────┼──────┐          ┌──────────────┴──────────┐
    │  Jetson Orin NX    │      │          │    RT ESP32-S3           │
    │  (Host autonomy)   │      │          │                          │
    │                    │      │          │  MCP2515 (SPI) ── High   │
    │  CAN: built-in     │      │          │  TWAI (GPIO 5/4) ── Low  │
    └────────────────────┘      │          │  Gateway: High ↔ Low     │
                                │          └──────────┬───────────────┘
                                │                     │
    ┌───────────────────────────┼── LOW CAN BUS (500 kbit/s) ────────────────────────────┐
    │                           │                     │                                   │
    │  ┌──────────────┐  ┌──────┴──────┐  ┌──────────┴───────────┐  ┌─────────────────┐  │
    │  │ SYS ESP32-S3 │  │ MTR STM32   │  │   PWT ESP32-S3       │  │   Actuators     │  │
    │  │              │  │             │  │                       │  │                 │  │
    │  │ TWAI:5/4     │  │ CAN: TBD    │  │ TWAI0: 5/4 (low)     │  │ EPS-C (steer)   │  │
    │  │ Safety L2    │  │ EGAS L1     │  │ TWAI1: 7/6 (pwt)     │  │ SEB   (brake)   │  │
    │  │ Body control │  │ Throttle    │  │ Gateway: Low ↔ Pwt   │  │ DC-DC (72→12V)  │  │
    │  └──────────────┘  │ Gear        │  └──────────┬───────────┘  └─────────────────┘  │
    │                    └─────────────┘             │                                   │
    └────────────────────────────────────────────────┼───────────────────────────────────┘
                                                     │
                         ┌── POWERTRAIN CAN BUS (250 kbit/s) ──┐
                         │         │                           │
                         │  ┌──────┴──────┐    ┌───────────────┴──────┐
                         │  │ PWT ESP32-S3│    │  Motor Controller     │
                         │  │ TWAI1: 7/6  │    │  (telemetry via CAN)  │
                         │  └─────────────┘    │  DC-DC Converter      │
                         │                     │  (0x012 enable cmd)   │
                         │                     └──────────────────────┘
```

- **High bus:** Jetson ↔ RT MCP2515. 120 Ω at both ends.
- **Low bus:** RT TWAI ↔ SYS TWAI ↔ MTR ↔ PWT TWAI0 ↔ EPS-C ↔ SEB ↔ DC-DC. 120 Ω at both physical ends.
- **Powertrain bus:** PWT TWAI1 ↔ DC-DC ↔ Motor Controller. 120 Ω at both ends.

---

## 2. CAN Bus Wiring — Physical Layer

### 2.1 Bus Specifications

| Parameter | High Bus | Low Bus | Powertrain Bus |
|-----------|----------|---------|----------------|
| Bitrate | 500 kbit/s | 500 kbit/s | 250 kbit/s |
| Max nodes | 7 | 8 | 4 |
| Termination | 120 Ω × 2 ends | 120 Ω × 2 ends | 120 Ω × 2 ends |
| Wire | Twisted pair, 22 AWG | Twisted pair, 22 AWG | Twisted pair, 22 AWG |
| CAN_H color | Orange (or yellow) | Yellow | Blue (or yellow) |
| CAN_L color | Blue (or green) | Green | White (or green) |
| GND color | Gray (or black) | Black | Black |
| Idle voltage (H/L) | ~2.5 V / ~2.5 V | ~2.5 V / ~2.5 V | ~2.5 V / ~2.5 V |
| Dominant (H/L) | ~3.5 V / ~1.5 V | ~3.5 V / ~1.5 V | ~3.5 V / ~1.5 V |

### 2.2 Termination Rules

| Rule | Detail |
|------|--------|
| Resistor value | 120 Ω between CAN_H and CAN_L |
| Placement | One at each **physical end** of the bus (the two nodes with the longest cable run between them) |
| Middle taps | Never terminate — termination only at furthest-apart nodes |
| Total bus impedance | Two 120 Ω in parallel = **~60 Ω** (measure with power off) |
| WCMCU-230 module | Onboard 120 Ω — enable via solder jumper / shunt / DIP switch |
| MCP2515 module | No onboard termination — add external if needed or rely on CANalyst-II Ch1 |
| CANalyst-II | Software-controlled termination per channel |

### 2.3 Bus Ground Reference

Every node on a CAN bus must share a common ground reference. Run a dedicated GND wire in the CAN backbone alongside CAN_H and CAN_L. Without it, ground offset between nodes causes random CRC errors that look like bus noise.

### 2.4 CAN Transceiver Module — WCMCU-230 (SN65HVD230)

The standard CAN transceiver module used by RT, SYS, and PWT. Contains a TI SN65HVD230 3.3 V CAN transceiver.

| Label on Module | Connects To | Notes |
|----------------|-------------|-------|
| VCC / 3V3 | ESP32 3.3 V pin | **Must be 3.3 V, not 5 V** |
| GND | ESP32 GND pin | Common ground |
| CTX / TX / TXD / D | ESP32 TWAI TX GPIO | From ESP32 to transceiver |
| CRX / RX / RXD / R | ESP32 TWAI RX GPIO | From transceiver to ESP32 |
| CAN_H | Bus CAN_H (screw terminal) | Differential high |
| CAN_L | Bus CAN_L (screw terminal) | Differential low |

- **4 wires needed:** VCC, GND, CTX (→ TX), CRX (→ RX)
- **RS pin:** Pulled to GND by onboard 10 kΩ — locked in high-speed mode. No external connection needed.
- **120 Ω terminator:** Onboard resistor controlled by solder jumper or DIP switch
- **Current draw:** ~30 mA at 3.3 V
- **Counterfeit warning:** Some modules ship with fake SN65HVD230 chips that receive but cannot transmit. If one node's frames never appear, swap modules between boards — if the problem follows the module, replace it.

### 2.5 CAN Controller Module — MCP2515 SPI (MCP2515 + TJA1050)

Used by RT for the high bus. Standalone SPI-to-CAN controller.

| Label on Module | Connects To | Notes |
|----------------|-------------|-------|
| VCC | **5 V** from ESP32 dev board | TJA1050 transceiver needs 5 V for proper CAN levels |
| GND | ESP32 GND | Common ground |
| SCK | ESP32 GPIO36 | SPI clock, up to 10 MHz |
| SI (MOSI) | ESP32 GPIO37 | SPI data: MCU → MCP2515 |
| SO (MISO) | ESP32 GPIO38 | SPI data: MCP2515 → MCU |
| CS | ESP32 GPIO39 | Chip select, active low |
| INT | ESP32 GPIO40 | Interrupt: RX buffer ready, active low |
| CAN_H | Bus CAN_H (screw terminal) | High bus backbone |
| CAN_L | Bus CAN_L (screw terminal) | High bus backbone |

- **SPI Mode:** 0 or 1 (CPOL=0, CPHA=0 or CPOL=1, CPHA=1)
- **Clock:** 8 MHz safe default, 10 MHz max
- **Crystal:** Either 8 MHz or 16 MHz — check laser etching. BRP must match.
- **5 V power required** — the TJA1050 transceiver produces marginal CAN levels at 3.3 V
- **No onboard termination** — rely on CANalyst-II Ch1 or add external 120 Ω

### 2.6 CANalyst-II USB Analyzer

Dual-channel passive CAN monitor/injector.

| Channel | Typical Bus | Termination | Notes |
|---------|------------|-------------|-------|
| Ch0 | Low bus | OFF (RT + SYS provide 60 Ω) | Middle tap |
| Ch1 | High bus | ON (sole terminator for high bus) | Endpoint |

- **Driver:** WinUSB via Zadig (see `debug-tool/CANALYST-II-SETUP.md`)
- **Mode:** Listen-only — never ACKs or participates in arbitration
- **Connectors:** Two 3-pin green pluggable terminal blocks (H, L, G per channel)

---

## 3. RT ESP32-S3 — CAN Gateway & Real-Time Controller

**Role:** CAN gateway (high ↔ low), real-time kinematics, steering state machine, PID speed control
**Board:** ESP32-S3-DevKitC-1
**CAN modules:** 2 — TWAI (built-in, low bus) + MCP2515 (SPI, high bus)

### 3.1 RT — CAN Module Wiring

#### Module A: WCMCU-230 (Low Bus via TWAI)

| GPIO | J1 Pin | Signal | Connected To | Wire |
|------|--------|--------|-------------|------|
| — | 1 (3V3) | 3.3 V power | WCMCU-230 VCC | Dupont F-F, red |
| — | 22 (GND) | Ground | WCMCU-230 GND | Dupont F-F, black |
| 5 | 5 | CAN TX (TWAI) | WCMCU-230 CTX | Dupont F-F, blue |
| 4 | 4 | CAN RX (TWAI) | WCMCU-230 CRX | Dupont F-F, green |
| — | — | CAN_H | Low bus backbone | 22 AWG, yellow |
| — | — | CAN_L | Low bus backbone | 22 AWG, green |
| — | — | GND | Low bus backbone | 22 AWG, black |

- 120 Ω terminator: **ON** (RT is a bus endpoint on the low bus)
- Source: `rt::kCanLowTxGpio=5`, `rt::kCanLowRxGpio=4`, bitrate 500 kbit/s

#### Module B: MCP2515 SPI (High Bus)

| GPIO | Signal | Connected To | Wire |
|------|--------|-------------|------|
| 36 | SPI SCK | MCP2515 SCK | Dupont F-F |
| 37 | SPI MOSI | MCP2515 SI | Dupont F-F |
| 38 | SPI MISO | MCP2515 SO | Dupont F-F |
| 39 | SPI CS | MCP2515 CS | Dupont F-F |
| 40 | INT | MCP2515 INT | Dupont F-F |
| — (J1-21, 5V) | 5 V power | MCP2515 VCC | Dupont F-F, red |
| — (J1-22, GND) | Ground | MCP2515 GND | Dupont F-F, black |
| — | CAN_H | High bus backbone | 22 AWG, orange |
| — | CAN_L | High bus backbone | 22 AWG, blue |

- No onboard termination — CANalyst-II Ch1 provides 120 Ω for the high bus
- Source: `rt::kSpiSckGpio=36`, `rt::kSpiMosiGpio=37`, `rt::kSpiMisoGpio=38`, `rt::kSpiCsGpio=39`, `rt::kMcpIntGpio=40`, bitrate 500 kbit/s

### 3.2 RT — Inputs

| # | Signal | GPIO | Type | Connected To | Status | Notes |
|---|--------|------|------|-------------|--------|-------|
| 1 | CAN RX (low bus) | 4 | TWAI RX | SN65HVD230 CRX | **Active** | Low bus frames from SYS, MTR, EPS-C, SEB |
| 2 | MCP2515 MISO | 38 | SPI | MCP2515 SO | **Active** | High bus RX data (Host commands) |
| 3 | MCP2515 INT | 40 | Digital, falling | MCP2515 INT | **Active** | RX buffer ready interrupt |
| 4 | Encoder A (rear motor) | 1 | PCNT quadrature | Motor encoder | **Active** | Speed feedback |
| 5 | Encoder B (rear motor) | 2 | PCNT quadrature | Motor encoder | **Active** | Quadrature phase B |
| 6 | Encoder A (front wheel) | 3 | PCNT quadrature | TBD sensor | Compile-disabled | `CONFIG_ENABLE_ENCODERS` off |
| 7 | Encoder B (front wheel) | 6 | PCNT quadrature | TBD sensor | Compile-disabled | `CONFIG_ENABLE_ENCODERS` off |
| 8 | Encoder A (rear left) | 9 | PCNT quadrature | TBD sensor | Compile-disabled | Differential speed |
| 9 | Encoder B (rear left) | 12 | PCNT quadrature | TBD sensor | Compile-disabled | Differential speed |
| 10 | Encoder A (rear right) | 13 | PCNT quadrature | TBD sensor | Compile-disabled | Differential speed |
| 11 | Encoder B (rear right) | 14 | PCNT quadrature | TBD sensor | Compile-disabled | Differential speed |
| 12 | I2C SDA (IMU) | 10 | I2C | Optional IMU | Optional | Pull-up to 3.3 V |
| 13 | I2C SCL (IMU) | 11 | I2C | Optional IMU | Optional | — |

### 3.3 RT — Outputs

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN TX (low bus) | 5 | TWAI TX | SN65HVD230 CTX | 0x204, 0x205, 0x169, 0x001, 0x302, 0x7FD on low bus |
| 2 | SPI SCK | 36 | SPI out | MCP2515 SCK | 10 MHz max |
| 3 | SPI MOSI | 37 | SPI out | MCP2515 SI | High bus TX data |
| 4 | SPI CS | 39 | Digital out | MCP2515 CS | Active low |
| 5 | WDT Toggle | 21 | Digital out | TPS3850 WDI pin | Toggled at 100 Hz by control_task |

### 3.4 RT — GPIO Quick Reference

```
GPIO 1  : Encoder A — rear motor (active)
GPIO 2  : Encoder B — rear motor (active)
GPIO 3  : Encoder A — front wheel (TBD, disabled)
GPIO 4  : CAN RX — low bus TWAI
GPIO 5  : CAN TX — low bus TWAI
GPIO 6  : Encoder B — front wheel (TBD, disabled)
GPIO 7  : (unused)
GPIO 8  : (unused)
GPIO 9  : Encoder A — rear left (TBD, disabled)
GPIO 10 : I2C SDA — IMU (optional)
GPIO 11 : I2C SCL — IMU (optional)
GPIO 12 : Encoder B — rear left (TBD, disabled)
GPIO 13 : Encoder A — rear right (TBD, disabled)
GPIO 14 : Encoder B — rear right (TBD, disabled)
GPIO 15–20: (unused)
GPIO 21 : WDT toggle → TPS3850 WDI
GPIO 22–35: (unused)
GPIO 36 : SPI SCK → MCP2515
GPIO 37 : SPI MOSI → MCP2515
GPIO 38 : SPI MISO ← MCP2515
GPIO 39 : SPI CS → MCP2515
GPIO 40 : INT ← MCP2515
```

### 3.5 RT — Power

| Rail | Source | Used For |
|------|--------|----------|
| 3.3 V | USB or dev board regulator | ESP32-S3, WCMCU-230 transceiver |
| 5 V | USB or dev board regulator (J1-21) | MCP2515 module (TJA1050 transceiver needs 5 V) |
| GND | USB or dev board | Common ground — shared with both CAN modules |

---

## 4. SYS ESP32-S3 — Safety & Body Controller

**Role:** Safety monitor (EGAS Level 2), body control (lights, gear, indicators), throttle pass-through (MANUAL mode), brake-by-wire interface
**Board:** ESP32-S3-DevKitC-1
**CAN modules:** 1 — TWAI (built-in, low bus only)

### 4.1 SYS — CAN Module Wiring

#### Module: WCMCU-230 (Low Bus via TWAI)

| GPIO | J1 Pin | Signal | Connected To | Wire |
|------|--------|--------|-------------|------|
| — | 1 (3V3) | 3.3 V power | WCMCU-230 VCC | Dupont F-F, red |
| — | 22 (GND) | Ground | WCMCU-230 GND | Dupont F-F, black |
| 5 | 5 | CAN TX (TWAI) | WCMCU-230 CTX | Dupont F-F, blue |
| 4 | 4 | CAN RX (TWAI) | WCMCU-230 CRX | Dupont F-F, green |
| — | — | CAN_H | Low bus backbone | 22 AWG, yellow |
| — | — | CAN_L | Low bus backbone | 22 AWG, green |
| — | — | GND | Low bus backbone | 22 AWG, black |

- 120 Ω terminator: **ON** (SYS is a bus endpoint on the low bus)
- Source: `sys::kCanTxGpio=5`, `sys::kCanRxGpio=4`, bitrate 500 kbit/s

### 4.2 SYS — Safety Inputs

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | ESTOP Button | 1 | Digital, NC, active-low | Red mushroom button → GND | 10k pull-up to 3.3 V. LOW = ESTOP. Shared with MTR STM32 (separate MCU, same button). |
| 2 | Brake Lever | 2 | Digital, NO, active-low | Lever switch → GND | Internal pull-up. LOW = pressed → SEB via CAN 0x7B9 |
| 3 | START Button | 32 | Digital, NO, active-low | Green momentary → GND | Internal pull-up. Press = ignition ON, hold 3s = OFF |
| 4 | MODE Button | 11 | Digital, NO, active-low | Momentary → GND | Internal pull-up. Toggles MANUAL ↔ AUTO. Long-press 3s exits ESTOP. |
| 5 | Ignition Relay | 8 | Digital out | 12V relay coil driver | HIGH = vehicle ON, LOW = all ECUs dead. Dual-path with CAN 0x012. |
| 6 | Throttle ADC | 10 (ADC1_CH5) | Analog (12-bit) | 0–5V grip via voltage divider | 5V → 3.3V resistive divider. Dead zone: 200 counts. Max: 3000 mm/s. |
| 7 | CAN RX (low bus) | 4 | TWAI RX | SN65HVD230 CRX | RT commands, MTR feedback (0x206), SEB feedback (0x721) |

### 4.3 SYS — Throttle / Gear I/O (currently on SYS, migrating to MTR per gap #5)

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | MCP4725 DAC SDA | 15 | I2C data | MCP4725 DAC | I2C addr 0x60, VCC = 5 V. Output: 0–5 V → motor controller throttle |
| 2 | MCP4725 DAC SCL | 16 | I2C clock | MCP4725 DAC | 100 kHz standard mode |
| 3 | Gear D Sense | 12 | Digital, active-low | TLP281 opto ch1 output | 72V gear line → optoisolator → GPIO. 10k pull-up to 3.3V. |
| 4 | Gear S Sense | 13 | Digital, active-low | TLP281 opto ch2 output | Same circuit as D Sense |
| 5 | Gear R Sense | 14 | Digital, active-low | TLP281 opto ch3 output | Same circuit as D Sense |
| 6 | Gear D Out | 33 | Digital out | 4-ch relay module IN1 | HIGH = relay energized → 72V passed to ECU Gear D |
| 7 | Gear S Out | 34 | Digital out | 4-ch relay module IN2 | HIGH = relay energized → 72V passed to ECU Gear S |
| 8 | Gear R Out | 35 | Digital out | 4-ch relay module IN3 | HIGH = relay energized → 72V passed to ECU Gear R |

### 4.4 SYS — Signal Lights & Switches

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | Left Turn Switch | 3 | Digital, NO, active-low | Handlebar momentary → GND | Internal pull-up |
| 2 | Right Turn Switch | 6 | Digital, NO, active-low | Handlebar momentary → GND | Internal pull-up |
| 3 | Headlight Switch | 7 | Digital, NO, active-low | Handlebar toggle → GND | Internal pull-up |
| 4 | Left Turn Lamp | 18 | Digital out | Relay coil → 12 V | Relay COM = 12 V, NO = lamp |
| 5 | Right Turn Lamp | 19 | Digital out | Relay coil → 12 V | Relay COM = 12 V, NO = lamp |
| 6 | Brake Light | 21 | Digital out | Relay coil → 12 V | Relay COM = 12 V, NO = lamp |
| 7 | Headlight | 22 | Digital out | Relay coil → 12 V | Relay COM = 12 V, NO = lamp |

### 4.5 SYS — Indicators & Power Control

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | AUTO Mode Bulb | 25 | Digital out | Relay coil → 12 V | Relay COM = 12 V, NO = bulb |
| 2 | MANUAL Mode Bulb | 26 | Digital out | Relay coil → 12 V | Relay COM = 12 V, NO = bulb |
| 3 | READY Bulb | 17 | Digital out | Relay coil → 12 V | Green — system ready (AUTO/MANUAL, RT alive, no faults) |
| 4 | ESTOP Bulb | 20 | Digital out | Relay coil → 12 V | Red — dedicated ESTOP indicator |
| 5 | 12V Power Relay | 27 | Digital out | Relay coil → 12 V | Relay COM = 12 V bus, NO = accessories. Opens in ESTOP. |
| 6 | WDT Toggle | 23 | Digital out | TPS3850 WDI pin | Toggled at 20 Hz by safety_task |

### 4.6 SYS — GPIO Quick Reference

```
GPIO 1  : ESTOP button (NC, active-low) — shared with MTR STM32
GPIO 2  : Brake lever (active-low) → SEB via CAN 0x7B9
GPIO 3  : Left turn switch (active-low)
GPIO 4  : CAN RX — low bus TWAI
GPIO 5  : CAN TX — low bus TWAI
GPIO 6  : Right turn switch (active-low)
GPIO 7  : Headlight switch (active-low)
GPIO 8  : Ignition relay — HIGH = vehicle ON
GPIO 9  : (unused)
GPIO 10 : Throttle ADC (ADC1_CH5) — 0–5V via voltage divider
GPIO 11 : MODE button (active-low) — publishes CAN 0x110
GPIO 12 : Gear D sense — TLP281 opto ch1 input
GPIO 13 : Gear S sense — TLP281 opto ch2 input
GPIO 14 : Gear R sense — TLP281 opto ch3 input
GPIO 15 : MCP4725 DAC SDA (I2C addr 0x60)
GPIO 16 : MCP4725 DAC SCL
GPIO 17 : READY bulb (green) — relay output
GPIO 18 : Left turn lamp — relay output
GPIO 19 : Right turn lamp — relay output
GPIO 20 : ESTOP bulb (red) — relay output
GPIO 21 : Brake light — relay output
GPIO 22 : Headlight — relay output
GPIO 23 : WDT toggle → TPS3850 WDI (20 Hz)
GPIO 24 : (unused)
GPIO 25 : AUTO mode bulb — relay output
GPIO 26 : MANUAL mode bulb — relay output
GPIO 27 : 12V power relay — opens in ESTOP
GPIO 28–31: (unused)
GPIO 32 : START button (active-low)
GPIO 33 : Gear D relay out → 4-ch relay module IN1
GPIO 34 : Gear S relay out → 4-ch relay module IN2
GPIO 35 : Gear R relay out → 4-ch relay module IN3
```

### 4.7 SYS — Power

| Rail | Source | Used For |
|------|--------|----------|
| 3.3 V | USB or dev board regulator | ESP32-S3, WCMCU-230 transceiver |
| 5 V | USB or dev board regulator | MCP4725 DAC VCC (for 0–5 V output) |
| 12 V | DC-DC converter (via 12V relay) | Lamps, bulbs, relay coils |
| GND | USB or dev board | Common ground |

---

## 5. MTR STM32 — Motor Controller (EGAS Level 1)

**Role:** Dedicated throttle & gear actuation. EGAS Level 1 primary controller.
**Board:** STM32 (specific variant TBD)
**CAN modules:** 1 — CAN transceiver (low bus, pin assignment TBD)
**Pin encoding:** `(port_number × 16 + pin_number)`. PA0–PA15 = 0–15, PB0–PB15 = 16–31, PC0–PC15 = 32–47.

### 5.1 MTR — CAN Module Wiring

| Signal | Pin | Connected To | Notes |
|--------|-----|-------------|-------|
| CAN RX | TBD | CAN transceiver RXD | RT commands (0x204), SYS mode (0x110) |
| CAN TX | TBD | CAN transceiver TXD | Speed telemetry (0x120), motor feedback (0x206) |

### 5.2 MTR — Inputs

| # | Signal | Pin | Type | Connected To | Notes |
|---|--------|-----|------|-------------|-------|
| 1 | CAN RX (low bus) | TBD | CAN RX | CAN transceiver RXD | RT 0x204, SYS 0x110 |
| 2 | ESTOP Button | TBD | Digital, NC, active-low | Red mushroom → GND | Shared with SYS GPIO1. Hardwired Level 3 kill. |
| 3 | Throttle ADC | PA0 (ADC1_IN0) | Analog (12-bit) | 0–5V grip via voltage divider | 5V → 3.3V resistive divider |
| 4 | Gear D Sense | PB0 (encoded 16) | Digital, active-low | TLP281 opto ch1 output | 72V gear line → optoisolator → GPIO |
| 5 | Gear S Sense | PB1 (encoded 17) | Digital, active-low | TLP281 opto ch2 output | Same circuit |
| 6 | Gear R Sense | PB2 (encoded 18) | Digital, active-low | TLP281 opto ch3 output | Same circuit |

### 5.3 MTR — Outputs

| # | Signal | Pin | Type | Connected To | Notes |
|---|--------|-----|------|-------------|-------|
| 1 | CAN TX (low bus) | TBD | CAN TX | CAN transceiver TXD | 0x120 throttle (100 Hz), 0x206 feedback (50 Hz) |
| 2 | MCP4725 DAC SDA | PB7 | I2C data | MCP4725 DAC | I2C addr **0x61** (A0 tied to VCC). Separate I²C bus from SYS. |
| 3 | MCP4725 DAC SCL | PB6 | I2C clock | MCP4725 DAC | 100 kHz standard mode |
| 4 | Gear D Out | PA3 (encoded 3) | Digital out | 4-ch relay module IN1 | HIGH = energized → 72V to ECU Gear D |
| 5 | Gear S Out | PA4 (encoded 4) | Digital out | 4-ch relay module IN2 | HIGH = energized → 72V to ECU Gear S |
| 6 | Gear R Out | PA5 (encoded 5) | Digital out | 4-ch relay module IN3 | HIGH = energized → 72V to ECU Gear R |

### 5.4 MTR — Throttle Circuit (0–5 V)

```
Throttle Grip (0–5V) ──┬── R1 (1.8 kΩ) ──┬── ADC pin (PA0)
                        │                  │
                        └── R2 (3.3 kΩ) ──┴── GND

MCP4725 DAC #2: VCC = 5 V, GND, SDA = PB7, SCL = PB6
  I2C addr 0x61 (A0 tied to VCC)
  VOUT → Motor Controller throttle input
```

### 5.5 MTR — Gear Circuit (72 V Bidirectional)

**Input (read gear selector, galvanic isolation via TLP281):**

```
72V Gear D ──┬── R_current_limit (~33 kΩ / 2 W) ── TLP281 pin 1 (anode)
             └── TLP281 pin 2 (cathode) ── GND_72V

TLP281 pin 4 (collector) ── GPIO (PB0) ── 10k pull-up ── 3.3 V
TLP281 pin 3 (emitter) ── GND
```

**Output (mimic 72V to ECU, mechanical relay isolation):**

```
72V Battery ──┬──[1A fast-blow fuse]──┬── Relay COM (D) ── NO ── ECU Gear D ──┬── [TVS SMCJ90CA] ── GND
              │                       ├── Relay COM (S) ── NO ── ECU Gear S ──┼── [TVS SMCJ90CA] ── GND
              │                       └── Relay COM (R) ── NO ── ECU Gear R ──┴── [TVS SMCJ90CA] ── GND
```

**Protection components:**

| Protection | Part | Spec |
|-----------|------|------|
| Fuse | 1A fast-blow | 72 V, 1 A |
| TVS ×3 | SMCJ90CA bidirectional | 90–100 V standoff, 1500 W peak |
| Optoisolator | TLP281-4 (4-ch) | 2500 Vrms isolation |
| Gear relays | 5 V coil, 4-channel module | COM rated for 72 V DC |

### 5.6 MTR — Mode-Gated Behavior

| Mode | CAN 0x110 | Throttle | Gear | Notes |
|------|:---------:|----------|------|-------|
| MANUAL | 0 | ADC → DAC pass-through | Sense → relay pass-through | Rider direct control |
| AUTO | 1 | Follow CAN 0x204 `RT_MotorSpeed` | Follow CAN 0x204 `RT_Gear` | RT kinematics |
| ESTOP | 2 | DAC = 0 (cut throttle) | All relays off | Hardware + CAN kill |

### 5.7 MTR — Power

| Rail | Source | Used For |
|------|--------|----------|
| 3.3 V | STM32 board regulator | STM32 MCU, TLP281 output side pull-ups |
| 5 V | STM32 board regulator (or external) | MCP4725 DAC VCC, gear relay coils |
| 72 V | Traction battery (via 1A fuse) | Gear relay COM terminals |
| GND | Board ground | Common 3.3V/5V ground. **Isolated from 72V GND** via TLP281. |

---

## 6. PWT ESP32-S3 — Powertrain CAN Gateway

**Role:** Bridge low bus (500 kbit/s) ↔ powertrain bus (250 kbit/s). DC-DC converter control.
**Board:** ESP32-S3-DevKitC-1
**CAN modules:** 2 — TWAI0 (built-in, low bus) + TWAI1 (built-in, powertrain bus)

### 6.1 PWT — CAN Module Wiring

#### Module A: WCMCU-230 (Low Bus via TWAI0, 500 kbit/s)

| GPIO | Signal | Connected To | Wire |
|------|--------|-------------|------|
| — (3V3) | 3.3 V power | WCMCU-230 #1 VCC | Dupont F-F, red |
| — (GND) | Ground | WCMCU-230 #1 GND | Dupont F-F, black |
| 5 | CAN TX (TWAI0) | WCMCU-230 #1 CTX | Dupont F-F, blue |
| 4 | CAN RX (TWAI0) | WCMCU-230 #1 CRX | Dupont F-F, green |
| — | CAN_H | Low bus backbone | 22 AWG, yellow |
| — | CAN_L | Low bus backbone | 22 AWG, green |
| — | GND | Low bus backbone | 22 AWG, black |

#### Module B: WCMCU-230 (Powertrain Bus via TWAI1, 250 kbit/s)

| GPIO | Signal | Connected To | Wire |
|------|--------|-------------|------|
| — (3V3) | 3.3 V power | WCMCU-230 #2 VCC | Dupont F-F, red |
| — (GND) | Ground | WCMCU-230 #2 GND | Dupont F-F, black |
| 7 | CAN TX (TWAI1) | WCMCU-230 #2 CTX | Dupont F-F, blue |
| 6 | CAN RX (TWAI1) | WCMCU-230 #2 CRX | Dupont F-F, green |
| — | CAN_H | Powertrain bus backbone | 22 AWG, blue |
| — | CAN_L | Powertrain bus backbone | 22 AWG, white |
| — | GND | Powertrain bus backbone | 22 AWG, black |

- Source: `pwt::kCanLowTxGpio=5`, `pwt::kCanLowRxGpio=4` (TWAI0), `pwt::kCanPwtTxGpio=7`, `pwt::kCanPwtRxGpio=6` (TWAI1)
- Both TWAI controllers built into ESP32-S3 — no external MCP2515 needed
- Both use SN65HVD230 3.3 V CAN transceivers
- Termination: depends on bus position — enable 120 Ω if PWT is at physical end of either bus

### 6.2 PWT — Inputs

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN RX (low bus) | 4 | TWAI0 RX | WCMCU-230 #1 CRX | 0x012, 0x001, 0x7FD, 0x7FE from low bus |
| 2 | CAN RX (pwt bus) | 6 | TWAI1 RX | WCMCU-230 #2 CRX | Motor controller telemetry, 0x001 from pwt bus |

### 6.3 PWT — Outputs

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN TX (low bus) | 5 | TWAI0 TX | WCMCU-230 #1 CTX | 0x001 forward, PWT heartbeat (0x7FB), motor telemetry |
| 2 | CAN TX (pwt bus) | 7 | TWAI1 TX | WCMCU-230 #2 CTX | 0x012 DC-DC cmd forward, 0x001 forward |
| 3 | WDT Toggle | 21 | Digital out | TPS3850 WDI pin | Toggled at 20 Hz |

### 6.4 PWT — GPIO Quick Reference

```
GPIO 4  : CAN RX — low bus TWAI0 (500 kbit/s)
GPIO 5  : CAN TX — low bus TWAI0 (500 kbit/s)
GPIO 6  : CAN RX — powertrain bus TWAI1 (250 kbit/s)
GPIO 7  : CAN TX — powertrain bus TWAI1 (250 kbit/s)
GPIO 21 : WDT toggle → TPS3850 WDI (20 Hz)
```

### 6.5 PWT — CAN Gateway Forwarding

| Direction | CAN ID | Action |
|-----------|--------|--------|
| Low → Pwt | 0x012 (SYS_DCDC_CMD) | Transparent forward — same ID, same payload, on change |
| Low → Pwt | 0x001 (SAFETY_ESTOP) | Transparent forward — rate-limited (max 2 per 500ms) |
| Pwt → Low | 0x001 (SAFETY_ESTOP) | Transparent forward — rate-limited |
| Pwt → Low | Motor telemetry (TBD IDs) | Parse and republish as new IDs (TBD) on low bus |
| Low only | 0x7FB (PWT_HEARTBEAT) | 2 Hz heartbeat to RT/SYS on low bus |

### 6.6 PWT — DC-DC Converter Protocol

| Parameter | Value |
|-----------|-------|
| CAN ID | 0x10262B27 (29-bit extended) |
| DLC | 8 |
| Cycle | 100 ms (10 Hz) |
| Byte 0 | Control: 0x00 = Disable, 0x01 = Enable |
| Bytes 1–6 | Reserved: 0xFF |
| Byte 7 | Reset Ctrl: 0x00 = No reset, 0x01 = Reset |

### 6.7 PWT — Power

| Rail | Source | Used For |
|------|--------|----------|
| 3.3 V | USB or dev board regulator | ESP32-S3, both WCMCU-230 transceivers |
| GND | USB or dev board | Common ground — shared with both CAN modules |

---

## 7. Jetson Orin NX — Host Autonomy Computer

**Role:** High-level autonomy, path planning, obstacle detection
**CAN modules:** 1 — built-in CAN interface (high bus only)

### 7.1 Jetson — CAN Interface

| Signal | Connected To | Notes |
|--------|-------------|-------|
| CAN_H | High bus backbone | Direct to RT MCP2515 (sole peer on high bus) |
| CAN_L | High bus backbone | — |
| CAN GND | High bus backbone | Ground reference |

- 120 Ω terminator: **ON** (Jetson is one end of the high bus)
- RT MCP2515 is the other end (termination via CANalyst-II Ch1 during bench testing, or external 120 Ω in production)

### 7.2 Jetson — CAN Messages (High Bus)

| Direction | CAN ID | Name | Rate |
|-----------|--------|------|------|
| TX | 0x300 | HOST_DRIVE_CMD | ≤100 Hz |
| TX | 0x301 | HOST_BRAKE_REQ | on change |
| TX | 0x7FC | HOST_HEARTBEAT | 2 Hz |
| RX | 0x7FD | RT_HEARTBEAT | 2 Hz |
| RX | 0x011 | SYS_SAFETY_STS (forwarded) | 5 Hz |
| RX | 0x120 | SYS_THROTTLE_STS (forwarded) | 100 Hz |
| RX | 0x210 | RT_STATE_RPT | 10 Hz |
| RX | 0x220 | RT_PID_RPT | 10 Hz |
| RX | 0x600 | SYS_DIAG_RPT (forwarded) | 1 Hz |

### 7.3 Jetson — Power

| Rail | Source | Notes |
|------|--------|-------|
| 19 V | Dedicated DC-DC or battery | Jetson Orin NX power input |
| GND | Chassis ground | Common ground reference |

---

## 8. Debug ESP32-S3 — CAN Diagnostic Tool

**Role:** CAN bus monitor, frame decoder, injector node for bench testing
**Board:** ESP32-S3-DevKitC-1
**CAN modules:** 1 or 2 — TWAI (built-in) + optional MCP2515 SPI for dual-bus

### 8.1 Debug — CAN Module Wiring

#### Module A: WCMCU-230 (via TWAI, 500 kbit/s)

| GPIO | Signal | Connected To | Notes |
|------|--------|-------------|-------|
| — (3V3) | 3.3 V power | WCMCU-230 VCC | — |
| — (GND) | Ground | WCMCU-230 GND | — |
| 5 | CAN TX (TWAI) | WCMCU-230 CTX | Default TWAI0 pins |
| 4 | CAN RX (TWAI) | WCMCU-230 CRX | Default TWAI0 pins |

#### Module B: MCP2515 SPI (optional, for dual-bus monitoring)

| GPIO | Signal | Connected To | Notes |
|------|--------|-------------|-------|
| 36 | SPI SCK | MCP2515 SCK | Same pinout as RT |
| 37 | SPI MOSI | MCP2515 SI | — |
| 38 | SPI MISO | MCP2515 SO | — |
| 39 | SPI CS | MCP2515 CS | — |
| 40 | INT | MCP2515 INT | — |
| — (5V) | 5 V power | MCP2515 VCC | TJA1050 transceiver |

- Transport: MQTT over Wi-Fi to backend
- Terminator: OFF (debug tool is a middle tap / passive listener)
- Source: `debug-tool/debug-esp32/`

---

## 9. EPS-C — Steer-by-Wire Unit (Third-Party)

**Role:** Steering actuator with internal CAN interface
**CAN bus:** Low bus (500 kbit/s)

### 9.1 EPS-C — CAN Interface

| Signal | Connected To | Notes |
|--------|-------------|-------|
| CAN_H | Low bus backbone | — |
| CAN_L | Low bus backbone | — |
| CAN GND | Low bus backbone | — |
| Power | 12 V (from DC-DC converter via fuse) | Internal power supply |

### 9.2 EPS-C — CAN Messages

| Direction | CAN ID | Name | Rate | Notes |
|-----------|--------|------|------|-------|
| TX | 0x201 | SES_STATUS | 100 Hz | Steering angle, torque, status |
| TX | 0x202 | SES_DIAG | 10 Hz | Diagnostic data |
| RX | 0x169 | VCU_SES_REQ | 50 Hz | Target steering angle from RT |

- **Termination:** If EPS-C is at a physical end of the low bus, enable 120 Ω. Otherwise leave off.

---

## 10. SEB — Brake-by-Wire Unit (Third-Party)

**Role:** Brake actuator with internal CAN interface
**CAN bus:** Low bus (500 kbit/s)

### 10.1 SEB — CAN Interface

| Signal | Connected To | Notes |
|--------|-------------|-------|
| CAN_H | Low bus backbone | — |
| CAN_L | Low bus backbone | — |
| CAN GND | Low bus backbone | — |
| Power | 12 V (from DC-DC converter via fuse) | Internal power supply |

### 10.2 SEB — CAN Messages

| Direction | CAN ID | Name | Rate | Notes |
|-----------|--------|------|------|-------|
| TX | 0x721 | SEB_STATUS | 100 Hz | Stroke, pressure, status |
| TX | 0x731 | SEB_DIAG | 10 Hz | Diagnostic data |
| RX | 0x7B9 | SYS_SEB_CMD | 50 Hz | Target brake pressure from SYS (manual) or RT (auto) |

---

## 11. DC-DC Converter (72 V → 12 V)

**Role:** Converts traction battery 72 V to 12 V accessory bus
**CAN buses:** Low bus (500 kbit/s, receives 0x012) AND Powertrain bus (250 kbit/s, receives forwarded 0x012 from PWT)

### 11.1 DC-DC — Connections

| Signal | Connected To | Notes |
|--------|-------------|-------|
| 72 V input | Traction battery (+) | Via main contactor / fuse |
| 72 V GND | Traction battery (−) | — |
| 12 V output | 12 V accessory bus (via fuse block) | Powers lamps, relays, bulbs, ECU boards |
| 12 V GND | Chassis ground | — |
| CAN_H | Low bus backbone + Powertrain bus backbone | Two separate CAN interfaces or one with PWT forwarding |
| CAN_L | Low bus backbone + Powertrain bus backbone | — |
| CAN GND | Bus ground reference | — |

**Note:** The DC-DC converter's CAN connection topology depends on the specific unit. If it has a single CAN interface, connect to the powertrain bus (250 kbit/s) and PWT bridges the 0x012 command from the low bus. If it has dual CAN, it can sit on both buses.

---

## 12. External Watchdog — TPS3850

Each ESP32 has a TPS3850 (or equivalent) window watchdog IC. If the firmware stops toggling the WDI pin, the IC asserts a hardware reset.

### 12.1 Watchdog Wiring

| Controller | WDI GPIO | Toggle Rate | Timeout | TPS3850 MR → |
|------------|----------|-------------|---------|---------------|
| RT ESP32-S3 | 21 | 100 Hz (control task) | ~100 ms | ESP32 EN pin |
| SYS ESP32-S3 | 23 | 20 Hz (safety task) | ~100 ms | ESP32 EN pin |
| PWT ESP32-S3 | 21 | 20 Hz | ~100 ms | ESP32 EN pin |

**Wiring per controller:**
```
GPIO (WDI pin) → TPS3850 WDI
TPS3850 MR → ESP32 EN pin (active-low reset)
TPS3850 VCC → 3.3 V
TPS3850 GND → GND
```

**Behavior:**
- GPIO toggles at the specified rate in the main safety/control task
- If toggling stops for >100 ms: TPS3850 pulls MR LOW → ESP32 hardware reset
- On reset: all GPIOs go high-impedance → motor controller sees 0 V throttle, all gear relays open
- Post-reset: firmware boots clean, starts in MANUAL mode, re-runs LBS sequences
- Startup delay ~200 ms before first toggle arms the watchdog

---

## 13. Power Distribution

```
72V Traction Battery
  │
  ├── [Main fuse / contactor] ── Motor Controller (72 V)
  │     │                         │
  │     │                         └── Motor (3-phase BLDC)
  │     │
  │     ├── DC-DC Converter (72 V → 12 V, CAN: 0x012)
  │     │     │
  │     │     ├── [12V Fuse Block — ATO/ATC, 12 circuits]
  │     │     │     │
  │     │     │     ├── 12V Relay (SYS GPIO27) ── 12V Accessory Bus
  │     │     │     │     ├── Signal lamps (via relays: 18, 19, 21, 22)
  │     │     │     │     ├── Mode bulbs (via relays: 25, 26)
  │     │     │     │     └── Headlight (via relay: 22)
  │     │     │     │
  │     │     │     ├── ESP32-S3 Dev Boards (×4) — onboard 5V/3.3V regulation
  │     │     │     ├── STM32 Board — onboard 5V/3.3V regulation
  │     │     │     ├── EPS-C (steer-by-wire unit)
  │     │     │     ├── SEB (brake-by-wire unit)
  │     │     │     └── Jetson Orin NX (19 V via separate DC-DC)
  │     │     │
  │     │     └── [M6 Ground Bus Bar — nickel-plated brass, ≥6 studs]
  │     │
  │     └── 72V Gear Lines (via 1A fuse → MTR relay COM terminals)
  │           ├── Relay D COM → NO → ECU Gear D
  │           ├── Relay S COM → NO → ECU Gear S
  │           └── Relay R COM → NO → ECU Gear R
  │
  └── Chassis Ground (M6 bolt to frame rail)
```

### 13.1 Voltage Domain Summary

| Domain | Source | Used By | Isolation |
|--------|--------|---------|-----------|
| 72 V DC | Traction battery | Motor controller, gear relays (COM), DC-DC input | Fuse + contactor |
| 12 V DC | DC-DC converter | Lamps, bulbs, relay coils, EPS-C, SEB, ECU boards (via onboard regulators) | 12V relay (SYS GPIO27) |
| 5 V DC | Onboard regulators (from 12 V or USB) | MCP4725 DACs, MCP2515 module (TJA1050), gear relay coils | — |
| 3.3 V DC | Onboard regulators | ESP32-S3 MCUs, STM32 MCU, WCMCU-230 transceivers, TLP281 output side | — |
| 72 V gear signals | Traction battery (via 1A fuse) | Gear selector → TLP281 input → GPIO | **Galvanic isolation** via TLP281 optoisolator (2500 Vrms) |

---

## 14. Gear Isolation — TLP281 Optoisolator Circuit

The 72 V gear selector signals must be galvanically isolated from the 3.3 V MCU GPIOs. A TLP281-4 quad optoisolator provides this isolation.

### 14.1 Circuit per Gear Channel

```
72V Gear line ──┬── R_current_limit (~33 kΩ / 2 W) ── TLP281 pin 1 (anode)
                └── TLP281 pin 2 (cathode) ── GND_72V

                                          ║  (optical isolation, 2500 Vrms)
                                          ║

TLP281 pin 4 (collector) ── GPIO (sense) ── 10k pull-up ── 3.3 V
TLP281 pin 3 (emitter) ── GND
```

- **72V present** → LED on → phototransistor conducts → GPIO reads LOW
- **72V absent** → LED off → phototransistor off → pull-up holds GPIO HIGH
- **R_current_limit:** ~33 kΩ, 2 W. At 72 V: I ≈ 2.2 mA through LED. P ≈ 0.16 W in resistor.

### 14.2 TLP281 Channel Mapping

| TLP281 Channel | 72V Gear Line | GPIO (SYS) | GPIO (MTR) | Notes |
|:--------------:|---------------|------------|------------|-------|
| 1 | Gear D | 12 | PB0 (encoded 16) | Drive gear sense |
| 2 | Gear S | 13 | PB1 (encoded 17) | Sport gear sense |
| 3 | Gear R | 14 | PB2 (encoded 18) | Reverse gear sense |
| 4 | (spare) | — | — | Available for future use |

---

## 15. Throttle DAC — MCP4725 Circuit

Two MCP4725 12-bit I2C DACs produce 0–5 V analog signals for the motor controller throttle input. One on SYS (EGAS Level 2 monitor), one on MTR (EGAS Level 1 primary).

### 15.1 MCP4725 Comparison

| Parameter | SYS (Level 2) | MTR (Level 1) |
|-----------|--------------|---------------|
| I2C address | **0x60** | **0x61** (A0 tied to VCC) |
| I2C SDA | GPIO15 | PB7 |
| I2C SCL | GPIO16 | PB6 |
| I2C bus | I2C_NUM_0 (separate bus) | Separate I2C bus |
| VCC | 5 V | 5 V |
| VOUT range | 0–5 V | 0–5 V |
| Resolution | 12-bit (4096 steps) | 12-bit (4096 steps) |
| Mode-gated? | Yes — ESTOP forces 0 V | Yes — ESTOP forces 0 V |

### 15.2 MCP4725 Fast Write Command

```
Byte 0: [0 0 PD1 PD0 D11 D10 D9 D8]  — C2=C1=0 (Fast Write), PD1=PD0=0 (normal mode)
Byte 1: [D7 D6 D5 D4 D3 D2 D1 D0]    — Lower 8 data bits
```

Both DACs operate in standard mode at 100 kHz I2C clock.

---

## 16. CAN Termination Summary

| Bus | Node | Terminator | Resistor | Notes |
|-----|------|:----------:|----------|-------|
| **High** | Jetson Orin | ✅ ON | 120 Ω | Endpoint |
| **High** | RT MCP2515 | ❌ OFF | — | Middle (CANalyst-II Ch1 is other endpoint in bench test) |
| **High** | CANalyst-II Ch1 | ✅ ON | 120 Ω | Bench-test endpoint. Remove in production, add 120 Ω at RT. |
| **Low** | RT WCMCU-230 | ✅ ON | 120 Ω | Endpoint |
| **Low** | SYS WCMCU-230 | ✅ ON | 120 Ω | Endpoint |
| **Low** | PWT WCMCU-230 #1 | ❌ OFF | — | Middle tap |
| **Low** | MTR CAN transceiver | ❌ OFF | — | Middle tap |
| **Low** | EPS-C | ❌ OFF | — | Middle tap (unless at physical end) |
| **Low** | SEB | ❌ OFF | — | Middle tap (unless at physical end) |
| **Low** | DC-DC Converter | ❌ OFF | — | Middle tap (unless at physical end) |
| **Low** | CANalyst-II Ch0 | ❌ OFF | — | Middle tap |
| **Pwt** | PWT WCMCU-230 #2 | ✅ ON | 120 Ω | Endpoint |
| **Pwt** | Motor Controller | ✅ ON | 120 Ω | Endpoint |
| **Pwt** | DC-DC Converter | ❌ OFF | — | Middle tap (if on pwt bus) |

**Resulting bus impedances (power off):**
- High bus: ~120 Ω (Jetson ∥ CANalyst-II Ch1; bench test) or ~60 Ω (Jetson ∥ RT; production)
- Low bus: ~60 Ω (RT ∥ SYS, both 120 Ω)
- Powertrain bus: ~60 Ω (PWT ∥ Motor Controller, both 120 Ω)

---

## 17. Pre-Power Checklist

### 17.1 RT ESP32-S3

- [ ] WCMCU-230 (low bus): VCC → 3V3 (J1-1), GND → GND (J1-22), CTX → GPIO5, CRX → GPIO4
- [ ] WCMCU-230: 120 Ω terminator jumper **ON**
- [ ] MCP2515 (high bus): VCC → 5V (J1-21), GND → GND (J1-22)
- [ ] MCP2515: SCK → GPIO36, MOSI → GPIO37, MISO → GPIO38, CS → GPIO39, INT → GPIO40
- [ ] MCP2515: CAN_H/CAN_L to high bus backbone
- [ ] WDT: GPIO21 → TPS3850 WDI

### 17.2 SYS ESP32-S3

- [ ] WCMCU-230 (low bus): VCC → 3V3 (J1-1), GND → GND (J1-22), CTX → GPIO5, CRX → GPIO4
- [ ] WCMCU-230: 120 Ω terminator jumper **ON**
- [ ] ESTOP button: GPIO1 → NC button → GND. 10k pull-up to 3.3V.
- [ ] Brake lever: GPIO2 → NO switch → GND. Internal pull-up.
- [ ] START button: GPIO32 → NO momentary → GND. Internal pull-up.
- [ ] MODE button: GPIO11 → NO momentary → GND. Internal pull-up.
- [ ] Throttle ADC: GPIO10 → voltage divider midpoint (0–5V grip)
- [ ] MCP4725 DAC: SDA → GPIO15, SCL → GPIO16, VCC → 5V, GND → GND
- [ ] Gear sense: GPIO12/13/14 → TLP281 ch1/ch2/ch3 collectors (with 10k pull-ups to 3.3V)
- [ ] Gear relays: GPIO33/34/35 → relay module IN1/IN2/IN3
- [ ] Signal switches: GPIO3 (left turn), GPIO6 (right turn), GPIO7 (headlight) → GND
- [ ] Lamp relays: GPIO18/19/21/22 → relay coils → 12V
- [ ] Indicator relays: GPIO25 (AUTO), GPIO26 (MANUAL), GPIO27 (12V power) → relay coils → 12V
- [ ] WDT: GPIO23 → TPS3850 WDI

### 17.3 MTR STM32

- [ ] CAN transceiver: TX/RX → STM32 CAN pins (TBD)
- [ ] ESTOP: shared hardwire with SYS GPIO1
- [ ] Throttle ADC: PA0 → voltage divider (0–5V grip)
- [ ] Gear sense: PB0/PB1/PB2 → TLP281 ch1/ch2/ch3 outputs
- [ ] Gear relays: PA3/PA4/PA5 → relay module IN1/IN2/IN3
- [ ] MCP4725 DAC #2: SDA → PB7, SCL → PB6, A0 → VCC (addr 0x61)

### 17.4 PWT ESP32-S3

- [ ] WCMCU-230 #1 (low bus): VCC → 3V3, GND → GND, CTX → GPIO5, CRX → GPIO4
- [ ] WCMCU-230 #2 (pwt bus): VCC → 3V3, GND → GND, CTX → GPIO7, CRX → GPIO6
- [ ] Terminators: enable on modules that are bus endpoints
- [ ] WDT: GPIO21 → TPS3850 WDI

### 17.5 Multimeter Checks (All Power OFF)

| Measurement | Expected | If Wrong |
|-------------|----------|----------|
| High bus CAN_H ↔ CAN_L | ~120 Ω (bench) or ~60 Ω (production) | Check terminators |
| Low bus CAN_H ↔ CAN_L | ~60 Ω | Check RT + SYS terminators |
| Powertrain bus CAN_H ↔ CAN_L | ~60 Ω | Check PWT + motor controller terminators |
| High bus CAN_H ↔ Low bus CAN_H | ∞ (open) | Buses must be isolated |
| High bus CAN_H ↔ GND | > 1 kΩ | Possible short |
| Low bus CAN_H ↔ GND | > 1 kΩ | Possible short |
| Any CAN pin ↔ VCC (3.3V) | > 1 kΩ | Possible short |
| 3.3V rail ↔ GND | > 100 Ω | Check for dead shorts on any board |

### 17.6 Power-On Voltage Checks

After applying USB/12V power, before starting CAN traffic:

| Measurement | Expected | Notes |
|-------------|----------|-------|
| CAN_H to GND (any bus) | ~2.5 V | Recessive / idle state |
| CAN_L to GND (any bus) | ~2.5 V | Recessive / idle state |
| CAN_H to CAN_L (idle) | ~0 V | No frames transmitting |
| MCP4725 VOUT (ESTOP) | 0 V | DAC disabled in ESTOP |
| ESP32 3.3V rail | 3.3 V ± 0.1 V | Within spec |
| ESP32 5V rail | 5.0 V ± 0.25 V | Within spec |
| TLP281 collector (72V gear off) | 3.3 V | Pull-up holds HIGH |
| TLP281 collector (72V gear on) | ~0 V | Opto pulls LOW |

---

## 18. Complete GPIO Master Reference

### 18.1 RT ESP32-S3

```
GPIO 1  : Encoder A — rear motor                    [IN, PCNT]
GPIO 2  : Encoder B — rear motor                    [IN, PCNT]
GPIO 3  : Encoder A — front wheel (TBD)             [IN, PCNT, disabled]
GPIO 4  : CAN RX — low bus TWAI                     [IN]
GPIO 5  : CAN TX — low bus TWAI                     [OUT]
GPIO 6  : Encoder B — front wheel (TBD)             [IN, PCNT, disabled]
GPIO 7  : —                                         [unused]
GPIO 8  : —                                         [unused]
GPIO 9  : Encoder A — rear left (TBD)               [IN, PCNT, disabled]
GPIO 10 : I2C SDA — IMU (optional)                  [I/O, I2C]
GPIO 11 : I2C SCL — IMU (optional)                  [OUT, I2C]
GPIO 12 : Encoder B — rear left (TBD)               [IN, PCNT, disabled]
GPIO 13 : Encoder A — rear right (TBD)              [IN, PCNT, disabled]
GPIO 14 : Encoder B — rear right (TBD)              [IN, PCNT, disabled]
GPIO 15–20: —                                       [unused]
GPIO 21 : WDT toggle → TPS3850 WDI                  [OUT, 100 Hz]
GPIO 22–35: —                                       [unused]
GPIO 36 : SPI SCK → MCP2515                         [OUT, ≤10 MHz]
GPIO 37 : SPI MOSI → MCP2515 SI                     [OUT]
GPIO 38 : SPI MISO ← MCP2515 SO                     [IN]
GPIO 39 : SPI CS → MCP2515                          [OUT, active-low]
GPIO 40 : INT ← MCP2515                             [IN, falling edge]
GPIO 41–48: —                                       [PSRAM / unavailable]
```

### 18.2 SYS ESP32-S3

```
GPIO 1  : ESTOP button (NC, active-low)             [IN, 10k pull-up]
GPIO 2  : Brake lever (NO, active-low)              [IN, internal pull-up]
GPIO 3  : Left turn switch (NO, active-low)         [IN, internal pull-up]
GPIO 4  : CAN RX — low bus TWAI                     [IN]
GPIO 5  : CAN TX — low bus TWAI                     [OUT]
GPIO 6  : Right turn switch (NO, active-low)        [IN, internal pull-up]
GPIO 7  : Headlight switch (active-low)             [IN, internal pull-up]
GPIO 8  : Ignition relay (HIGH = ON)                [OUT]
GPIO 9  : —                                         [unused]
GPIO 10 : Throttle ADC (ADC1_CH5)                   [IN, analog, 0–3.3V via divider]
GPIO 11 : MODE button (NO, active-low)              [IN, internal pull-up]
GPIO 12 : Gear D sense ← TLP281 ch1                 [IN, 10k pull-up]
GPIO 13 : Gear S sense ← TLP281 ch2                 [IN, 10k pull-up]
GPIO 14 : Gear R sense ← TLP281 ch3                 [IN, 10k pull-up]
GPIO 15 : MCP4725 DAC SDA                           [I/O, I2C addr 0x60]
GPIO 16 : MCP4725 DAC SCL                           [OUT, I2C]
GPIO 17 : READY bulb (green) relay                  [OUT, active-high]
GPIO 18 : Left turn lamp relay                      [OUT, active-high]
GPIO 19 : Right turn lamp relay                     [OUT, active-high]
GPIO 20 : ESTOP bulb (red) relay                    [OUT, active-high]
GPIO 21 : Brake light relay                         [OUT, active-high]
GPIO 22 : Headlight relay                           [OUT, active-high]
GPIO 23 : WDT toggle → TPS3850 WDI                  [OUT, 20 Hz]
GPIO 24 : —                                         [unused]
GPIO 25 : AUTO mode bulb relay                      [OUT, active-high]
GPIO 26 : MANUAL mode bulb relay                    [OUT, active-high]
GPIO 27 : 12V power relay                           [OUT, active-high]
GPIO 28–31: —                                       [unused]
GPIO 32 : START button (NO, active-low)             [IN, internal pull-up]
GPIO 33 : Gear D relay out → relay module IN1       [OUT, active-high]
GPIO 34 : Gear S relay out → relay module IN2       [OUT, active-high]
GPIO 35 : Gear R relay out → relay module IN3       [OUT, active-high]
GPIO 36–48: —                                       [PSRAM / unavailable]
```

### 18.3 MTR STM32

```
Pin            Encoded  Signal                         Direction
──────────────────────────────────────────────────────────────────
PA0 (ADC1_IN0)    0     Throttle ADC (0–3.3V via divider)  [IN]
PA1               1     (unused)
PA2               2     (unused)
PA3               3     Gear D relay out → relay IN1       [OUT]
PA4               4     Gear S relay out → relay IN2       [OUT]
PA5               5     Gear R relay out → relay IN3       [OUT]
PA6–PA15        6–15   (unused)
PB0              16     Gear D sense ← TLP281 ch1          [IN, 10k pull-up]
PB1              17     Gear S sense ← TLP281 ch2          [IN, 10k pull-up]
PB2              18     Gear R sense ← TLP281 ch3          [IN, 10k pull-up]
PB3–PB5        19–21   (unused)
PB6              22     MCP4725 DAC SCL                    [OUT, I2C]
PB7              23     MCP4725 DAC SDA                    [I/O, I2C addr 0x61]
PB8–PB15       24–31   (unused)
PC0–PC15       32–47   (unused / reserved)
—               TBD     CAN RX ← transceiver               [IN]
—               TBD     CAN TX → transceiver               [OUT]
—               TBD     ESTOP button (shared with SYS)     [IN, NC, active-low]
```

### 18.4 PWT ESP32-S3

```
GPIO 4  : CAN RX — low bus TWAI0 (500 kbit/s)        [IN]
GPIO 5  : CAN TX — low bus TWAI0 (500 kbit/s)        [OUT]
GPIO 6  : CAN RX — powertrain bus TWAI1 (250 kbit/s) [IN]
GPIO 7  : CAN TX — powertrain bus TWAI1 (250 kbit/s) [OUT]
GPIO 21 : WDT toggle → TPS3850 WDI                   [OUT, 20 Hz]
```

### 18.5 Debug ESP32-S3

```
GPIO 4  : CAN RX — TWAI (bus A, 500 kbit/s)          [IN]
GPIO 5  : CAN TX — TWAI (bus A, 500 kbit/s)          [OUT]
GPIO 36 : SPI SCK → MCP2515 (optional, bus B)        [OUT]
GPIO 37 : SPI MOSI → MCP2515 SI (optional)           [OUT]
GPIO 38 : SPI MISO ← MCP2515 SO (optional)           [IN]
GPIO 39 : SPI CS → MCP2515 (optional)                [OUT]
GPIO 40 : INT ← MCP2515 (optional)                   [IN]
```

---

## 19. Connector & Wire Reference

### 19.1 Wire Gauges

| Circuit | AWG | Color | Notes |
|---------|-----|-------|-------|
| CAN_H (bus backbone) | 22 | Yellow / Orange / Blue | Twisted pair with CAN_L |
| CAN_L (bus backbone) | 22 | Green / Blue / White | Twisted pair with CAN_H |
| CAN GND (bus backbone) | 22 | Black | Ground reference |
| ESP32 ↔ Transceiver (Dupont) | 24–26 | Various | 20 cm female-female jumpers |
| 72V traction power | 8–10 | Red / Black | High current |
| 12V accessory power | 14–16 | Red / Black | Medium current |
| 5V DAC / logic power | 20–22 | Red / Black | Low current |
| Signal / GPIO | 20–22 | Various | Low current |
| Chassis ground bonding | 8–10 | Green/Yellow | Safety ground |

### 19.2 Connector Types

| Application | Connector Series | Notes |
|-------------|-----------------|-------|
| 72V battery | Anderson SB50 | 50A rated, polarized |
| 12V power distribution | ATO/ATC fuse block | 12 circuits |
| CAN bus drops | Deutsch DT 4-pin | Sealed, automotive |
| Gear relay outputs | TE Superseal 3-pin | Sealed |
| ESP32 dev board I/O | 2.54 mm Dupont headers | Bench test only — use Deutsch for vehicle |
| Dashboard controls | Molex Mini-Fit Jr | Handlebar junction |
| Ground bus | M6 ring terminals → bus bar | Nickel-plated brass |

---

## 20. Troubleshooting

| Symptom | Likely Cause | Check |
|---------|-------------|-------|
| No frames on any bus | Power not applied, CAN transceiver not powered | 3.3V at WCMCU-230 VCC pin |
| Only one node's frames visible | CAN wiring open/short, or one transceiver bad | Continuity CAN_H/CAN_L between nodes. Swap WCMCU-230 modules. |
| One node receives but doesn't transmit | Counterfeit/fake SN65HVD230 chip | Swap modules between working and non-working node |
| SYS stuck in ESTOP | RT heartbeat missing (0x7FD) | Check RT powered, CAN wired, CTX/CRX not swapped |
| `heartbeat_ok=0` in 0x011 | RT 0x7FD not arriving at SYS | SYS TWAI RX issue or bus wiring |
| RT steering FAULT | No 0x201 (EPS-C status) on low bus | Inject 0x201 or connect EPS-C |
| CAN bus-off errors | Missing or wrong termination | Measure CAN_H ↔ CAN_L resistance (power off) |
| Random CRC errors | Missing CAN ground reference | Connect GND between all CAN nodes |
| MCP2515 init fails | SPI wiring wrong, wrong crystal setting | Check MOSI/MISO not swapped. Verify crystal frequency. |
| MCP2515 "not in config mode" on RT boot | Normal — MCP2515 not installed | Expected. RT boots without high bus. |
| MCP4725 I2C write fails | Wrong address, wiring | Check SDA/SCL continuity. Verify addr: SYS=0x60, MTR=0x61. |
| Gear sense always reads HIGH (no gear) | 72V not present, TLP281 circuit open | Verify 72V at gear selector. Check TLP281 input resistor. |
| Relay chatters or doesn't engage | Insufficient coil voltage, no flyback diode | Check 5V at relay module. Add flyback diode across coil. |
| ESP32 random resets | Watchdog not being toggled, power brownout | Check WDT GPIO toggling on scope. Verify 3.3V rail stable. |
| CANalyst-II "connect timeout" | Wrong driver or device not found | Re-run Zadig. Device Manager → "WinUSB" device. |

---

## 21. References

| Document | Path | Content |
|----------|------|---------|
| Architecture Overview | `architecture.md` | System topology, CAN message catalog, mode state machine |
| CAN Protocol Definitions | `shared/can/can_protocol.h` | All CAN ID constants and struct layouts |
| CAN Signal Dictionary | `can-dictionary.md` | Bit-level CAN signal definitions |
| CAN Bench Test Plan | `docs/can-bench-test.md` | Bench test wiring, injection scripts, test scenarios |
| Wiring Harness Spec | `docs/wiring-harness.md` | Buildable harness, connectors, BOM per sub-harness |
| I/O Data Inventory | `docs/io-data.md` | Complete variable inventory across all ECUs |
| High-Voltage Isolation | `docs/high-voltage-isolation.md` | 72V isolation, TLP281, TVS protection |
| External Watchdog | `docs/external-watchdog.md` | TPS3850 wiring, timeout, testing |
| CAN Module Notes | `notes/can-module-notes.md` | WCMCU-230 and MCP2515 practical notes |
| CAN Hardware Basics | `notes/can-hardware-basics.md` | CAN physical layer fundamentals |
| CAN Gateway Design | `notes/can-gateway-design.md` | Forwarding categories, dispatch design |
| Debug Tool Architecture | `debug-tool/debug-tool-architecture.md` | Debug tool system design |
| CANalyst-II Setup | `debug-tool/CANALYST-II-SETUP.md` | Driver installation, environment variables |
| RT Config | `rt-esp32/src/config.h` | RT timing and GPIO constants |
| SYS Config | `sys-esp32/src/config.h` | SYS timing and GPIO constants |
| MTR Config | `mtr-stm32/src/config.h` | MTR timing and pin constants |
| PWT Config | `pwt-esp32/src/config.h` | PWT timing and GPIO constants |
| PWT Architecture | `pwt-esp32/pwt-architecture.md` | Powertrain gateway detailed design |
| Shared Config | `shared/shared_config.h` | Vehicle-wide constants |
