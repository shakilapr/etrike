# E-Trike Wiring Reference

**Vehicle:** Electric tricycle, 72V system
**Controller:** ESP32-S3-**N16R8** (DevKitC-1) + Jetson Orin NX
**CAN:** 2× SN65HVD230 transceiver
**Document version:** 2026-06-11
**⚠️ OPI PSRAM:** GPIO 33–37 reserved (Octal PSRAM). Do not use.

---

## Wiring Principles

1. **72V circuits are galvanically isolated** — voltage dividers for input, optocouplers/relays for output. No 72V reaches the ESP32.
2. **CAN buses are terminated once at each physical end** — 120 Ω resistor between CAN_H and CAN_L at the two farthest nodes.
3. **All grounds star-connected** at a single chassis point. No ground loops through CAN shield.
4. **Signal and power wiring run in separate loom branches** — 72V, 12V, and 3.3V/5V paths physically separated by ≥50 mm where possible.
5. **Every connector is keyed and labelled** — no two connectors in the harness share the same shell.

---

## Power Distribution

```
72V Battery ──┬──► Motor Controller (high-current, fused 100A)
              ├──► 72V→12V DC-DC Converter ──► 12V Rail (lights, PSU relay)
              ├──► Gear Selector (72V switched lines)
              └──► E-Stop NC Relay (coil + contacts)

12V Rail ──┬──► Signal Lights L/R
           ├──► Mode Indicator Lights
           ├──► 12V→5V Buck Converter ──► ESP32 DevKit USB-C (or 5V pin)
           │                             ──► Jetson Orin NX (via barrel jack)
           └──► 12V→5V isolated ──► CAN Transceivers (SN65HVD230 Vcc)

5V Rail ──┬──► ESP32-S3 (USB-C or 5V pin)
          └──► Jetson Orin NX
```

### Fusing

| Circuit | Fuse | Notes |
|---------|------|-------|
| 72V main | 100 A ANL | At battery positive terminal |
| Motor controller | 80 A | Within 200 mm of controller |
| DC-DC converter input | 10 A blade | |
| 12V rail | 15 A blade | After DC-DC converter |
| 5V buck input | 3 A blade | |
| ESP32 USB | Self-protected | On-board 500 mA polyfuse |
| CAN transceivers | 500 mA | Per transceiver |

---

## CAN Bus Wiring

### Public Bus (Jetson ↔ ESP32-S3)

```
┌──────────────┐          ┌──────────────┐
│   ESP32-S3   │          │ Jetson Orin  │
│              │          │     NX       │
│ SN65HVD230   │          │ SocketCAN    │
│  TXD ── GPIO5│          │  (can0)      │
│  RXD ── GPIO4│          │              │
│  Vcc ── 5V   │          │              │
│  GND ── GND  │          │              │
│  CAN_H ──────┼──────────┼── CAN_H ─────┤
│  CAN_L ──────┼──────────┼── CAN_L ─────┤
│              │   120Ω   │              │
└──────────────┘          └──────────────┘
       │                        │
       └──── 120Ω ──────────────┘  (one terminator at each physical end)
```

| Signal | ESP32 GPIO | SN65HVD230 Pin | Wire Color | Notes |
|--------|------------|----------------|------------|-------|
| CAN0 TX | GPIO 5 | TXD (pin 1) | Yellow | |
| CAN0 RX | GPIO 4 | RXD (pin 4) | Green | |
| CAN_H | — | CANH (pin 7) | White/Orange | Twisted pair with CAN_L |
| CAN_L | — | CANL (pin 6) | White/Blue | Twisted pair with CAN_H |
| GND | GND | GND (pin 2) | Black | Signal ground reference |
| Vcc | 5V | VCC (pin 3) | Red | 5V from buck converter |
| RS | GND | RS (pin 8) | — | Slope control: tie to GND for high-speed mode |

**Termination:** 120 Ω, 1/4 W resistor between CAN_H and CAN_L at ESP32 end AND Jetson end.

**Cable:** 22 AWG twisted pair (CAN_H + CAN_L) with overall shield. Shield connected to chassis ground at ONE end only (ESP32 side). Max bus length at 500 kbit/s: 100 m.

### Private Bus (ESP32-S3 ↔ Syntree Actuators)

```
┌──────────────┐          ┌──────────────────────┐
│   ESP32-S3   │          │  Syntree EPS-C       │
│ SN65HVD230   │          │  (Steering)          │
│  TXD ── GPIO9│          │                      │
│  RXD ── GPIO8│          │  Syntree SEB         │
│  CAN_H ──────┼──────────┼── CAN_H ─┬── CAN_H   │
│  CAN_L ──────┼──────────┼── CAN_L ─┼── CAN_L   │
│              │   120Ω   │          │   120Ω    │
└──────────────┘          └──────────┴───────────┘
```

| Signal | ESP32 GPIO | SN65HVD230 Pin | Wire Color | Notes |
|--------|------------|----------------|------------|-------|
| CAN1 TX | GPIO 9 | TXD | Yellow/Stripe | |
| CAN1 RX | GPIO 8 | RXD | Green/Stripe | |
| CAN_H | — | CANH | White/Red | |
| CAN_L | — | CANL | White/Black | |

**Termination:** 120 Ω at ESP32 end (furthest from actuators if they have internal termination).

**⚠️ Syntree CAN output disabled by default** (`kSyntreeCanOutputEnabled = false`). Enable only after Phase 1 protocol verification.

---

## ESP32-S3 Pinout (Wiring Harness Side)

| GPIO | Signal | Dir | Wire Color | Connects To | Notes |
|------|--------|-----|------------|-------------|-------|
| — | 3.3V | — | — | Not used externally | |
| — | 5V | In | Red | 5V buck converter | Powers DevKit via 5V pin |
| — | GND | — | Black | Star ground point | Multiple GND pins — all tied |
| **CAN** | | | | | |
| 4 | CAN0 RX | In | Green | SN65HVD230 #1 RXD | Public bus |
| 5 | CAN0 TX | Out | Yellow | SN65HVD230 #1 TXD | Public bus |
| 8 | CAN1 RX | In | Green/Stripe | SN65HVD230 #2 RXD | Private bus |
| 9 | CAN1 TX | Out | Yellow/Stripe | SN65HVD230 #2 TXD | Private bus |
| **Throttle** | | | | | |
| 10 | Throttle ADC | In | Violet | Voltage divider → throttle signal | 0–5V scaled to 0–3.3V (2:1) |
| — | MCP4725 DAC | I2C | Gray | I2C addr 0x60, VDD=5V, VOUT=0–5V | Shares I2C0 bus with IMU (addr 0x68) |
| — | Analog switch control | Out | — | Mode relay / CD4053 select line | Controlled by `mode_task` |
| **Gear Selector (72V)** | | | | | |
| 6 | Gear D OUT | Out | Brown | Optocoupler → relay → 72V D line | |
| 13 | Gear D IN | In | Orange | TLP281 ch1 → GPIO, active-low | Internal pull-up on GPIO |
| 26 | Gear S IN | In | Orange/Stripe | TLP281 ch2 → GPIO, active-low | Internal pull-up on GPIO |
| 14 | Gear R IN | In | Orange/Red | TLP281 ch3 → GPIO, active-low | Internal pull-up on GPIO |
| 42 | Gear S OUT | Out | Brown/Stripe | Optocoupler → relay → 72V S line | (was 34 — OPI PSRAM) |
| 43 | Gear R OUT | Out | Brown/Red | Optocoupler → relay → 72V R line | (was 35 — OPI PSRAM) |
| **Safety** | | | | | |
| 1 | E-Stop | In | Red/White | NC E-Stop button → GND | Active-low, internal pull-up |
| 2 | Brake Lever | In | Blue/White | Brake lever switch → GND | Active-low, internal pull-up |
| 21 | Ext Watchdog | Out | Pink | External watchdog IC trigger | Toggled at 20 Hz |
| **Mode** | | | | | |
| 11 | Mode Switch | In | Blue | SPDT switch: pull-up (Manual) / GND (Auto) | |
| **Sensors** | | | | | |
| 3 | Encoder A | In | White | Encoder phase A | PCNT |
| 17 | Encoder B | In | White/Black | Encoder phase B | PCNT |
| 15 | Ultrasonic TRIG | Out | Purple | HC-SR04 TRIG | 10 µs pulse |
| 16 | Ultrasonic ECHO | In | Purple/White | HC-SR04 ECHO | Pulse width → distance |
| 19 | IMU SDA | I/O | Cyan | MPU6050 SDA | I2C, 4.7k pull-up to 3.3V |
| 20 | IMU SCL | Out | Cyan/White | MPU6050 SCL | I2C, 4.7k pull-up to 3.3V |
| **Lighting (12V via transistor)** | | | | | |
| 44 | 12V PSU | Out | Red/Blue | NPN → relay coil → 12V rail switch | (was 36 — OPI PSRAM conflict) |
| 38 | Mode: Auto | Out | Green/Yellow | NPN → 12V indicator lamp | |
| 39 | Mode: Manual | Out | Green/Red | NPN → 12V indicator lamp | |
| 40 | Signal Left | Out | Yellow/Black | NPN → 12V lamp | |
| 41 | Signal Right | Out | Yellow/Green | NPN → 12V lamp | |
| **Servo (fallback)** | | | | | |
| 18 | Servo PWM | Out | Orange/Black | Steering servo signal wire | 50 Hz, fallback only |

---

## Throttle Interface (0–5V Bidirectional)

```
Physical throttle grip
    │
    ├──► 10kΩ ──┬──► GPIO10 (ADC read, always active)
    │            │
    │          6.8kΩ
    │            │
    │           GND
    │
    └──► Analog switch (CD4053 or relay)
              │
              ├── Input A: Physical throttle wiper (Manual mode)
              ├── Input B: MCP4725 DAC 0–5V output (Auto mode)
              │
              └── Output ──► Motor controller 0–5V input
```

### MCP4725 DAC — 0–5V generation

```
ESP32-S3 I2C0 (GPIO19 SDA, GPIO20 SCL)
    │
    ├──► MCP4725 VDD  ── 5V (from buck converter)
    ├──► MCP4725 GND  ── GND
    ├──► MCP4725 SDA  ── GPIO19 (shared with IMU SDA)
    ├──► MCP4725 SCL  ── GPIO20 (shared with IMU SCL)
    ├──► MCP4725 A0   ── GND (address = 0x60)
    │
    └──► MCP4725 VOUT ──► Analog switch ──► Motor controller 0–5V input
```

- 12-bit resolution: 4096 steps from 0V to 5V (~1.22 mV/step)
- I2C address: 0x60 (A0 tied to GND)
- E-stop: I2C write 0x000 → VOUT = 0V
- No RC filter or level shifting needed — true analog output

### Voltage Divider (read side)

```
Throttle signal (0–5V) ──► 10kΩ ──┬──► GPIO10
                                    │
                                  6.8kΩ
                                    │
                                   GND
```

- Ratio: 6.8 / (10 + 6.8) = 0.405
- 5V input → 2.02V at GPIO (safe, within 0–3.3V range)
- 0V input → 0V at GPIO

---

## Gear Selector Interface (72V)

### Read side — TLP281 4-channel optoisolator module

```
                    72V SIDE                          3.3V SIDE
                    ────────                          ────────

72V Gear D ──► Rled ──► TLP281 ch1 LED ──► GND(72V)
                              ch1 photo ──► GPIO13 ──► 10k pull-up to 3.3V

72V Gear S ──► Rled ──► TLP281 ch2 LED ──► GND(72V)
                              ch2 photo ──► GPIO26 ──► 10k pull-up to 3.3V

72V Gear R ──► Rled ──► TLP281 ch3 LED ──► GND(72V)
                              ch3 photo ──► GPIO14 ──► 10k pull-up to 3.3V

                         ch4 — spare
```

- TLP281: 4-channel, 2500 Vrms isolation
- Rled: ~3.3 kΩ, 2 W (limits LED current at 72V: ~21 mA per channel)
- GPIO reads LOW = gear active (phototransistor ON when LED lit)
- Module provides onboard pull-ups; ESP32 internal pull-ups as backup

### Drive side — optocoupler + relay

```
GPIO6  ──► 220Ω ──► Opto LED ──► GND          (TLP281 ch4 or discrete PC817)
                     Opto transistor
                          │
                     Relay coil (72V) ──► 72V Gear D line

GPIO42 ──► 220Ω ──► Opto LED ──► GND          (discrete PC817)
                     Opto transistor
                          │
                     Relay coil (72V) ──► 72V Gear S line

GPIO43 ──► 220Ω ──► Opto LED ──► GND          (discrete PC817)
                     Opto transistor
                          │
                     Relay coil (72V) ──► 72V Gear R line
```

### Component Values

| Parameter | Value | Notes |
|-----------|-------|-------|
| TLP281 module | 4-ch optoisolator | Read side: ch1/2/3 for D/S/R, ch4 spare |
| Rled (72V→LED) | 3.3 kΩ, 2 W | 72V → ~21 mA LED current |
| Output optocoupler | PC817 | Drive side, one per gear |
| Output LED resistor | 220 Ω | 3.3V → ~15 mA |
| Relay | 72V coil, SPDT | Rated for gear solenoid load |
| Flyback diode | 1N4007 | Across each relay coil |

### Gear Truth Table

| D IN (GPIO13) | S IN (GPIO26) | R IN (GPIO14) | Gear |
|---------------|---------------|---------------|------|
| LOW | HIGH | HIGH | Drive (D) |
| HIGH | LOW | HIGH | S |
| HIGH | HIGH | LOW | Reverse (R) |
| HIGH | HIGH | HIGH | Neutral (N) |
| *any other* | — | — | Invalid → hold last, flag diag |

(Active-low: optoisolator inverts. 72V present → LED on → GPIO LOW.)

### Mode-dependent behavior

| Mode | Gear OUT |
|------|----------|
| Manual | Pass-through: gear OUT = gear IN |
| Auto (forward) | D OUT = HIGH, S OUT = LOW, R OUT = LOW |
| Auto (reverse) | D OUT = LOW, S OUT = LOW, R OUT = HIGH |
| Auto (stop) | All OUT = LOW (Neutral) |
| Estop | All OUT = LOW (Neutral) |

---

## Safety Wiring

### E-Stop Circuit (Two Independent Layers)

```
72V Battery (+) ──► 100A Fuse ──► E-Stop NC Relay (contacts) ──► Motor Controller (+)
                       │
                       └──► E-Stop Button (NC) ──► GPIO1 (ESP32)
                                   │
                                  GND

Layer 1 (Hardware): E-stop button ──► NC relay coil opens ──► motor power physically disconnected
Layer 2 (Firmware):  GPIO1 LOW ──► safety_task ──► mode_set(ESTOP) ──► 0V throttle + gear N + brake 100%
```

- E-stop button: NC (normally-closed) mushroom head, twist-to-release
- E-stop relay: NC contacts rated ≥100A at 72V DC
- Wire: 4 mm² (12 AWG) for high-current path to motor controller

### Brake Lever

```
GPIO2 ──► 10kΩ pull-up to 3.3V (internal)
     │
     └──► Brake lever NC switch ──► GND
```

- Lever pressed (NC open) → GPIO2 HIGH → brake released
- Lever released (NC closed) → GPIO2 LOW → brake engaged (fail-safe: broken wire = brake on)

---

## Lighting Wiring (12V)

```
12V Rail ──► 15A Fuse ──┬──► Signal Left lamp ──► NPN collector
                         │         │
                         │    NPN base ──► 1kΩ ──► GPIO40
                         │    NPN emitter ──► GND
                         │
                         ├──► Signal Right lamp ──► NPN collector
                         │         │
                         │    NPN base ──► 1kΩ ──► GPIO41
                         │    NPN emitter ──► GND
                         │
                         ├──► Mode: Auto lamp ──► NPN collector
                         │         │
                         │    NPN base ──► 1kΩ ──► GPIO38
                         │    NPN emitter ──► GND
                         │
                         └──► Mode: Manual lamp ──► NPN collector
                                   │
                              NPN base ──► 1kΩ ──► GPIO39
                              NPN emitter ──► GND
```

- NPN transistor: 2N2222 or similar (Ic ≥ 500 mA for typical 12V indicator lamp)
- Base resistor: 1 kΩ (3.3V → ~2.6 mA base current)
- Lamp power: 12V, ≤5W per lamp

### 12V PSU Control

```
GPIO44 ──► 1kΩ ──► NPN base
                    NPN collector ──► Relay coil ──► 12V
                    NPN emitter ──► GND
                    Relay NO contacts ──► Switch 12V rail to accessories
```

- Relay: SPDT, 12V coil, contacts rated ≥10A

---

## Sensor Wiring

### Encoder (Rear Wheel Speed)

```
Encoder Vcc (5V or 3.3V) ──► Red
Encoder GND               ──► Black
Encoder A                 ──► White   ──► GPIO3  (PCNT)
Encoder B                 ──► White/Black ──► GPIO17 (PCNT)
```

- Type: Incremental quadrature encoder
- Power: Match encoder Vcc to supply (3.3V preferred to avoid level shifting)
- Cable: Shielded 4-conductor, shield to GND at ESP32 end

### IMU (MPU6050) + MCP4725 DAC — shared I2C0 bus

```
I2C0 Bus (GPIO19 SDA, GPIO20 SCL)
    │
    ├──► 4.7kΩ pull-up to 3.3V (SDA)
    ├──► 4.7kΩ pull-up to 3.3V (SCL)
    │
    ├──► MPU6050  Vcc=3.3V  AD0=GND  addr=0x68
    │    ├─ SDA ── GPIO19 (Cyan)
    │    └─ SCL ── GPIO20 (Cyan/White)
    │
    └──► MCP4725  VDD=5V    A0=GND   addr=0x60
         ├─ SDA ── GPIO19 (shared)
         ├─ SCL ── GPIO20 (shared)
         └─ VOUT ──► Analog switch ──► Motor controller 0–5V
```

| Device | Address | Vcc | Notes |
|--------|---------|-----|-------|
| MPU6050 (IMU) | 0x68 | 3.3V | AD0 pin to GND |
| MCP4725 (DAC) | 0x60 | 5V | A0 pin to GND. 5V-tolerant I2C inputs |

### Ultrasonic (HC-SR04)

```
HC-SR04 Vcc  ──► 5V    ──► Red
HC-SR04 GND  ──► GND   ──► Black
HC-SR04 TRIG ──► GPIO15 ──► Purple
HC-SR04 ECHO ──► GPIO16 ──► Purple/White
```

- ECHO is 5V logic — use 1k/2k voltage divider to 3.3V OR confirm ESP32-S3 GPIO is 5V-tolerant
- Mount: Front of vehicle, facing forward, ≥300 mm above ground

---

## Connector Summary

| Connector | Pins | Mates With | Notes |
|-----------|------|------------|-------|
| ESP32 DevKit | 2×15 header | Custom IDC ribbon → breakout PCB | Label each GPIO at breakout |
| CAN0 (public) | 4-pin JST XH | SN65HVD230 module | TX, RX, 5V, GND |
| CAN1 (private) | 4-pin JST XH | SN65HVD230 module | TX, RX, 5V, GND |
| Throttle | 4-pin Weather-Pack | Throttle grip + motor controller | Signal, 5V, GND, mode-select |
| Gear IN | 6-pin Molex Mini-Fit | Gear selector harness | D/S/R in + D/S/R out |
| E-Stop | 2-pin bullet | E-stop button | NC contacts |
| Lights | 8-pin Molex | Lighting sub-harness | L, R, Auto, Manual, 12V, GND×3 |
| Encoder | 4-pin JST SH | Encoder | Vcc, GND, A, B |
| I2C (IMU) | 4-pin JST SH | IMU breakout | Vcc, GND, SDA, SCL |
| HC-SR04 | 4-pin dupont | HC-SR04 module | Vcc, GND, TRIG, ECHO |

---

## Grounding

```
                    ┌─────────────────────────┐
                    │  CHASSIS STAR GROUND     │
                    │  (single bolt, sanded    │
                    │   to bare metal)         │
                    └───────┬─────────────────┘
                            │
        ┌───────────────────┼───────────────────────┐
        │                   │                       │
   72V Battery (-)    12V DC-DC (-)           CAN Shield
        │                   │                 (ESP32 side only)
   Motor Controller    All 12V loads
   (-) terminal        (-) returns

                            │
                    ESP32 GND pins (all)
                            │
                    Sensor GND returns
                    (encoder, IMU, HC-SR04)
```

- **Single-point ground:** All grounds meet at one chassis bolt. No ground loops.
- **CAN shield:** Connected at ESP32 end only. Floating at far end.
- **72V return:** Separate heavy-gauge cable to battery negative. Does NOT pass through signal ground.
- **No shared 72V/signal paths:** The gear voltage dividers and optocouplers ensure 72V never reaches the ESP32 ground plane.

---

## Wire Gauge Quick Reference

| Circuit | Gauge (AWG) | mm² | Max Length |
|---------|-------------|-----|------------|
| 72V battery → motor | 4 | 21 | 2 m |
| 72V battery → DC-DC | 14 | 2.1 | 1 m |
| Motor controller phases | 6 | 13 | 1.5 m |
| 12V lighting | 18 | 0.82 | 3 m |
| CAN bus | 22 | 0.33 | 100 m max @ 500k |
| GPIO signals | 24 | 0.20 | 1 m |
| I2C (IMU) | 24 | 0.20 | 300 mm max |
| Encoder | 24 | 0.20 | 1 m |
| Ground returns | Match supply gauge | — | — |

---

*See also: [[achitecture]] for the system-level design, [[notes/can-hardware-basics]] for CAN physical setup, [[notes/can-troubleshooting]] for CAN debugging.*
