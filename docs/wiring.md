# E-Trike Wiring Reference

Pin-to-pin wiring for both ESP32-S3 boards, CAN buses, and all peripherals. Derived from `architecture.md` and `config.h`.

---

## 1. CAN Bus Topology

Two physically separate CAN buses at 500 kbit/s. Each requires 120Ω termination at both ends.

### Low-Level CAN Bus

```
  RT-ESP32 (TWAI)          SYS-ESP32 (TWAI)       EPS-C          SEB         DC-DC
  TX=5  RX=4              TX=5  RX=4           (steering)      (brake)    72V→12V
   │     │                  │     │                │              │           │
   ├──┬──┘                  ├──┬──┘                │              │           │
   │  │                     │  │                   │              │           │
 CAN_H CAN_L              CAN_H CAN_L           CAN_H CAN_L   CAN_H CAN_L  CAN_H CAN_L
   │  │                     │  │                   │  │           │  │        │  │
   └──┼─────────────────────┼──┼───────────────────┼──┼───────────┼──┼────────┼──┤
      └─────────────────────┴──┴───────────────────┴──┴───────────┴──┴────────┴──┘
                                   120Ω terminator at each physical end
```

| Signal | RT GPIO | SYS GPIO | Notes |
|--------|---------|----------|-------|
| CAN TX | 5 | 5 | To SN65HVD230 TXD |
| CAN RX | 4 | 4 | From SN65HVD230 RXD |
| CAN_GND | — | — | **Must** connect grounds if nodes on separate power supplies |

### High-Level CAN Bus

```
  Jetson Orin NX           RT-ESP32 (MCP2515 SPI)
  CAN interface           SCK=36 MOSI=37 MISO=38 CS=39 INT=40
      │                        │
   CAN_H CAN_L              CAN_H CAN_L
      │  │                     │  │
      └──┼─────────────────────┼──┘
         └─────────────────────┘
           120Ω terminator at each end
```

---

## 2. RT ESP32-S3 — Input/Output Tables

### 2.1 INPUTS (signals RT reads from external hardware)

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN RX (low bus) | 4 | TWAI RX | SN65HVD230 RXD pin | Low-level CAN: SYS, EPS-C, SEB, DC-DC |
| 2 | MCP2515 MISO | 38 | SPI | MCP2515 SO pin | High-level CAN RX data |
| 3 | MCP2515 INT | 40 | Digital | MCP2515 INT pin | Interrupt: RX buffer ready |
| 4 | Encoder A (rear motor) | 1 | PCNT | Encoder sensor | Speed feedback — **active** |
| 5 | Encoder B (rear motor) | 2 | PCNT | Encoder sensor | Quadrature phase B |
| 6 | Encoder A (front wheel) | 3 | PCNT | Encoder sensor | **Sensor TBD** |
| 7 | Encoder B (front wheel) | 6 | PCNT | Encoder sensor | **Sensor TBD** |
| 8 | Encoder A (rear left) | 9 | PCNT | Encoder sensor | **Sensor TBD** |
| 9 | Encoder B (rear left) | 12 | PCNT | Encoder sensor | **Sensor TBD** |
| 10 | Encoder A (rear right) | 13 | PCNT | Encoder sensor | **Sensor TBD** |
| 11 | Encoder B (rear right) | 14 | PCNT | Encoder sensor | **Sensor TBD** |
| 12 | I2C SDA | 10 | I2C | IMU (optional) | Pull-up to 3.3V required |

### 2.2 OUTPUTS (signals RT drives to external hardware)

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN TX (low bus) | 5 | TWAI TX | SN65HVD230 TXD pin | Commands to SYS + EPS-C |
| 2 | SPI SCK | 36 | SPI | MCP2515 SCK pin | 10 MHz clock |
| 3 | SPI MOSI | 37 | SPI | MCP2515 SI pin | High-level CAN TX data |
| 4 | SPI CS | 39 | Digital | MCP2515 CS pin | Chip select, active low |
| 5 | I2C SCL | 11 | I2C | IMU (optional) | Clock |
| 6 | WDT Toggle | 21 | Digital | TPS3850 WDI pin | Toggled at 100 Hz by control_task |

---

## 3. SYS ESP32-S3 — Input/Output Tables

### 3.1 INPUTS (signals SYS reads from external hardware)

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN RX (low bus) | 4 | TWAI RX | SN65HVD230 RXD | RT commands, SEB feedback |
| 2 | ESTOP Button | 1 | Digital, NC | Red mushroom → GND | 10k pull-up to 3.3V. LOW = ESTOP. |
| 3 | Brake Lever | 2 | Digital, NO | Lever switch → GND | Internal pull-up. LOW = pressed. |
| 4 | START Button | 32 | Digital, NO | Green momentary → GND | Internal pull-up. LOW = pressed. |
| 5 | MODE Button | 11 | Digital, NO | Momentary → GND | Internal pull-up. LOW = pressed. |
| 6 | Throttle ADC | 10 | ADC1_CH5 | Voltage divider from 0–5V grip | 5V→3.3V via resistive divider |
| 7 | Gear D Sense | 12 | Digital | TLP281 ch1 output | 72V gear line → optoisolator → GPIO |
| 8 | Gear S Sense | 13 | Digital | TLP281 ch2 output | 72V gear line → optoisolator → GPIO |
| 9 | Gear R Sense | 14 | Digital | TLP281 ch3 output | 72V gear line → optoisolator → GPIO |
| 10 | Left Turn Switch | 3 | Digital, NO | Handlebar momentary → GND | Internal pull-up |
| 11 | Right Turn Switch | 6 | Digital, NO | Handlebar momentary → GND | Internal pull-up |
| 12 | Headlight Switch | 7 | Digital, NO | Handlebar toggle → GND | Internal pull-up |

### 3.2 OUTPUTS (signals SYS drives to external hardware)

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN TX (low bus) | 5 | TWAI TX | SN65HVD230 TXD | Telemetry to RT, brake cmd to SEB |
| 2 | MCP4725 SDA | 15 | I2C | MCP4725 DAC SDA | Throttle 0–5V output. Pull-up to 3.3V. |
| 3 | MCP4725 SCL | 16 | I2C | MCP4725 DAC SCL | Clock |
| 4 | Gear D Out | 33 | Digital | 4-ch relay module IN1 | 72V to ECU gear D wire |
| 5 | Gear S Out | 34 | Digital | 4-ch relay module IN2 | 72V to ECU gear S wire |
| 6 | Gear R Out | 35 | Digital | 4-ch relay module IN3 | 72V to ECU gear R wire |
| 7 | Left Turn Lamp | 18 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 8 | Right Turn Lamp | 19 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 9 | Brake Light | 21 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 10 | Headlight | 22 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 11 | AUTO Bulb | 25 | Digital | Relay coil → 12V | Relay COM=12V, NO=bulb |
| 12 | MANUAL Bulb | 26 | Digital | Relay coil → 12V | Relay COM=12V, NO=bulb |
| 13 | 12V Power Relay | 27 | Digital | Relay coil → 12V | Relay COM=12V bus, NO=accessories |
| 14 | WDT Toggle | 23 | Digital | TPS3850 WDI pin | Toggled at 20 Hz by safety_task |

### 3.2 Safety — Dashboard Buttons

| Button | GPIO | Type | Wiring |
|--------|------|------|--------|
| ESTOP | 1 | NC, active-low | Big red mushroom. GPIO → button → GND. 10k pull-up to 3.3V. |
| START | 32 | NO, active-low | Green momentary. GPIO → button → GND. Internal pull-up. |
| MODE | 11 | NO, active-low | Momentary. GPIO → button → GND. Internal pull-up. |
| Brake Lever | 2 | Active-low | GPIO → lever switch → GND. Internal pull-up. |

### 3.3 Throttle — Bidirectional 0–5V

| Signal | GPIO | Circuit |
|--------|------|---------|
| Throttle Read | 10 (ADC1_CH5) | 0–5V grip → **voltage divider** (2-resistor, ~5V→3.3V) → GPIO10 |
| Throttle Output | I2C: SDA=15, SCL=16 | **MCP4725 DAC** (addr 0x60, VCC=5V). Output 0–5V → motor controller |

```
Throttle Grip (0-5V) ──┬── R1 (e.g. 1.8kΩ) ──┬── GPIO10 (ADC)
                        │                      │
                        └── R2 (e.g. 3.3kΩ) ──┴── GND

MCP4725: VCC=5V, GND, SDA→GPIO15, SCL→GPIO16, VOUT→Motor Controller
```

### 3.4 Gear — Bidirectional 72V

**Input** (read gear selector, galvanic isolation):

| Signal | GPIO | Circuit |
|--------|------|---------|
| Gear D Sense | 12 | 72V → **TLP281 optoisolator ch1** (input: 72V+resistor, output: GPIO12 + pull-up) |
| Gear S Sense | 13 | TLP281 optoisolator ch2 |
| Gear R Sense | 14 | TLP281 optoisolator ch3 |

```
72V Gear D ──┬── R (current limit, ~33kΩ/2W) ── TLP281 pin1 (anode)
             └── TLP281 pin2 (cathode) ── GND_72V
TLP281 pin4 (collector) ── GPIO12 ── 10k pull-up ── 3.3V
TLP281 pin3 (emitter) ── GND
```

**Output** (mimic 72V to ECU, mechanical relay isolation):

| Signal | GPIO | Circuit |
|--------|------|---------|
| Gear D Out | 33 | GPIO → 4-ch 5V relay module IN1 → **COM=72V** (via 1A fuse) → NO → ECU Gear D |
| Gear S Out | 34 | GPIO → relay IN2 → COM=72V → NO → ECU Gear S |
| Gear R Out | 35 | GPIO → relay IN3 → COM=72V → NO → ECU Gear R |

**Protection**:
```
72V Battery ──┬──[1A fast-blow fuse]──┬── Relay COM (D) ── NO ── ECU Gear D ──┬── [TVS SMCJ90CA] ── GND
              │                       ├── Relay COM (S) ── NO ── ECU Gear S ──┼── [TVS SMCJ90CA] ── GND
              │                       └── Relay COM (R) ── NO ── ECU Gear R ──┴── [TVS SMCJ90CA] ── GND
```

| Protection | Part | Spec |
|-----------|------|------|
| Fuse | 1A fast-blow | 72V, 1A |
| TVS ×3 | SMCJ90CA bidirectional | 90–100V standoff, 1500W peak |

### 3.5 Signal Lights

| Signal | GPIO | Circuit |
|--------|------|---------|
| Left Turn Switch | 3 | Handlebar momentary, active-low. GPIO → switch → GND. Pull-up. |
| Right Turn Switch | 6 | Handlebar momentary, active-low |
| Headlight Switch | 7 | Handlebar toggle, active-low |
| Left Turn Lamp | 18 | GPIO → relay coil → 12V. Relay COM → 12V, NO → lamp. |
| Right Turn Lamp | 19 | GPIO → relay → 12V → lamp |
| Brake Light | 21 | GPIO → relay → 12V → lamp |
| Headlight | 22 | GPIO → relay → 12V → lamp |

### 3.6 Indicators & Power

| Signal | GPIO | Circuit |
|--------|------|---------|
| AUTO Bulb | 25 | GPIO → relay → 12V → bulb |
| MANUAL Bulb | 26 | GPIO → relay → 12V → bulb |
| 12V Relay | 27 | GPIO → relay coil → 12V. Relay COM → 12V bus, NO → accessories. |
| WDT Toggle | 23 | GPIO → TPS3850 WDI pin. Toggled at 20 Hz by safety_task. |

---

## 4. External Watchdog

Each ESP32 has a TPS3850 (or equivalent) window watchdog IC.

| Pin | RT | SYS |
|-----|-----|-----|
| WDI (input) | GPIO21 | GPIO23 |
| MR (output) | ESP32 EN pin | ESP32 EN pin |
| Window | 100ms | 100ms |

**Wiring**: GPIO → WDI. TPS3850 MR → ESP32 EN (active-low reset). If GPIO stops toggling for >100ms, TPS3850 pulls MR LOW → ESP32 hardware reset.

---

## 5. Power Distribution

```
72V Traction Battery
  │
  ├── Motor Controller (72V) ── Motor
  │
  ├── DC-DC Converter (72V→12V, CAN: 0x012)
  │     │
  │     ├── 12V Relay (GPIO27) ── 12V Accessory Bus
  │     │     ├── Signal lamps (via relays)
  │     │     ├── Mode bulbs (via relays)
  │     │     ├── Headlight
  │     │     └── MCP4725 VCC (5V regulated from 12V)
  │     │
  │     └── ESP32-S3 Dev Boards (5V regulated)
  │
  ├── SYNTREE EPS-C (steering) — internal power
  ├── SYNTREE SEB (brake) — internal power
  │
  └── 72V Gear Lines (via 1A fuse → relay COM terminals)

CAN_GND: Connect ground references if any CAN node uses isolated power.
```

---

## 6. Quick Reference — GPIO Summary

### RT ESP32-S3

```
GPIO 1  : Encoder A (rear motor)
GPIO 2  : Encoder B (rear motor)
GPIO 3  : Encoder A (front wheel, TBD)
GPIO 4  : CAN RX (low-level)
GPIO 5  : CAN TX (low-level)
GPIO 6  : Encoder B (front wheel, TBD)
GPIO 7  : (unused)
GPIO 8  : (unused)
GPIO 9  : Encoder A (rear left, TBD)
GPIO 10 : I2C SDA (IMU optional)
GPIO 11 : I2C SCL (IMU optional)
GPIO 12 : Encoder B (rear left, TBD)
GPIO 13 : Encoder A (rear right, TBD)
GPIO 14 : Encoder B (rear right, TBD)
GPIO 21 : WDT toggle
GPIO 36 : SPI SCK (MCP2515)
GPIO 37 : SPI MOSI (MCP2515)
GPIO 38 : SPI MISO (MCP2515)
GPIO 39 : SPI CS (MCP2515)
GPIO 40 : MCP2515 INT
```

### SYS ESP32-S3

```
GPIO 1  : ESTOP button (NC, active-low)
GPIO 2  : Brake lever (active-low)
GPIO 3  : Left turn switch
GPIO 4  : CAN RX (low-level)
GPIO 5  : CAN TX (low-level)
GPIO 6  : Right turn switch
GPIO 7  : Headlight switch
GPIO 10 : Throttle ADC read (ADC1_CH5)
GPIO 11 : MODE button
GPIO 12 : Gear D sense (TLP281 ch1)
GPIO 13 : Gear S sense (TLP281 ch2)
GPIO 14 : Gear R sense (TLP281 ch3)
GPIO 15 : I2C SDA (MCP4725 DAC)
GPIO 16 : I2C SCL (MCP4725 DAC)
GPIO 18 : Left turn lamp (relay)
GPIO 19 : Right turn lamp (relay)
GPIO 21 : Brake light (relay)
GPIO 22 : Headlight (relay)
GPIO 23 : WDT toggle
GPIO 25 : AUTO bulb (relay)
GPIO 26 : MANUAL bulb (relay)
GPIO 27 : 12V power relay
GPIO 32 : START button
GPIO 33 : Gear D output (relay ch1)
GPIO 34 : Gear S output (relay ch2)
GPIO 35 : Gear R output (relay ch3)
```

---

## 7. CAN Termination

Each bus requires a 120Ω resistor between CAN_H and CAN_L at both physical ends. Place one at each farthest node.

| Bus | Termination points |
|-----|-------------------|
| Low-Level | RT ESP32 + DC-DC converter (or farthest actuator) |
| High-Level | Jetson + RT ESP32 |

Without proper termination, signal reflections cause bit errors and bus-off states.
