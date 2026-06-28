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
| 1 | CAN RX (low bus) | 4 | TWAI RX | SN65HVD230 RXD | RT commands, MTR feedback (0x206), SEB feedback |
| 2 | ESTOP Button | 1 | Digital, NC | Red mushroom → GND | 10k pull-up to 3.3V. LOW = ESTOP. Shared with MTR. |
| 3 | Brake Lever | 2 | Digital, NO | Lever switch → GND | Internal pull-up. LOW = pressed. → SEB via CAN 0x7B9. |
| 4 | START Button | 32 | Digital, NO | Green momentary → GND | Internal pull-up. LOW = pressed. |
| 5 | MODE Button | 11 | Digital, NO | Momentary → GND | Internal pull-up. LOW = pressed. → publishes CAN 0x110. |
| 6 | Left Turn Switch | 3 | Digital, NO | Handlebar momentary → GND | Internal pull-up |
| 7 | Right Turn Switch | 6 | Digital, NO | Handlebar momentary → GND | Internal pull-up |
| 8 | Headlight Switch | 7 | Digital, NO | Handlebar toggle → GND | Internal pull-up |

### 3.2 OUTPUTS (signals SYS drives to external hardware)

| # | Signal | GPIO | Type | Connected To | Notes |
|---|--------|------|------|-------------|-------|
| 1 | CAN TX (low bus) | 5 | TWAI TX | SN65HVD230 TXD | Telemetry to RT, mode cmd (0x110), brake cmd (0x7B9) to SEB |
| 2 | Left Turn Lamp | 18 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 3 | Right Turn Lamp | 19 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 4 | Brake Light | 21 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 5 | Headlight | 22 | Digital | Relay coil → 12V | Relay COM=12V, NO=lamp |
| 6 | AUTO Bulb | 25 | Digital | Relay coil → 12V | Relay COM=12V, NO=bulb |
| 7 | MANUAL Bulb | 26 | Digital | Relay coil → 12V | Relay COM=12V, NO=bulb |
| 8 | 12V Power Relay | 27 | Digital | Relay coil → 12V | Relay COM=12V bus, NO=accessories |
| 9 | WDT Toggle | 23 | Digital | TPS3850 WDI pin | Toggled at 20 Hz by safety_task |

### 3.3 Safety — Dashboard Buttons

| Button | GPIO | Type | Wiring |
|--------|------|------|--------|
| ESTOP | 1 | NC, active-low | Big red mushroom. GPIO → button → GND. 10k pull-up to 3.3V. |
| START | 32 | NO, active-low | Green momentary. GPIO → button → GND. Internal pull-up. |
| MODE | 11 | NO, active-low | Momentary. GPIO → button → GND. Internal pull-up. |
| Brake Lever | 2 | Active-low | GPIO → lever switch → GND. Internal pull-up. |

### 3.4 Signal Lights

| Signal | GPIO | Circuit |
|--------|------|---------|
| Left Turn Switch | 3 | Handlebar momentary, active-low. GPIO → switch → GND. Pull-up. |
| Right Turn Switch | 6 | Handlebar momentary, active-low |
| Headlight Switch | 7 | Handlebar toggle, active-low |
| Left Turn Lamp | 18 | GPIO → relay coil → 12V. Relay COM → 12V, NO → lamp. |
| Right Turn Lamp | 19 | GPIO → relay → 12V → lamp |
| Brake Light | 21 | GPIO → relay → 12V → lamp |
| Headlight | 22 | GPIO → relay → 12V → lamp |

### 3.5 Indicators & Power

| Signal | GPIO | Circuit |
|--------|------|---------|
| AUTO Bulb | 25 | GPIO → relay → 12V → bulb |
| MANUAL Bulb | 26 | GPIO → relay → 12V → bulb |
| 12V Relay | 27 | GPIO → relay coil → 12V. Relay COM → 12V bus, NO → accessories. |
| WDT Toggle | 23 | GPIO → TPS3850 WDI pin. Toggled at 20 Hz by safety_task. |

---

## 4. MTR STM32 — Input/Output Tables

Motor controller (EGAS Level 1). Sole owner of all throttle/gear I/O. Mode-gated via CAN `0x110` from SYS.

### 4.1 INPUTS (signals MTR reads from external hardware)

| # | Signal | Pin | Type | Connected To | Notes |
|---|--------|-----|------|-------------|-------|
| 1 | CAN RX (low bus) | TBD | CAN RX | CAN transceiver RXD | RT commands (0x204), SYS mode (0x110) |
| 2 | ESTOP Button | TBD | Digital, NC | Red mushroom → GND | Shared with SYS GPIO1. Hardwired Level 3 kill — zero CAN delay. |
| 3 | Throttle ADC | TBD | ADC | Voltage divider from 0–5V grip | 5V→3.3V via resistive divider |
| 4 | Gear D Sense | TBD | Digital | TLP281 ch1 output | 72V gear line → optoisolator → GPIO |
| 5 | Gear S Sense | TBD | Digital | TLP281 ch2 output | 72V gear line → optoisolator → GPIO |
| 6 | Gear R Sense | TBD | Digital | TLP281 ch3 output | 72V gear line → optoisolator → GPIO |

### 4.2 OUTPUTS (signals MTR drives to external hardware)

| # | Signal | Pin | Type | Connected To | Notes |
|---|--------|-----|------|-------------|-------|
| 1 | CAN TX (low bus) | TBD | CAN TX | CAN transceiver TXD | Speed telemetry (0x120), motor feedback (0x206) |
| 2 | MCP4725 SDA | TBD | I2C | MCP4725 DAC (addr 0x60) | Throttle 0–5V output. Pull-up to 3.3V. |
| 3 | MCP4725 SCL | TBD | I2C | MCP4725 DAC | I2C clock |
| 4 | Gear D Out | TBD | Digital | 4-ch relay module IN1 | 72V to ECU gear D wire |
| 5 | Gear S Out | TBD | Digital | 4-ch relay module IN2 | 72V to ECU gear S wire |
| 6 | Gear R Out | TBD | Digital | 4-ch relay module IN3 | 72V to ECU gear R wire |

### 4.3 Throttle — 0–5V (MTR-owned)

| Signal | Pin | Circuit |
|--------|-----|---------|
| Throttle Read | TBD (ADC) | 0–5V grip → **voltage divider** (2-resistor, ~5V→3.3V) → ADC pin |
| Throttle Output | I2C (TBD) | **MCP4725 DAC** (addr 0x60, VCC=5V). Output 0–5V → motor controller |

```
Throttle Grip (0-5V) ──┬── R1 (e.g. 1.8kΩ) ──┬── ADC pin
                        │                      │
                        └── R2 (e.g. 3.3kΩ) ──┴── GND

MCP4725: VCC=5V, GND, SDA, SCL, VOUT→Motor Controller
```

### 4.4 Gear — Bidirectional 72V (MTR-owned)

**Input** (read gear selector, galvanic isolation):

| Signal | Pin | Circuit |
|--------|-----|---------|
| Gear D Sense | TBD | 72V → **TLP281 optoisolator ch1** (input: 72V+resistor, output: GPIO + pull-up) |
| Gear S Sense | TBD | TLP281 optoisolator ch2 |
| Gear R Sense | TBD | TLP281 optoisolator ch3 |

```
72V Gear D ──┬── R (current limit, ~33kΩ/2W) ── TLP281 pin1 (anode)
             └── TLP281 pin2 (cathode) ── GND_72V
TLP281 pin4 (collector) ── GPIO ── 10k pull-up ── 3.3V
TLP281 pin3 (emitter) ── GND
```

**Output** (mimic 72V to ECU, mechanical relay isolation):

| Signal | Pin | Circuit |
|--------|-----|---------|
| Gear D Out | TBD | GPIO → 4-ch 5V relay module IN1 → **COM=72V** (via 1A fuse) → NO → ECU Gear D |
| Gear S Out | TBD | GPIO → relay IN2 → COM=72V → NO → ECU Gear S |
| Gear R Out | TBD | GPIO → relay IN3 → COM=72V → NO → ECU Gear R |

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

### 4.5 Mode-Gated Behavior

| Mode | CAN 0x110 | Throttle | Gear | Notes |
|------|:---------:|----------|------|-------|
| Manual | 0 | ADC → DAC pass-through | Sense → relay pass-through | Rider direct control |
| Auto | 1 | Follow CAN 0x204 `RT_MotorSpeed` | Follow CAN 0x204 `RT_Gear` | RT kinematics |
| ESTOP | 2 | DAC = 0 (cut throttle) | All relays off | Hardware + CAN kill |

---

## 5. External Watchdog

Each ESP32 has a TPS3850 (or equivalent) window watchdog IC.

| Pin | RT | SYS |
|-----|-----|-----|
| WDI (input) | GPIO21 | GPIO23 |
| MR (output) | ESP32 EN pin | ESP32 EN pin |
| Window | 100ms | 100ms |

**Wiring**: GPIO → WDI. TPS3850 MR → ESP32 EN (active-low reset). If GPIO stops toggling for >100ms, TPS3850 pulls MR LOW → ESP32 hardware reset.

---

## 6. Power Distribution

```
72V Traction Battery
  │
  ├── Motor Controller (72V) ── Motor (MCP4725 0–5V from MTR drives throttle input)
  │
  ├── DC-DC Converter (72V→12V, CAN: 0x012)
  │     │
  │     ├── 12V Relay (SYS GPIO27) ── 12V Accessory Bus
  │     │     ├── Signal lamps (via relays)
  │     │     ├── Mode bulbs (via relays)
  │     │     └── Headlight
  │     │
  │     ├── ESP32-S3 Dev Boards (5V regulated)
  │     └── STM32 Board (5V regulated)
  │           └── MCP4725 VCC (5V) — on MTR, provides 0–5V to motor controller
  │
  ├── steer-by-wire unit (steering) — internal power
  ├── brake-by-wire unit (brake) — internal power
  │
  └── 72V Gear Lines (via 1A fuse → relay COM terminals on MTR)

CAN_GND: Connect ground references if any CAN node uses isolated power.
```

---

## 7. Quick Reference — GPIO Summary

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
GPIO 1  : ESTOP button (NC, active-low) — shared with MTR
GPIO 2  : Brake lever (active-low) → SEB via CAN 0x7B9
GPIO 3  : Left turn switch
GPIO 4  : CAN RX (low-level)
GPIO 5  : CAN TX (low-level)
GPIO 6  : Right turn switch
GPIO 7  : Headlight switch
GPIO 11 : MODE button → publishes CAN 0x110
GPIO 18 : Left turn lamp (relay)
GPIO 19 : Right turn lamp (relay)
GPIO 21 : Brake light (relay)
GPIO 22 : Headlight (relay)
GPIO 23 : WDT toggle (20 Hz)
GPIO 25 : AUTO bulb (relay)
GPIO 26 : MANUAL bulb (relay)
GPIO 27 : 12V power relay
GPIO 32 : START button
```

### MTR STM32

```
ESTOP  : ESTOP button (NC, active-low) — shared with SYS GPIO1
ADC    : Throttle grip 0–5V (via voltage divider)
GPIO_x : Gear D sense (TLP281 ch1)
GPIO_x : Gear S sense (TLP281 ch2)
GPIO_x : Gear R sense (TLP281 ch3)
GPIO_x : Gear D output (relay ch1)
GPIO_x : Gear S output (relay ch2)
GPIO_x : Gear R output (relay ch3)
I2C    : MCP4725 DAC SDA (addr 0x60)
I2C    : MCP4725 DAC SCL
CAN_RX : CAN transceiver RXD
CAN_TX : CAN transceiver TXD
```

---

## 8. CAN Termination

Each bus requires a 120Ω resistor between CAN_H and CAN_L at both physical ends. Place one at each farthest node.

| Bus | Termination points |
|-----|-------------------|
| Low-Level | RT ESP32 + DC-DC converter (or farthest actuator) |
| High-Level | Jetson + RT ESP32 |

Without proper termination, signal reflections cause bit errors and bus-off states.
