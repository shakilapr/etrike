# E-Trike Wiring Harness Specification

Buildable wiring harness for the 4-node distributed electric trike. Two CAN buses (500 kbit/s), four compute nodes, five actuators, full lighting and control system.

**References:** `architecture.md`, `can-dictionary.md`, `docs/wiring.md`, `docs/high-voltage-isolation.md`
**Vehicle:** ~2.0 m wheelbase, ~0.8 m track, ~2.5 m overall length
**Voltage domains:** 72 V traction · 12 V accessory · 5 V DAC · 3.3 V logic

---

## 1. Harness Architecture

Eight logical sub-harnesses interconnect at three junction points. Each can be built and tested independently.

### 1.1 Sub-Harness Overview

| # | Name | From → To | Length | Contents |
|---|------|-----------|--------|----------|
| H1 | Dashboard/Handlebar | Handlebar controls → JP3 → JP1 | 1.2 m | SYS ESP32-S3, buttons, switches, mode bulbs, throttle grip |
| H2 | Power Distribution | Battery → DC-DC → fuse block (JP1) | 0.8 m | 72 V cables, DC-DC converter, ATO fuse block, ground bus |
| H3 | CAN Backbone | All CAN nodes, trunk line | 2.5 m trunk + drops | Two STP cables, 6 drops low bus, 2 drops high bus |
| H4 | MTR/Sensor | JP1 → rear motor area | 1.5 m | MTR STM32, MCP4725 #2, gear relays #2, TLP281 sense #2, rear encoder |
| H5 | Steering/Front | JP1 → steering column | 1.0 m | EPS-C power + CAN, front wheel encoder |
| H6 | Brake Module | JP1 → SEB location | 1.0 m | SEB power + CAN |
| H7 | Lighting Rear | JP1 → rear lamps | 2.0 m | Brake light, left/right turn signals |
| H8 | Chassis Ground | All ground points → star bus (JP1) | 2.0 m total | Ground bus, bonding straps, shield drain tie point |

### 1.2 Junction Points

```
JP1 — Center Power Bay (mid-chassis, under seat/enclosure)
  ├── 72 V battery input (Anderson SB50)
  ├── DC-DC converter (72 V → 12 V)
  ├── 12-circuit ATO/ATC fuse block
  ├── M6 ground bus bar (nickel-plated brass, ≥6 studs)
  ├── Chassis bond point (M6 bolt to frame rail)
  ├── CAN low bus termination (120 Ω at farthest end: DC-DC or JP1)
  └── Stub connections to H4, H5, H6, H7

JP2 — Center Enclosure, Upper Deck
  ├── Jetson Orin NX (power + high CAN)
  ├── RT ESP32-S3 (power + low CAN + high CAN SPI + encoders)
  ├── CAN high bus: Jetson ↔ RT (MCP2515)
  └── CAN high termination (120 Ω at RT end)

JP3 — Handlebar Junction
  ├── SYS ESP32-S3 board
  ├── Handlebar switch cluster (left: turn+headlight, right: turn+START)
  ├── Dashboard buttons (ESTOP, MODE)
  ├── Brake lever switch
  ├── Throttle grip (0–5 V)
  ├── Gear selector switch (72 V D/S/R)
  └── Mode indicator bulbs (AUTO, MANUAL)
```

### 1.3 Physical Layout

```
  ┌─ Handlebar ──────────────────────────────────────────┐
  │  SYS ESP32-S3, buttons, switches, bulbs, throttle     │  H1
  │  Gear selector (72 V D/S/R)                           │
  └────────────────────┬──────────────────────────────────┘
                       │  JP3 (Handlebar Junction)
                       │
  ┌─ Front Steering ───┼────── H5 ──────────────────────┐
  │  EPS-C, front enc. │                                  │
  └────────────────────┤  ┌── JP2 (Center Enclosure) ──┐  │
                       │  │ Jetson + RT ESP32-S3         │  │
  ┌─ Brake Module ─────┼──┤ MCP2515 + SN65HVD230 ×2     │  │
  │  SEB               │  │ CAN termination (both buses)  │  │
  └────────────────────┼──└──────────────────────────────┘  │
                       │           │
                  JP1 (Power Bay)  ├── H6) Brake
  ┌─ Power ───────────┤           ├── H4) MTR + Motor
  │  Battery → DC-DC →│           │
  │  ATO fuse block   │           ├── H7) Rear Lights
  └────────────────────┤           │
                       │           └── H8) Ground
  ┌─ Rear Motor ───────┼── H4 ──────────────────────────┐
  │  MTR STM32, 2×     │                                 │
  │  MCP4725, 2× gear  │                                 │
  │  relays, motor ctrl │                                 │
  │  rear enc.          │                                 │
  └─────────────────────┴─────────────────────────────────┘
```

### 1.4 EGAS Dual-Redundant Note

Both **SYS ESP32-S3** and **MTR STM32** have complete throttle + gear hardware:

| Function | SYS (Level 2 monitor) | MTR (Level 1 primary) |
|----------|----------------------|----------------------|
| Throttle ADC | GPIO10 (ADC1_CH5) | PA0 (ADC1_IN0) |
| MCP4725 DAC | I²C addr 0x60, SDA=GPIO15, SCL=GPIO16 | I²C addr 0x60, SDA=PB7, SCL=PB6 |
| Gear sense (TLP281) | D=GPIO12, S=GPIO13, R=GPIO14 | D=PB0, S=PB1, R=PB2 |
| Gear relay out | D=GPIO33, S=GPIO34, R=GPIO35 | D=PA3, S=PA4, R=PA5 |

The harness wires **both** paths to the motor controller. In AUTO mode, MTR drives; SYS monitors. In MANUAL mode, SYS passes through physical inputs. The motor controller's throttle input and gear inputs receive signals from whichever MCU is active. Use a **double-throw relay or diode-OR** to prevent both DACs driving the throttle line simultaneously if both MCUs are active.

---

## 2. Connector Selection

### 2.1 Connector Families Used

| Family | Pins | Current | IP Rating | Wire Range | Use |
|--------|------|---------|-----------|------------|-----|
| **Deutsch DT** | 2–6 | 13 A/contact | IP68 mated | 14–20 AWG | External power, signal, CAN drops |
| **Deutsch DTP** | 2 | 25 A/contact | IP68 mated | 10–16 AWG | EPS-C / SEB high-current 12 V |
| **Anderson SB50** | 2 | 50 A cont | IP20 | 6–10 AWG | 72 V battery disconnect |
| **TE Superseal 1.5** | 2–4 | 8 A/contact | IP67 mated | 16–22 AWG | Outdoor sensors, encoders, throttle |
| **Molex Mini-Fit Jr** | 2–6 | 9 A/contact | Unsealed | 18–24 AWG | Board-to-wire inside enclosures |
| **JST SM** | 2–4 | 3 A/contact | Unsealed | 22–28 AWG | Internal low-current sensor |

### 2.2 Deutsch DT Part Numbers (Primary External Connectors)

| Pins | Receptacle (female) | Plug (male) | Wedge | Contact 16–20 AWG | Contact 20–22 AWG |
|------|--------------------|-------------|-------|-------------------|-------------------|
| 2 | DT04-2P | DT06-2S | W2S | 0462-201-16141 | 0462-201-2031 |
| 3 | DT04-3P | DT06-3S | W3S | same | same |
| 4 | DT04-4P | DT06-4S | W4S | same | same |
| 6 | DT04-6P | DT06-6S | W6S | same | same |

**Deutsch DTP (high-current 12 V):**

| Pins | Receptacle | Plug | Contact 14–16 AWG |
|------|-----------|------|--------------------|
| 2 | DTP04-2P | DTP06-2S | 0460-202-1631 |

### 2.3 TE Superseal 1.5 (Outdoor Sensors)

| Pins | Receptacle (housing) | Plug (housing) | Contact 20–22 AWG |
|------|---------------------|----------------|-------------------|
| 2 | 1-967627-1 | 1-967628-1 | 929939-1 |
| 3 | 1-967628-1 | 1-967629-1 | 929939-1 |
| 4 | 1-967629-1 | 1-967630-1 | 929939-1 |

### 2.4 Molex Mini-Fit Jr (Board-Side, Internal)

| Pins | Receptacle Housing | Vertical Header | Contact 18–24 AWG |
|------|-------------------|----------------|-------------------|
| 2 | 39-01-2020 | 22-23-2021 | 44476-1111 |
| 4 | 39-01-2040 | 22-23-2041 | 44476-1111 |
| 6 | 39-01-2060 | 22-23-2061 | 44476-1111 |

### 2.5 Other Hardware

| Use | Specification | Notes |
|-----|--------------|-------|
| Battery disconnect | Anderson SB50 (gray housing) + SB50G boot | 6 AWG contacts |
| ATO fuse block | 12-circuit ATO/ATC, #10-32 input stud | Common automotive |
| Ground bus bar | M6 studs, nickel-plated brass, ≥6 position | Insulated standoff mount |
| ANL fuse holder | ANL format, 60 A rating | Main traction fuse |
| MEGA fuse holder (inline) | MEGA format, waterproof | DC-DC input |
| 3AG inline sealed holder | Littelfuse FHAC0001ZXJ | Per gear line 1 A fuse |
| Chassis bond bolt | M6 × 20 mm stainless, serrated washer | Green/yellow 6 AWG ring terminal |
| Cable ties | 100 mm, UV-resistant nylon | For bundle management |
| Adhesive cable tie mounts | 25 × 25 mm | For flat surfaces on frame |
| P-clips | Rubber-lined, for 10–25 mm bundles | Frame rail attachment every 300 mm |
| Grommets (panel) | Rubber, sized to panel thickness | All chassis pass-throughs |
| Heat-shrink labels | White-on-black, 6.4 mm diameter | Wire identification, both ends |

---

## 3. Wire Specification

All wire: stranded tinned copper, automotive-grade GXL/SXL, 105 °C minimum, 300 V (600 V for 72 V circuits).

### 3.1 Wire Gauge by Circuit

| Circuit | AWG | mm² | Insulation | Color | Current | Max Length | Drop |
|---------|-----|-----|-----------|-------|---------|-----------|------|
| 72 V battery main (motor + DC-DC) | 6 | 13.3 | SXL 600 V | Orange | 50 A cont | 1.0 m | 0.09 V |
| 72 V DC-DC input | 14 | 2.1 | GXL 600 V | Orange | 5 A | 0.8 m | 0.03 V |
| 72 V gear sense (×6: D/S/R ×2 paths) | 20 | 0.52 | GXL 600 V | Orange | <10 mA | 1.5 m | negl. |
| 72 V gear output (×6: D/S/R ×2 paths) | 18 | 0.82 | GXL 600 V | Orange | 1 A fused | 1.5 m | 0.03 V |
| 12 V main bus (DC-DC → fuse block) | 8 | 8.4 | GXL 300 V | Red | 42 A | 0.5 m | 0.04 V |
| 12 V EPS-C power | 12 | 3.3 | GXL 300 V | Red | 30 A peak | 1.0 m | 0.16 V |
| 12 V SEB power | 12 | 3.3 | GXL 300 V | Red | 20 A peak | 1.0 m | 0.11 V |
| 12 V Jetson power | 16 | 1.3 | GXL 300 V | Red | 3 A | 0.5 m | 0.02 V |
| 12 V RT ESP32-S3 | 18 | 0.82 | GXL 300 V | Red | 0.5 A | 0.5 m | 0.01 V |
| 12 V SYS ESP32-S3 | 18 | 0.82 | GXL 300 V | Red | 0.5 A | 1.2 m | 0.01 V |
| 12 V MTR STM32 | 18 | 0.82 | GXL 300 V | Red | 0.2 A | 1.5 m | 0.01 V |
| 12 V lighting bus (accessory relay out) | 14 | 2.1 | GXL 300 V | Red | 10 A peak | 2.0 m | 0.17 V |
| 12 V always-on (brake light, CAN) | 16 | 1.3 | GXL 300 V | Red/White | 3 A | 2.0 m | 0.08 V |
| 12 V individual lamp | 18 | 0.82 | GXL 300 V | Red | 2 A | 2.0 m | 0.09 V |
| 12 V relay coil | 22 | 0.33 | GXL 300 V | Yellow | 0.15 A | 1.2 m | 0.01 V |
| CAN trunk (low + high) | 22 | 0.33 | STP 300 V | Yellow/Green | signal | 2.5 m | — |
| CAN drop | 22 | 0.33 | STP 300 V | Yellow/Green | signal | ≤0.3 m | — |
| CAN_GND backbone | 18 | 0.82 | GXL 300 V | Black/White | return | 2.5 m | negl. |
| Throttle signal (shielded) | 22 | 0.33 | Shielded 300 V | White | <1 mA | 1.5 m | critical |
| Encoder signal (shielded) | 24 | 0.21 | Shielded 300 V | Gray / Gray+White | <10 mA | 2.0 m | negl. |
| Switch inputs (handlebar/dash) | 22 | 0.33 | GXL 300 V | Blue / Brown | <1 mA | 1.2 m | negl. |
| Ground return (12 V circuits) | match supply +1 | — | GXL 300 V | Black | per circuit | per circuit | per circuit |
| Ground return (72 V gear) | 18 | 0.82 | GXL 600 V | Black/Orange | 1 A | 1.5 m | negl. |
| Chassis bond | 6 | 13.3 | SXL 300 V | Green/Yellow | fault only | 0.5 m | — |

### 3.2 Color Code

| Color | Domain | Usage |
|-------|--------|-------|
| **Orange** | 72 V traction | All battery circuits, gear lines |
| **Red** | 12 V switched | Accessory rail (via GPIO27 relay) |
| **Red/White** | 12 V always-on | Brake light, MCU power, CAN transceivers |
| **Black** | Ground | All 12 V / 5 V / 3.3 V returns |
| **Black/Orange** | 72 V ground | 72 V gear line returns |
| **Black/White** | CAN_GND | CAN signal ground reference |
| **Yellow** | CAN_H | Twisted with Green |
| **Green** | CAN_L | Twisted with Yellow |
| **White** | Analog signal | Throttle 0–5 V |
| **Gray** | Encoder A | Quadrature channel A |
| **Gray/White** | Encoder B | Quadrature channel B |
| **Blue** | Switch input | Handlebar/dashboard switches |
| **Brown** | Switch common | Switch ground return |
| **Yellow (solid)** | Relay coil | GPIO → relay coil drive |
| **Green/Yellow** | Protective earth | Chassis bond only |

### 3.3 Shielded Cable Specifications

| Cable | Part Number | Pairs | Impedance | Use |
|-------|------------|-------|-----------|-----|
| CAN trunk, dual STP | Belden 3107A | 2 × STP | 120 Ω | Both CAN buses in one jacket (JP1↔JP2) |
| CAN drop, single STP | Belden 9841 | 1 × STP | 120 Ω | Individual node drops |
| Throttle signal | Belden 8451 | 1 × STP | 56 Ω | 0–5 V throttle (noise-critical) |
| Encoder quad | Belden 8772 | 2 × STP | 78 Ω | 2 channels + shield per encoder |

**Shield rules:**
- Drain wire connected to chassis ground at JP1 only (one point per shield).
- Far end (node side): shield drain cut flush, insulated with heat-shrink. Do NOT connect.
- CAN_GND (black/white) is a separate wire, NOT the shield drain.

---

## 4. CAN Bus Physical Layer

### 4.1 Topology

```
Low-Level CAN Bus (500 kbit/s) — 6 nodes, linear multidrop:

  [120R]──RT────SYS────MTR────EPS-C───SEB────DC-DC──[120R]
          |      |       |       |       |       |
         0.2m   0.3m    0.5m    0.5m    0.5m    0.5m   ← stub lengths
  ← 2.5 m trunk total →

High-Level CAN Bus (500 kbit/s) — 2 nodes, point-to-point:

  [120R]──RT (MCP2515)──────────────────Jetson Orin──[120R]
          |                                |
         0.1m                             0.3m         ← stub lengths
  ← 1.0 m trunk →
```

### 4.2 Termination

- 120 Ω, 1/4 W, 1% metal film resistor (Stackpole RNMF14FTD120R or equiv.)
- Soldered between CAN_H and CAN_L pins, encapsulated in adhesive-lined heat-shrink
- Housed in a Deutsch DT04-2P shell, labeled "TERM 120R"
- Low bus ends: RT ESP32-S3 and DC-DC converter
- High bus ends: RT ESP32-S3 (MCP2515 side) and Jetson Orin
- Verify: measure across CAN_H ↔ CAN_L at any node — must read ~60 Ω (two 120 Ω in parallel)

### 4.3 Node Drop Connector (4-Pin)

| Pin | Signal | Wire Color | Deutsch Contact |
|-----|--------|-----------|-----------------|
| 1 | CAN_H | Yellow | 0462-201-2031 (20–22 AWG) |
| 2 | CAN_L | Green | 0462-201-2031 |
| 3 | CAN_GND | Black/White | 0462-201-16141 (18 AWG) |
| 4 | Shield drain | Bare (tinned) | 0462-201-2031 |

### 4.4 CAN Node Protection (Every Node, Within 50 mm of Connector)

Each CAN drop requires, in this order from connector toward transceiver:

```
DT04-4P ── CMC ── TVS to GND ── SN65HVD230
```

**Common-mode choke:** TDK ACT45B-510-2P (51 µH, 2-line, 200 mA, 3 kΩ @ 100 MHz)
**TVS diode pair:** ON Semi NUP2105L (bidirectional, 24 V standoff, 44 V clamp, SOT-23)

Solder CMC and TVS to a small FR4 pigtail board, pot in heat-shrink, within 50 mm of the DT04-4P backshell.

**Nodes requiring protection: 8 total**
- Low bus: RT, SYS, MTR, EPS-C, SEB, DC-DC = 6
- High bus: RT(MCP2515), Jetson = 2

### 4.5 Stub Length Limits

At 500 kbit/s, maximum stub length is 0.3 m (bit time = 2 µs, round-trip propagation ≈ 0.3 m at 5 ns/m). All drops in this design are ≤0.3 m.

### 4.6 DC-DC Converter Baud Rate Warning

The DC-DC protocol specification references J1939 extended CAN at **250 kbps**, but the low bus operates at **500 kbps**. Before finalizing: verify the DC-DC converter's CAN interface can be configured for 500 kbps. If it is fixed at 250 kbps, either:
1. Place it on a separate CAN segment with a baud rate converter, or
2. Replace with a unit supporting 500 kbps operation.

---

## 5. Power Distribution

### 5.1 72 V Traction Distribution

```
72 V Battery ── SB50 Anderson ── [ANL 60 A] ── 72 V Bus Bar (M6 stud)
                                                    │
                             ┌──────────────────────┼──────────────────────┐
                             │                      │                      │
                         Motor Ctrl              DC-DC Conv            Gear Relays
                         (42 A cont)             (5 A, 72→12)          (1 A ×6 paths)
                                                 [MEGA 15 A]           [1 A 3AG ×6]
```

**Fuses (72 V side):**

| ID | Type | Rating | Holder | Protects |
|----|------|--------|--------|----------|
| F1 | ANL | 60 A | ANL fuse holder | Main traction (motor + DC-DC total) |
| F2 | MEGA | 15 A | Inline MEGA holder (waterproof) | DC-DC converter 72 V input |
| F3–F8 | 3AG fast-blow | 1 A | FHAC0001ZXJ (×6 inline sealed) | Each gear output line (D/S/R ×2 paths) |

### 5.2 12 V Distribution (ATO Fuse Block)

```
DC-DC Output (12 V, 8 AWG red)
  │
  └── 12-circuit ATO/ATC fuse block
       │
       ├── [ATO 40 A] ── 12 V Main Bus (8 AWG → junction distribution)
       ├── [ATO 30 A] ── EPS-C (12 AWG, H5)        + PTC RUEF300-2
       ├── [ATO 25 A] ── SEB (12 AWG, H6)           + PTC RUEF250-2
       ├── [ATO  5 A] ── Jetson Orin (16 AWG, JP2)  + PTC RUEF300
       ├── [ATO  3 A] ── RT ESP32-S3 (18 AWG, JP2)  + PTC BK60-020
       ├── [ATO  3 A] ── SYS ESP32-S3 (18 AWG, H1)  + PTC BK60-020
       ├── [ATO  3 A] ── MTR STM32 (18 AWG, H4)     + PTC BK60-010
       ├── [ATO 15 A] ── Lighting bus (14 AWG, → GPIO27 accy relay → H7) + PTC RUEF110-2
       └── [ATO  2 A] ── Always-on rail (16 AWG red/white → brake light, CAN xcvrs)
```

**PTC resettable fuse notes:**
- Install PTC inline on positive wire, within 150 mm of the fuse block output
- PTC trip current selected at ~1.8–2× hold current to avoid nuisance trips
- All PTCs rated for 12 VDC minimum; RUEF series rated 30 VDC

### 5.3 Voltage Drop Summary (Worst-Case)

| Path | Voltage | Current | Gauge | Round-trip Length | Resistance | Drop | % |
|------|---------|---------|-------|-------------------|-----------|------|---|
| Battery → Motor Controller | 72 V | 50 A | 6 AWG | 2.0 m | 3.6 mΩ | 0.18 V | 0.25% |
| DC-DC → Fuse block | 12 V | 42 A | 8 AWG | 1.0 m | 2.1 mΩ | 0.09 V | 0.75% |
| Fuse block → EPS-C | 12 V | 30 A | 12 AWG | 2.0 m | 10.6 mΩ | 0.32 V | 2.65% |
| Fuse block → SEB | 12 V | 20 A | 12 AWG | 2.0 m | 10.6 mΩ | 0.21 V | 1.77% |
| Fuse block → Jetson | 12 V | 3 A | 16 AWG | 1.0 m | 13.5 mΩ | 0.04 V | 0.34% |
| Fuse block → SYS (longest MCU) | 12 V | 0.5 A | 18 AWG | 2.4 m | 51.4 mΩ | 0.03 V | 0.21% |
| Fuse block → Rear lamp | 12 V | 2 A | 18 AWG | 4.0 m | 85.6 mΩ | 0.17 V | 1.43% |

All drops < 3% — compliant with SAE J1292.

### 5.4 12 V Accessory Relay (GPIO27)

SYS GPIO27 controls a 40 A automotive relay (TE 1-1393302-1 or equivalent):

```
GPIO27 ── 1 kΩ base R ── NPN (2N2222) ── relay coil ── 12 V
                                 │
                                 └── 1N4007 flyback (cathode to 12 V)

Relay COM  ← always-on 12 V rail (ATO 15 A → PTC RUEF110-2)
Relay NO   → accessory bus: headlight, turn signals, mode bulbs, position lights
Relay NC   → not connected
```

ESTOP behavior: GPIO27 goes high-impedance → NPN off → relay opens → accessory bus de-energized. Brake light is on the **always-on** rail (before this relay), so it stays powered in ESTOP.

---

## 6. Grounding Strategy

### 6.1 Star Ground Topology

```
                 ┌─────────────────────────────────┐
                 │  Ground Bus Bar (M6 studs, ≥6 pos) │
                 │  Nickel-plated brass              │
                 │  JP1 — Center Power Bay           │
                 │  Mounted on insulated standoffs   │
                 └──────────┬───────────────────────┘
                            │
       ┌────────────────────┼──────────────────────┐
       │           │        │        │              │
   DC-DC GND    Battery   RT GND   SYS GND    CAN_GND (one
   (8 AWG blk)  return    (18 AWG) (18 AWG)    point only,
                 (6 AWG                       18 AWG blk/wht)
                 blk/org)
       │                                    │
  12 V returns                         Shield drains
  (one per                               (one point,
  device)                                at chassis
                                         bond)
```

### 6.2 Ground Rules

1. **72 V traction ground is SEPARATE from 12 V ground.** Two physically separate bus bars, ≥50 mm clearance. 72 V return wires are black/orange stripe.
2. **Every 12 V device gets its own ground return wire** from the device to the 12 V ground bus bar. No daisy-chaining ground through chassis or other devices.
3. **CAN_GND (black/white, 18 AWG) connects to 12 V ground bus at exactly ONE point** (at JP1). Do NOT connect CAN_GND to chassis. The wire runs with the CAN backbone.
4. **Chassis bond:** Single 6 AWG green/yellow wire from ground bus bar to M6 stainless bolt on frame rail near JP1. Use serrated washer to bite through paint/powder-coat.
5. **Shield drains:** All shielded cable drain wires connect to chassis at exactly ONE point (the chassis bond bolt near JP1). Drain wire floats (unconnected, insulated) at the far end.
6. **EPS-C ECU housing** requires chassis ground per manufacturer spec — bond to same chassis bolt.

### 6.3 Ground Wire Gauge

| Return Path | Gauge | Notes |
|------------|-------|-------|
| 72 V battery return | 6 AWG | Black/orange, to 72 V ground bus |
| DC-DC 12 V return | 8 AWG | Black, to 12 V ground bus |
| EPS-C return | 12 AWG | Black |
| SEB return | 12 AWG | Black |
| Jetson return | 16 AWG | Black |
| RT, SYS, MTR return | 18 AWG | Black |
| Lighting return | 14 AWG | Black (common return for all rear lamps) |
| CAN_GND | 18 AWG | Black/white |
| Chassis bond | 6 AWG | Green/yellow |

---

## 7. Protection Additions

### 7.1 CAN Bus (Per Node)

| Component | Part Number | Qty per Node | Purpose |
|-----------|------------|-------------|---------|
| Common-mode choke | TDK ACT45B-510-2P | 1 | Suppress CAN bus radiated EMI |
| TVS diode pair | ON Semi NUP2105L | 1 | ESD + transient clamp on CAN_H / CAN_L |

**Total: 8 sets** (6 low bus nodes + 2 high bus nodes)

### 7.2 PTC Resettable Fuses (12 V Branches)

| Branch | PTC | Hold Current | Trip Current |
|--------|-----|-------------|--------------|
| EPS-C | Littelfuse RUEF300-2 | 3 A | 6 A |
| SEB | Littelfuse RUEF250-2 | 2.5 A | 5 A |
| Lighting bus | Littelfuse RUEF110-2 | 1.1 A | 2.2 A |
| Jetson | Littelfuse RUEF300 | 3 A | 6 A |
| RT ESP32-S3 | Bourns BK60-020 | 0.2 A | 0.4 A |
| SYS ESP32-S3 | Bourns BK60-020 | 0.2 A | 0.4 A |
| MTR STM32 | Bourns BK60-010 | 0.1 A | 0.2 A |

### 7.3 Throttle Signal Protection

At the ADC input (both SYS GPIO10 and MTR PA0):

```
Throttle signal ── 100 Ω series ──┬── ADC pin
(shielded)                        │
                                  ├── SMBJ5.0A TVS (5 V standoff, 8.6 V clamp) ── GND
                                  │
                                  └── 100 pF NPO ceramic ── GND
```

**TVS:** Littelfuse SMBJ5.0A (unidirectional, DO-214AA). Install at both SYS and MTR throttle ADC inputs.

### 7.4 Encoder Input Protection

At each encoder input on RT ESP32-S3:

```
Encoder A/B ── 330 Ω series ──┬── GPIO
                               │
                               ├── PESD5V0S2UT TVS array ── GND
                               │
                               └── 100 pF ── GND
```

**TVS:** Nexperia PESD5V0S2UT (2-channel, SOT-23). One array per encoder (2 channels = A+B). Total: 4 arrays (rear motor, front wheel, rear left, rear right — rear left/right are TBD sensors).

### 7.5 Relay Flyback Diodes

Every relay coil MUST have a flyback diode soldered directly across the coil terminals (cathode to +12 V side, anode to ground/switched side).

**Diode:** 1N4007 (1 A, 1000 V). One per relay coil.

**Relays requiring flyback diodes (total 13):**

| Location | Qty | Purpose |
|----------|-----|---------|
| MTR gear output | 3 | D, S, R relay drivers (PA3/4/5) |
| SYS gear output | 3 | D, S, R relay drivers (GPIO33/34/35) |
| SYS lighting | 6 | Left turn, Right turn, Brake, Headlight, AUTO bulb, MANUAL bulb |
| SYS 12 V accessory | 1 | GPIO27 main accessory relay |

### 7.6 Lamp Circuit TVS Protection

At each lamp connector, bidirectional TVS across 12 V and GND:

```
12 V ──┬── Lamp ── GND
       │
       └── SMCJ18A TVS (18 V standoff, 29.2 V clamp) ── GND
```

**TVS:** Littelfuse SMCJ18A (unidirectional, SMC/DO-214AB). One per lamp: brake light, left turn, right turn, headlight = 4 total.

### 7.7 72 V Gear Line Protection (Per Existing Design)

Already specified in `docs/high-voltage-isolation.md`. Confirmed for harness:

| Per gear line (×6 paths: D/S/R ×2 MCUs) | Part |
|------------------------------------------|------|
| 1 A fast-blow fuse (inline sealed holder) | Littelfuse FHAC0001ZXJ |
| Bidirectional TVS (90 V standoff, 146 V clamp) | Littelfuse SMCJ90CA |
| TLP281 optoisolator (input sense) | Toshiba TLP281-4 (quad) or TLP281 (single) ×3 |
| Input current-limit resistor (~33 kΩ, 2 W) | Per TLP281 LED anode |

---

## 8. Routing & Mechanical

### 8.1 Conduit / Sleeving

| Harness | Sleeve Type | Diameter | Color |
|---------|------------|----------|-------|
| H1 Dashboard | Braided PET sleeve | 19 mm | Black |
| H2 Power (72 V) | Split loom, UV-resistant | 25 mm | Orange |
| H2 Power (12 V) | Split loom | 19 mm | Black |
| H3 CAN Backbone | Braided PET sleeve | 12 mm | Black |
| H4 MTR/Sensor | Braided PET sleeve | 19 mm | Black |
| H5 Steering | Braided PET sleeve | 16 mm | Black |
| H6 Brake | Braided PET sleeve | 16 mm | Black |
| H7 Lighting | Split loom | 12 mm | Black |
| H8 Ground | Split loom | 10 mm | Black |

### 8.2 Separation Rules

| Rule | Distance |
|------|----------|
| 72 V power cables from CAN/signal cables | ≥100 mm |
| 12 V high-current (EPS-C, SEB) from signal cables | ≥50 mm |
| Crossing angle (when must cross) | 90° |
| 72 V and signal in same conduit | **NEVER** |

### 8.3 Routing Layout (Frame Cross-Section)

```
Left Frame Rail:                    Right Frame Rail:
  CAN backbone (H3, 12 mm sleeve)     72 V gear lines (orange, split loom)
  Sensor cables (throttle, enc.)       Motor controller power (6 AWG orange)
  Ground bus wires (black)            DC-DC input (14 AWG orange)
  12 V low-current (18 AWG red)       12 V high-current (EPS-C/SEB)
```

### 8.4 Grommets & Pass-Throughs

Every chassis pass-through requires a rubber grommet sized to the panel thickness:

| Location | Panel Thickness | Bundle Diameter |
|----------|----------------|-----------------|
| Center enclosure (all cables) | 1.2 mm | 25 mm |
| Rear bulkhead (H4, H7) | 1.0 mm | 19 mm |
| Steering column (H5) | 1.5 mm | 16 mm |
| Brake module bracket (H6) | 1.0 mm | 16 mm |

### 8.5 Strain Relief & Service Loops

- Every connector with >150 mm unsupported wire: cable tie anchor within 50 mm of connector backshell
- Adhesive-backed mounts on flat surfaces; screw-down P-clips on frame rails
- Main trunk secured every 300 mm with rubber-lined P-clips
- 150 mm service loop at every connector (200 mm inside enclosures, 300 mm at battery)
- Coil excess and zip-tie to nearest frame point

### 8.6 Wire Labeling

Every wire gets a heat-shrink label on **both ends**, within 20 mm of the terminal.

**Format:** `H<harness>-J<connector>-<pin>`

Examples:
- `H3-J10-1` = CAN backbone, drop connector J10, pin 1 (CAN_H)
- `H1-J4-2` = Dashboard, ESTOP connector J4, pin 2
- `H4-J18-3` = MTR harness, encoder connector J18, pin 3 (ChA)

Use an industrial heat-shrink label maker with white-on-black 6.4 mm tubing.

---

## 9. Sub-Harness Bills of Materials

### H1 — Dashboard / Handlebar (1.2 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 18 AWG red (GXL) | 4 m | GXL-18-RED | SYS power, bulb power |
| Wire, 18 AWG black (GXL) | 4 m | GXL-18-BLK | Ground returns |
| Wire, 22 AWG blue | 4 m | GXL-22-BLU | Switch signals (×5) |
| Wire, 22 AWG brown | 2 m | GXL-22-BRN | Switch common/return |
| Wire, 22 AWG yellow | 3 m | GXL-22-YEL | Relay coil drives (×6 + GPIO27) |
| Wire, 22 AWG white shielded | 1.5 m | Belden 8451 | Throttle grip signal |
| Wire, 20 AWG orange (×3) | 4 m | GXL-20-ORG | Gear sense TLP281 inputs (D/S/R) |
| Wire, 18 AWG orange (×3) | 2 m | GXL-18-ORG | Gear relay outputs to motor controller |
| Shielded pair, 22 AWG | 1.5 m | Belden 9841 | CAN drop to SYS |
| DT04-4P + contacts | 2 | Deutsch | Handlebar switch connectors J2 (left), J3 (right) |
| DT06-6S + contacts | 1 | Deutsch | Dashboard buttons + bulbs J4 |
| DT04-4P + contacts | 1 | Deutsch | Power/CAN input to SYS area J5 |
| Molex 39-01-2060 + 44476-1111 | 2 | Mini-Fit Jr 6-pin | SYS board I/O breakout J1a, J1b |
| Molex 39-01-2040 + 44476-1111 | 1 | Mini-Fit Jr 4-pin | SYS board power J1c |
| TE Superseal 1-967628-1 (3-pin) | 1 | Superseal 1.5 | Throttle grip connector J15 |
| SMBJ5.0A TVS | 1 | Littelfuse | Throttle signal protection (at SYS ADC) |
| NUP2105L + ACT45B-510-2P | 1 set | CAN protection | SYS CAN node |
| 1N4007 flyback diode | 7 | — | SYS relay coils (gear×3, lights×3, GPIO27) |
| TLP281 optoisolator | 3 | Toshiba | SYS gear sense (D/S/R) |
| SMCJ90CA TVS | 3 | Littelfuse | SYS gear line TVS (72 V) |
| 3AG 1A fuse + FHAC0001ZXJ | 3 | Littelfuse | SYS gear output fuses |
| SMCJ18A TVS | 1 | Littelfuse | Bulb power protection |
| Braided PET sleeve, 19 mm | 1.5 m | — | Harness wrap |
| Heat-shrink labels | 30 | — | Wire markers, both ends |

### H2 — Power Distribution (0.8 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 6 AWG orange (SXL) | 2 m | SXL-6-ORG | Battery → bus → motor controller |
| Wire, 6 AWG black/orange (SXL) | 2 m | SXL-6-BLK/ORG | 72 V return |
| Wire, 14 AWG orange (GXL) | 1 m | GXL-14-ORG | DC-DC input |
| Wire, 8 AWG red (GXL) | 1.5 m | GXL-8-RED | DC-DC → fuse block |
| Wire, 8 AWG black (GXL) | 1.5 m | GXL-8-BLK | DC-DC 12 V return |
| Anderson SB50 + SB50G boot | 1 pair | Anderson | Battery disconnect |
| ANL fuse holder | 1 | — | Main 60 A fuse |
| MEGA inline fuse holder | 1 | — | DC-DC 15 A fuse, waterproof |
| 12-circuit ATO/ATC fuse block | 1 | — | #10-32 input stud |
| M6 ground bus bar, ≥6 stud | 1 | — | Nickel-plated brass, insulated standoffs |
| ANL 60 A fuse | 1 | — | Main traction |
| MEGA 15 A fuse | 1 | — | DC-DC input |
| ATO fuses (40,30,25,5,3×3,15,2 A) | 10 | Assorted | Per branch |
| DT04-2P + contacts | 1 | Deutsch | DC-DC 72 V input J7 |
| DTP04-2P + contacts | 1 | Deutsch DTP | DC-DC 12 V output J8 |
| DT04-6P + contacts | 1 | Deutsch | 12 V distribution output J9 |
| Split loom, 25 mm orange | 1 m | — | 72 V conduit |
| Split loom, 19 mm black | 1 m | — | 12 V conduit |
| M6 ring terminals (6 AWG) | 4 | — | Ground & power terminations |
| Heat-shrink labels | 20 | — | Wire markers, both ends |

### H3 — CAN Backbone (2.5 m trunk + 6 × 0.3 m drops)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Dual STP, 22 AWG | 3 m | Belden 3107A | Low + high CAN in one jacket |
| Single STP, 22 AWG | 2 m | Belden 9841 | Individual node drops |
| Wire, 18 AWG black/white | 5 m | GXL-18-BLK/WHT | CAN_GND backbone |
| DT04-4P + 0462-201-2031 | 6 | Deutsch | Low bus node drops (J10a–f): RT, SYS, MTR, EPS-C, SEB, DC-DC |
| DT04-4P + 0462-201-2031 | 2 | Deutsch | High bus node drops (J10g–h): RT-MCP2515, Jetson |
| DT04-2P + 0462-201-2031 | 2 | Deutsch | Terminator enclosures (J11, J12) — one per bus |
| 120 Ω 1/4W 1% resistor | 2 | RNMF14FTD120R | Termination (×2 buses) |
| NUP2105L + ACT45B-510-2P | 8 sets | CAN protection | One per node |
| FR4 pigtail boards | 8 | Custom (15×10 mm) | Solder CMC + TVS |
| Braided PET sleeve, 12 mm | 3 m | — | Trunk wrap |
| Heat-shrink labels | 30 | — | Wire markers, both ends |

### H4 — MTR / Sensor (1.5 m, Rear Motor Area)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 18 AWG red | 2 m | GXL-18-RED | MTR STM32 power |
| Wire, 18 AWG black | 2 m | GXL-18-BLK | MTR ground return |
| Wire, 14 AWG red | 1 m | GXL-14-RED | 12 V to relay module COM |
| Wire, 18 AWG orange (×3) | 4 m | GXL-18-ORG | Gear D/S/R relay outputs |
| Wire, 20 AWG orange (×3) | 4 m | GXL-20-ORG | Gear D/S/R TLP281 sense inputs |
| Shielded pair, 22 AWG | 0.3 m | Belden 9841 | CAN drop |
| Shielded pair, 22 AWG | 2 m | Belden 8451 | Throttle signal (MTR ADC) |
| Shielded quad, 24 AWG | 2 m | Belden 8772 | Rear motor encoder |
| DT04-4P + contacts | 1 | Deutsch | CAN drop J14 |
| TE Superseal 1.5 3-pin | 1 | 1-967628-1 | Throttle grip input J15 (MTR path) |
| TE Superseal 1.5 3-pin | 1 | 1-967628-1 | MCP4725 output → motor controller J16 |
| DT04-4P + contacts | 1 | Deutsch | Motor controller gear input J17 |
| TE Superseal 1.5 4-pin | 1 | 1-967629-1 | Rear motor encoder J18 |
| DT04-2P + contacts (×3) | 3 | Deutsch | TLP281 sense input J19 D/S/R |
| DT04-2P + contacts (×3) | 3 | Deutsch | Relay gear output J20 D/S/R |
| Molex 39-01-2040 + 44476-1111 | 1 | Mini-Fit Jr | MTR board power J13a |
| Molex 39-01-2060 + 44476-1111 | 2 | Mini-Fit Jr | MTR board signal J13b |
| 1A 3AG fuse + FHAC0001ZXJ | 3 | Littelfuse | MTR gear output fuses |
| SMCJ90CA TVS | 3 | Littelfuse | MTR gear line TVS |
| TLP281 optoisolator | 3 | Toshiba | MTR gear sense |
| 1N4007 flyback diode | 3 | — | MTR gear relay coils |
| SMBJ5.0A TVS | 1 | Littelfuse | MTR throttle ADC protection |
| PESD5V0S2UT TVS | 1 | Nexperia | Rear motor encoder protection |
| NUP2105L + ACT45B-510-2P | 1 set | CAN protection | MTR CAN node |
| Braided PET sleeve, 19 mm | 2 m | — | Harness wrap |

### H5 — Steering / Front (1.0 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 12 AWG red | 1.5 m | GXL-12-RED | EPS-C 12 V power |
| Wire, 12 AWG black | 1.5 m | GXL-12-BLK | EPS-C ground return |
| Shielded pair, 22 AWG | 1.5 m | Belden 9841 | CAN drop |
| Shielded quad, 24 AWG | 1.5 m | Belden 8772 | Front wheel encoder (TBD sensor) |
| DTP04-2P + 0460-202-1631 | 1 | Deutsch DTP | EPS-C power J21a |
| DT04-4P + 0462-201-2031 | 1 | Deutsch | EPS-C CAN J21b |
| TE Superseal 1.5 4-pin | 1 | 1-967629-1 | Front encoder J22 |
| RUEF300-2 PTC | 1 | Littelfuse | EPS-C overcurrent protection |
| PESD5V0S2UT TVS | 1 | Nexperia | Front encoder protection |
| NUP2105L + ACT45B-510-2P | 1 set | CAN protection | EPS-C CAN node |
| Braided PET sleeve, 16 mm | 1.5 m | — | Harness wrap |

### H6 — Brake Module (1.0 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 12 AWG red | 1.5 m | GXL-12-RED | SEB 12 V power |
| Wire, 12 AWG black | 1.5 m | GXL-12-BLK | SEB ground return |
| Shielded pair, 22 AWG | 1.5 m | Belden 9841 | CAN drop |
| DTP04-2P + 0460-202-1631 | 1 | Deutsch DTP | SEB power J23a |
| DT04-4P + 0462-201-2031 | 1 | Deutsch | SEB CAN J23b |
| RUEF250-2 PTC | 1 | Littelfuse | SEB overcurrent protection |
| NUP2105L + ACT45B-510-2P | 1 set | CAN protection | SEB CAN node |
| Braided PET sleeve, 16 mm | 1.5 m | — | Harness wrap |

### H7 — Lighting Rear (2.0 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 14 AWG red | 2 m | GXL-14-RED | Lighting bus from accy relay |
| Wire, 18 AWG red | 5 m | GXL-18-RED | Individual lamp feeds (×3) |
| Wire, 18 AWG black | 5 m | GXL-18-BLK | Lamp ground returns |
| Wire, 16 AWG red/white | 2 m | GXL-16-RED/WHT | Brake light (always-on rail) |
| DT04-2P + 0462-201-16141 | 3 | Deutsch | Lamp connectors J24 (left turn), J25 (right turn), J26 (brake) |
| SMCJ18A TVS | 3 | Littelfuse | Per-lamp transient protection |
| Split loom, 12 mm | 2.5 m | — | Harness wrap |

### H8 — Chassis Ground (2.0 m Total)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 6 AWG green/yellow (SXL) | 1 m | SXL-6-GRN/YEL | Ground bus → chassis bolt |
| Wire, 6 AWG black (SXL) | 1 m | SXL-6-BLK | 72 V ground bus link |
| M6 stainless bolt + nut + serrated washer | 1 set | — | Chassis bond point |
| M6 ring terminals (6 AWG) | 4 | — | Ground cable terminations |
| Split loom, 10 mm | 2 m | — | Individual ground wire protection |

---

## 10. Reference Tables

### 10.1 Wire Ampacity (GXL/SXL, 105 °C, in-bundle derated by 30%)

| AWG | mm² | Ampacity (chassis) | Ampacity (power dist.) | Ω/km |
|-----|-----|-------------------|----------------------|------|
| 6 | 13.3 | 120 A | 55 A | 1.3 |
| 8 | 8.4 | 85 A | 40 A | 2.1 |
| 10 | 5.3 | 65 A | 30 A | 3.3 |
| 12 | 3.3 | 50 A | 20 A | 5.3 |
| 14 | 2.1 | 40 A | 15 A | 8.5 |
| 16 | 1.3 | 25 A | 10 A | 13.5 |
| 18 | 0.82 | 18 A | 7 A | 21.4 |
| 20 | 0.52 | 11 A | 4 A | 34.1 |
| 22 | 0.33 | 7 A | 3 A | 54.3 |
| 24 | 0.21 | 3.5 A | 2 A | 86.4 |

Per SAE J1128. Power distribution column assumes 30% bundle derating.

### 10.2 Connector Family Comparison

| Family | Contact Res. | Mating Cycles | Seal | Temp Range | Cost/Conn. |
|--------|-------------|--------------|------|------------|-----------|
| Deutsch DT | 8 mΩ | 100+ | IP68 | −55 to +125 °C | $8–12 |
| Deutsch DTP | 5 mΩ | 100+ | IP68 | −55 to +125 °C | $10–15 |
| TE Superseal 1.5 | 10 mΩ | 50+ | IP67 | −40 to +130 °C | $3–6 |
| Molex Mini-Fit Jr | 10 mΩ | 30 | Unsealed | −40 to +105 °C | $1–3 |
| JST SM | 20 mΩ | 20 | Unsealed | −25 to +85 °C | $0.50–1 |
| Anderson SB50 | 0.5 mΩ | 1000+ | IP20 | −20 to +105 °C | $8–12 |

### 10.3 CAN Message → Physical Node Wiring

**Low-Level CAN Bus (500 kbit/s):**

| CAN ID | Name | Sender (MCU) | Sender (Harness Location) | Receivers (MCUs) |
|--------|------|-------------|--------------------------|-------------------|
| 0x001 | SAFETY_ESTOP | SYS or RT | H1 (dash) or JP2 (center) | All low bus nodes |
| 0x011 | SYS_SAFETY_STS | SYS | H1 (dash) | RT (JP2) |
| 0x012 | SYS_DCDC_CMD | SYS | H1 (dash) | DC-DC converter (JP1) |
| 0x110 | SYS_MODE_CMD | SYS | H1 (dash) | RT (JP2) |
| 0x120 | SYS_THROTTLE_STS | MTR | H4 (rear motor) | RT (JP2) |
| 0x169 | VCU_SES_REQ | RT | JP2 (center) | EPS-C (H5, front steering) |
| 0x201 | SES_STATUS | EPS-C | H5 (front steering) | RT (JP2) |
| 0x202 | SES_ErrInfo | EPS-C | H5 (front steering) | RT (JP2) |
| 0x203 | SES_Version | EPS-C | H5 (front steering) | RT (JP2) |
| 0x204 | RT_DRIVE_CMD | RT | JP2 (center) | MTR (H4), SYS (H1) |
| 0x205 | RT_BRAKE_CMD | RT | JP2 (center) | SYS (H1) |
| 0x206 | MTR_MOTOR_FBK | MTR | H4 (rear motor) | SYS (H1), RT (JP2) |
| 0x302 | HOST_LIGHT_CMD | RT (fwd) | JP2 (center) | SYS (H1) |
| 0x600 | SYS_DIAG_RPT | SYS | H1 (dash) | RT (JP2) |
| 0x6FA | SES_Test | EPS-C | H5 (front steering) | RT (JP2) |
| 0x6FB | SEB_Test | SEB | H6 (brake) | SYS (H1) |
| 0x721 | SEB_STATUS | SEB | H6 (brake) | SYS (H1) |
| 0x731 | SEB_ErrInfo | SEB | H6 (brake) | SYS (H1) |
| 0x741 | SEB_Version | SEB | H6 (brake) | SYS (H1) |
| 0x7B9 | VCU_SEB_REQ | SYS (MANUAL/ESTOP) | H1 (dash) | SEB (H6) |
| 0x7FD | RT_HEARTBEAT | RT | JP2 (center) | SYS (H1) |
| 0x7FE | SYS_HEARTBEAT | SYS | H1 (dash) | RT (JP2) |

**High-Level CAN Bus (500 kbit/s):**

| CAN ID | Name | Sender | Receiver |
|--------|------|--------|----------|
| 0x300 | HOST_DRIVE_CMD | Jetson | RT |
| 0x301 | HOST_BRAKE_REQ | Jetson | RT |
| 0x302 | HOST_LIGHT_CMD | Jetson | RT |
| 0x400 | HOST_OBSTACLE_DIST | Jetson | RT |
| 0x7FC | JETSON_HEARTBEAT | Jetson | RT |
| 0x001 | SAFETY_ESTOP | RT or Jetson | Both |
| 0x011 | SYS_SAFETY_STS | RT (fwd) | Jetson |
| 0x120 | SYS_THROTTLE_STS | RT (fwd) | Jetson |
| 0x206 | MTR_MOTOR_FBK | RT (fwd) | Jetson |
| 0x210 | RT_STATE_RPT | RT | Jetson |
| 0x220 | RT_PID_RPT | RT | Jetson |
| 0x310 | STEER_DIAG | RT | Jetson |
| 0x311 | BRAKE_DIAG | RT | Jetson |
| 0x600 | SYS_DIAG_RPT | RT (fwd) | Jetson |
| 0x7FD | RT_HEARTBEAT | RT | Jetson |

---

## Appendix A: Assembly Checklist

- [ ] All wires cut to length with 150 mm extra for service loops
- [ ] Continuity test on every wire BEFORE terminating (verify no internal breaks)
- [ ] Deutsch DT/DTP contacts crimped with proper 4-indent crimp tool (Deutsch HDT-48-00 or equiv.)
- [ ] Molex Mini-Fit Jr contacts crimped with proper tool (Molex 63811-8700)
- [ ] TE Superseal contacts crimped with open-barrel tool (TE 1490200-1)
- [ ] Anderson SB50 contacts crimped or soldered to 6 AWG; torque connector screws to spec
- [ ] Flyback diode polarity verified on all 13 relay coils (cathode → +12 V, anode → GPIO/NPN)
- [ ] CAN termination: measure 60 Ω ±5% between CAN_H and CAN_L at any node with both terminators installed
- [ ] CAN CMC + TVS soldered on FR4 pigtail within 50 mm of each node connector
- [ ] CAN_GND wired to 12 V ground bus at exactly ONE point (JP1)
- [ ] Shield drains bonded to chassis at exactly ONE point (JP1 chassis bolt)
- [ ] 72 V circuits → chassis: must measure OPEN (>10 MΩ at 500 V)
- [ ] 12 V ground bus → chassis: must measure CLOSED (<1 Ω)
- [ ] CAN_GND → 12 V ground bus: CLOSED at JP1 only
- [ ] 100 mm minimum separation between 72 V power and signal/CAN verified along entire route
- [ ] 90° crossing verified at every power/signal intersection
- [ ] Heat-shrink labels on BOTH ends of every wire, within 20 mm of terminal
- [ ] 150 mm service loop at every connector
- [ ] Grommets installed at all chassis pass-through points
- [ ] All connectors fully seated with wedge locks engaged
- [ ] DC-DC converter CAN baud rate verified (500 kbps) or workaround implemented
- [ ] Power-up sequence test:
  1. Battery connected → DC-DC output 12.0–13.8 V
  2. All ATO fuses installed one at a time; verify no shorts
  3. LDO outputs: 3.3 V ±5% at each ESP32-S3, 5 V ±5% at STM32 and MCP4725
  4. CAN bus: verify traffic on both buses with PCAN-USB or logic analyzer
- [ ] ESTOP test: press button → all relays de-energize, MCP4725 outputs 0 V, brake light ON
- [ ] No smoke, no hot wires, no sparking on any connection

## Appendix B: Tools Required

| Tool | Use |
|------|-----|
| Deutsch HDT-48-00 crimp tool | DT and DTP contacts |
| Molex 63811-8700 crimp tool | Mini-Fit Jr contacts |
| TE 1490200-1 open-barrel crimper | Superseal contacts |
| Automotive ratcheting wire stripper (6–24 AWG) | All wire preparation |
| Heat gun with shrink nozzle | Heat-shrink labels + adhesive-lined tubing |
| Digital multimeter | Continuity, resistance, voltage |
| Megohmmeter / insulation tester (1000 V) | 72 V isolation verification |
| CAN bus interface (PCAN-USB or similar) | CAN traffic validation |
| Industrial heat-shrink label maker | Wire identification labels |
| Cable tie tension gun | Consistent tie tension |
| Soldering iron (temperature-controlled) | CMC/TVS pigtail boards, termination resistors |
| M6 torque wrench | Ground bus and Anderson connector terminals |
| Wire ferrule crimper | Ferrule termination for screw terminals |
| Oscilloscope (optional, 2-ch 100 MHz) | CAN signal integrity, throttle noise check |

---

## Appendix C: EGAS Dual-Path Motor Control Wiring Detail

Both SYS (Level 2) and MTR (Level 1) have a complete throttle + gear path to the motor controller. The motor controller sees only one active signal at a time through mode-gated control. To prevent both MCUs driving the same analog line simultaneously:

**Throttle (0–5 V analog):**
```
SYS MCP4725 (GPIO15/16) ── 1 kΩ ──┐
                                     ├── Motor Controller Throttle Input
MTR MCP4725 (PB6/PB7)    ── 1 kΩ ──┘
```
Both DACs can be connected through 1 kΩ series resistors at all times. The inactive MCU outputs 0 V (DAC code 0). The series resistors prevent either DAC from loading the other. Verify: when one DAC outputs 5 V through 1 kΩ and the other outputs 0 V, the motor controller sees ~2.5 V — acceptable because only the active MCU drives a non-zero value, and the idle MCU is held at 0 V by firmware.

**Gear (72 V discrete lines):**
```
SYS gear relays (GPIO33/34/35) ── COM: 72 V (fused 1 A) ──┬── Motor Controller
MTR gear relays (PA3/PA4/PA5)   ── COM: 72 V (fused 1 A) ──┘   Gear Input
```
Relay outputs can be parallel-connected: when a relay is open, its output floats. The active MCU's relay closure provides 72 V. Both MCUs asserting the same gear is safe (same voltage on same line). Both asserting DIFFERENT gears would create a conflict — prevented in firmware (only one MCU controls gears at a time per mode gate).

---

*Document version 1.0. Generated from architecture.md, all three config.h files, and the comprehensive codebase audit of 2026-06-25.*
