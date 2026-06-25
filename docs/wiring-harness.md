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

All voltages drops are one-way (positive wire only). Chassis is the return path; its resistance is negligible (steel frame tube cross-section ≫ any wire).

| Circuit | AWG | Color | Current | Length (one-way) | Drop |
|---------|-----|-------|---------|-----------------|------|
| 72 V battery → bus bar | 6 | Orange | 50 A cont | 0.5 m | 0.03 V |
| 72 V bus → motor controller | 6 | Orange | 42 A cont | 0.5 m | 0.03 V |
| 72 V bus → DC-DC input | 14 | Orange | 5 A | 1.5 m | 0.06 V |
| 72 V gear sense (×6: D/S/R ×2 paths) | 20 | Orange | <10 mA | 1.5 m | negl. |
| 72 V gear output (×6: D/S/R ×2 paths) | 18 | Orange | 1 A fused | 1.5 m | 0.03 V |
| DC-DC output → forward fuse block (rear→center) | 8 | Red | 35 A | 2.0 m | 0.15 V |
| Fuse block → EPS-C | 12 | Red | 30 A peak | 1.0 m | 0.16 V |
| Fuse block → SEB | 12 | Red | 20 A peak | 0.5 m | 0.05 V |
| Fuse block → Jetson | 16 | Red | 3 A | 0.5 m | 0.02 V |
| Fuse block → RT ESP32-S3 | 18 | Red | 0.5 A | 0.3 m | 0.003 V |
| Fuse block → SYS ESP32-S3 | 18 | Red | 0.5 A | 1.0 m | 0.01 V |
| Fuse block → MTR STM32 | 18 | Red | 0.2 A | 0.3 m | 0.001 V |
| Fuse block → lighting bus | 14 | Red | 10 A peak | 1.5 m | 0.13 V |
| Fuse block → always-on rail | 16 | Red/White | 3 A | 1.5 m | 0.04 V |
| Individual lamp feed | 18 | Red | 2 A | 2.0 m | 0.09 V |
| CAN trunk/drops | 22 | Yellow/Green | signal | 2.5 m | — |
| CAN_GND (signal reference) | 18 | Black/White | return | 2.5 m | negl. |
| Throttle signal (shielded) | 22 | White | <1 mA | 1.5 m | — |
| Encoder signal (shielded) | 24 | Gray | <10 mA | 2.0 m | — |
| Switch inputs (handlebar/dash) | 22 | Blue/Brown | <1 mA | 1.2 m | — |
| Relay coil drive | 22 | Yellow | 0.15 A | 1.2 m | 0.01 V |
| Chassis ground straps (short, local) | match supply | Black | per circuit | ≤0.3 m | negl. |
| Chassis bond (battery negative → frame) | 6 | Black | 50 A | 0.2 m | 0.01 V |

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

### 4.4 CAN Node Protection

Place a NUP2105L TVS diode (ON Semi, SOT-23, 24 V standoff, bidirectional) between CAN_H and CAN_GND, and between CAN_L and CAN_GND, within 50 mm of each node's connector. One device protects both lines. 8 nodes total (6 low bus + 2 high bus).

A common-mode choke (TDK ACT45B-510-2P) on each drop is recommended if EMI is observed, but not required for initial build.

### 4.5 Stub Length Limits

At 500 kbit/s, maximum stub length is 0.3 m (bit time = 2 µs, round-trip propagation ≈ 0.3 m at 5 ns/m). All drops in this design are ≤0.3 m.

### 4.6 DC-DC Converter Baud Rate Warning

The DC-DC protocol specification references J1939 extended CAN at **250 kbps**, but the low bus operates at **500 kbps**. Before finalizing: verify the DC-DC converter's CAN interface can be configured for 500 kbps. If it is fixed at 250 kbps, either:
1. Place it on a separate CAN segment with a baud rate converter, or
2. Replace with a unit supporting 500 kbps operation.

---

## 5. Power Distribution

Heavy components (battery, motor controller, DC-DC converter) are at the **rear**. High-current 12 V loads (EPS-C steering, Jetson) are at the **front/center**. The chassis frame is the ground return for all power circuits — this is standard automotive practice and avoids running heavy copper return wires the length of the vehicle.

### 5.1 Component Placement & Power Flow

```
REAR ───────────────────────────────────────────────────── FRONT
  │                                                          │
  │  ┌──────────┐   72 V, 6 AWG, 0.5 m                      │
  │  │ 72 V     │──────┬────────────── Motor Controller      │
  │  │ Battery  │      │            (rear, short run)        │
  │  └────┬─────┘      │                                     │
  │       │            ├── 14 AWG, 2.0 m ──────────────┐    │
  │       │            │   (72 V forward to DC-DC)     │    │
  │       │            │                               │    │
  │       │    [ANL 60 A] at battery (+) terminal      │    │
  │       │    [MEGA 15 A] at DC-DC tap point          │    │
  │       │                                            │    │
  │  ┌────▼─────┐                              ┌───────▼──────┐
  │  │ DC-DC    │  12 V, 8 AWG, 0.3 m          │ DC-DC        │
  │  │ 72→12 V  │────── 12 V fuse block ───────│ (preferred:  │
  │  │ (rear)   │       (rear)                 │  at front    │
  │  └──────────┘                              │  near loads) │
  │                                            └──────────────┘
  │  12 V runs forward from rear:
  │  ┌──────────────────────────────────────────────────────┐
  │  │ 8 AWG red: DC-DC → forward fuse block (JP2, center)  │
  │  │                 ↓                                    │
  │  │   ├── EPS-C steering (12 AWG, front, 1.0 m)          │
  │  │   ├── Jetson Orin (16 AWG, center, 0.5 m)            │
  │  │   ├── RT ESP32-S3 (18 AWG, center, 0.3 m)            │
  │  │   ├── SYS ESP32-S3 (18 AWG, handlebar, 1.0 m)        │
  │  │   ├── Lighting (14 AWG, front+rear, 1.5 m)           │
  │  │   └── Always-on rail (16 AWG, 1.5 m)                 │
  │  │                                                      │
  │  │  Rear loads (short runs from rear fuse block):        │
  │  │   ├── SEB brake (12 AWG, rear, 0.5 m)                │
  │  │   └── MTR STM32 (18 AWG, rear, 0.3 m)               │
  │  └──────────────────────────────────────────────────────┘
  │
  │  All grounds: chassis return (see §6)
```

**Key decision — DC-DC placement:** If the DC-DC is at the rear (with the battery), 12 V runs forward at high current through 8 AWG. If moved to the front/center, 72 V runs forward at only ~5 A through 14 AWG, and 12 V is generated right where the big loads are. The DC-DC is on the CAN bus which spans the full vehicle, so either placement works. This spec assumes **DC-DC at rear** (worst-case for 12 V distribution); front placement halves the 12 V copper weight.

### 5.2 Fuse Strategy

Fuses protect the **wire**, so they go at the **source end** of every wire run — as close to the power source as physically possible.

| ID | Location | Type | Rating | Protects (wire run) | Fault current path |
|----|----------|------|--------|---------------------|-------------------|
| F_main | Battery (+) terminal, inside battery box | ANL | 60 A | 6 AWG from battery to bus bar (0.5 m) | Battery → short → chassis at rear |
| F_dcdc | 72 V bus bar, rear | MEGA | 15 A | 14 AWG 72 V feed to DC-DC input | 72 V bus → DC-DC → chassis |
| F_gear_* | At gear relay COM terminal, rear | 3AG fast | 1 A ×6 | 18 AWG gear output wires to motor ctrl | 72 V → gear wire → motor ctrl → chassis |
| F_12v_main | DC-DC output terminal, rear | ATO | 40 A | 8 AWG 12 V forward run (2 m) | DC-DC → 12 V wire → chassis short |
| F_epsc | Forward fuse block (JP2), center | ATO | 30 A | 12 AWG to EPS-C (1 m) | 12 V bus → EPS-C → chassis at front |
| F_seb | Rear fuse block, rear | ATO | 25 A | 12 AWG to SEB (0.5 m) | 12 V bus → SEB → chassis at rear |
| F_jetson | Forward fuse block (JP2), center | ATO | 5 A | 16 AWG to Jetson (0.5 m) | 12 V bus → Jetson → chassis |
| F_rt | Forward fuse block (JP2), center | ATO | 3 A | 18 AWG to RT ESP32 (0.3 m) | 12 V bus → RT → chassis |
| F_sys | Forward fuse block (JP2), center | ATO | 3 A | 18 AWG to SYS ESP32 (1 m) | 12 V bus → SYS → chassis |
| F_mtr | Rear fuse block, rear | ATO | 3 A | 18 AWG to MTR STM32 (0.3 m) | 12 V bus → MTR → chassis |
| F_lights | Forward fuse block (JP2), center | ATO | 15 A | 14 AWG lighting bus (1.5 m) | 12 V bus → GPIO27 relay → lamps → chassis |
| F_always | Forward fuse block (JP2), center | ATO | 2 A | 16 AWG always-on rail (1.5 m) | 12 V bus → brake light, CAN → chassis |

**Fuse coordination:** F_main (60 A) is the last-resort protection at the battery. Every branch fuse opens before F_main does. The DC-DC has its own input fuse (F_dcdc, 15 A on 72 V side) and output is protected by F_12v_main (40 A on 12 V side). A DC-DC internal fault that shorts 72 V to 12 V would be caught by F_dcdc opening.

**All fuse holders must be at the source end of the wire they protect.** The ANL fuse lives on the battery positive terminal. Branch fuses live in two ATO/ATC blocks: one at the rear (near DC-DC, for SEB and MTR), one at the center (near JP2, for front loads).

### 5.3 Voltage Drop (One-Way, Chassis Return)

With chassis as the ground return, only the positive wire length matters. Chassis steel has higher resistivity than copper, but the cross-sectional area of a frame tube is enormous — effective resistance is ≪1 mΩ over 2 meters. The dominant drop is in the positive wire only.

| Path | Voltage | Current | Gauge | Length (one-way) | Drop | % |
|------|---------|---------|-------|-----------------|------|---|
| Battery → Motor Controller | 72 V | 50 A | 6 AWG | 0.5 m | 0.03 V | 0.04% |
| Battery → DC-DC (72 V side) | 72 V | 5 A | 14 AWG | 1.5 m | 0.06 V | 0.09% |
| DC-DC → Forward fuse block | 12 V | 35 A | 8 AWG | 2.0 m | 0.15 V | 1.22% |
| Fuse block → EPS-C | 12 V | 30 A | 12 AWG | 1.0 m | 0.16 V | 1.33% |
| Fuse block → SEB | 12 V | 20 A | 12 AWG | 0.5 m | 0.05 V | 0.44% |
| Fuse block → Jetson | 12 V | 3 A | 16 AWG | 0.5 m | 0.02 V | 0.17% |
| Fuse block → SYS (longest MCU run) | 12 V | 0.5 A | 18 AWG | 1.0 m | 0.01 V | 0.09% |

All drops well under 3%. The 8 AWG forward run from rear DC-DC to center fuse block is the dominant drop at 1.22%. If DC-DC is moved to the front, this run disappears entirely and 12 V distribution starts at the center.

### 5.5 12 V Accessory Relay (GPIO27)

SYS GPIO27 controls a 40 A automotive relay:

```
GPIO27 ── 1 kΩ base R ── NPN (2N2222) ── relay coil ── 12 V
                                 │
                                 └── 1N4007 flyback (cathode to +12 V)

Relay COM  ← always-on 12 V rail (F_always, 2 A)
Relay NO   → accessory bus: headlight, turn signals, mode bulbs
Relay NC   → not connected
Relay coil return → chassis (local)
```

ESTOP behavior: GPIO27 goes high-impedance → NPN off → relay opens → accessory bus dead. Brake light is on the **always-on** rail (before this relay), so it stays powered in ESTOP.

---

## 6. Grounding Strategy

The trike's steel frame is the ground return conductor for all power circuits. This is how every production vehicle works — the chassis cross-section is orders of magnitude larger than any wire you'd run, so its resistance is effectively zero. The weight and cost savings vs. running dedicated return wires over 2-meter distances are substantial.

### 6.1 Grounding Rules

1. **Chassis IS the power ground return.** Every device's negative/ground terminal connects to the nearest clean frame point with a short strap. No long dedicated ground return wires. This applies to 72 V, 12 V, and 5 V returns alike — they all reference chassis.

2. **Battery negative → chassis at exactly ONE point** (at the rear, near the battery). This is the system ground reference. Use a 6 AWG strap ≤0.2 m long. Remove paint/powder-coat to bare metal, apply dielectric grease, torque to spec.

3. **Ground strap sizing** — short straps to chassis carry the same current as the positive supply wire, but over ~0.2 m instead of 2 m. They can be the same gauge as the positive or one size smaller:
   - Motor controller: 6 AWG strap
   - DC-DC converter: 8 AWG strap
   - EPS-C: 12 AWG strap
   - SEB: 12 AWG strap
   - Jetson: 16 AWG strap
   - ESP32-S3 / STM32: 18 AWG strap
   - Lamps: 18 AWG strap, local to mounting bolt

4. **CAN_GND is the exception.** It is NOT a power return — it's a signal reference. Run a dedicated 18 AWG black/white wire in the CAN backbone. Connect it to chassis at exactly ONE point (at JP2, the center). This prevents CAN return current from taking paths through the chassis that would create common-mode voltage differences between nodes.

5. **Shield drains** for all shielded cables (CAN, throttle, encoders) connect to chassis at exactly ONE point (near JP2). The far end of each shield is cut flush and insulated — no connection. This prevents ground loops while maintaining an RF drain path.

6. **Clean the bond points.** Every chassis ground connection must be to bare metal. Use a serrated washer to bite through any remaining coating. Apply dielectric grease after torquing to prevent corrosion. On a powder-coated or painted frame, grind a clean patch for each ground strap.

7. **EPS-C ECU housing** requires a chassis ground per the Syntree manufacturer spec — this is for RF shielding, not power return. Bond the housing to chassis with a dedicated strap at its mounting point.

### 6.3 Notes

With chassis-referenced ground, the 72 V and 12 V domains share the same return path — there is one ground, not two separate buses. A positive-to-chassis short anywhere draws fault current through the nearest fuse and opens it; the fuse is the ground-fault protection.

CAN transceivers (SN65HVD230, ±25 V common-mode) are safe: the steel frame has ≪1 mΩ over 2 m, so even 50 A motor current produces ≪50 mV of ground offset. The dedicated CAN_GND wire provides a secondary signal reference path connected to chassis at one point.

---

## 7. Protection Additions

### 7.1 CAN Bus (Per Node)

NUP2105L TVS diode (ON Semi, SOT-23) between CAN_H and CAN_GND, and between CAN_L and CAN_GND. 8 nodes total. Add a common-mode choke (TDK ACT45B-510-2P) only if EMI issues appear in testing.

### 7.2 Throttle Signal Protection

At the ADC input (both SYS GPIO10 and MTR PA0):

```
Throttle signal ── 100 Ω series ──┬── ADC pin
(shielded)                        │
                                  ├── SMBJ5.0A TVS (5 V standoff, 8.6 V clamp) ── GND
                                  │
                                  └── 100 pF NPO ceramic ── GND
```

**TVS:** Littelfuse SMBJ5.0A (unidirectional, DO-214AA). Install at both SYS and MTR throttle ADC inputs.

### 7.3 Encoder Input Protection

At each encoder input on RT ESP32-S3:

```
Encoder A/B ── 330 Ω series ──┬── GPIO
                               │
                               ├── PESD5V0S2UT TVS array ── GND
                               │
                               └── 100 pF ── GND
```

**TVS:** Nexperia PESD5V0S2UT (2-channel, SOT-23). One array per encoder (2 channels = A+B). Total: 4 arrays (rear motor, front wheel, rear left, rear right — rear left/right are TBD sensors).

### 7.4 Relay Flyback Diodes

Every relay coil MUST have a flyback diode soldered directly across the coil terminals (cathode to +12 V side, anode to ground/switched side).

**Diode:** 1N4007 (1 A, 1000 V). One per relay coil.

**Relays requiring flyback diodes (total 13):**

| Location | Qty | Purpose |
|----------|-----|---------|
| MTR gear output | 3 | D, S, R relay drivers (PA3/4/5) |
| SYS gear output | 3 | D, S, R relay drivers (GPIO33/34/35) |
| SYS lighting | 6 | Left turn, Right turn, Brake, Headlight, AUTO bulb, MANUAL bulb |
| SYS 12 V accessory | 1 | GPIO27 main accessory relay |

### 7.5 Lamp Circuit TVS Protection

At each lamp connector, bidirectional TVS across 12 V and GND:

```
12 V ──┬── Lamp ── GND
       │
       └── SMCJ18A TVS (18 V standoff, 29.2 V clamp) ── GND
```

**TVS:** Littelfuse SMCJ18A (unidirectional, SMC/DO-214AB). One per lamp: brake light, left turn, right turn, headlight = 4 total.

### 7.6 72 V Gear Line Protection

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
  CAN_GND (18 AWG black/white)         Motor controller power (6 AWG orange)
  Sensor cables (throttle, enc.)       DC-DC 72 V input (14 AWG orange)
  12 V distribution (8 AWG red)       12 V high-current (EPS-C/SEB, 12 AWG red)
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
| Wire, 18 AWG red (GXL) | 4 m | SYS power, bulb power |
| Wire, 18 AWG black (GXL) | 0.5 m | SYS (−) → chassis strap (0.2 m); bulb ground straps (×2, 0.15 m) |
| Wire, 22 AWG blue | 4 m | Switch signals (×5) |
| Wire, 22 AWG brown | 2 m | Switch common/return |
| Wire, 22 AWG yellow | 3 m | Relay coil drives (×6 + GPIO27) |
| Wire, 22 AWG white shielded | 1.5 m | Belden 8451 | Throttle grip signal |
| Wire, 20 AWG orange (×3) | 4 m | Gear sense TLP281 inputs (D/S/R) |
| Wire, 18 AWG orange (×3) | 2 m | Gear relay outputs to motor controller |
| Shielded pair, 22 AWG | 1.5 m | Belden 9841 | CAN drop to SYS |
| DT04-4P + contacts | 2 | Deutsch | Handlebar switch connectors J2 (left), J3 (right) |
| DT06-6S + contacts | 1 | Deutsch | Dashboard buttons + bulbs J4 |
| DT04-4P + contacts | 1 | Deutsch | Power/CAN input to SYS area J5 |
| Molex 39-01-2060 + 44476-1111 | 2 | Mini-Fit Jr 6-pin | SYS board I/O breakout J1a, J1b |
| Molex 39-01-2040 + 44476-1111 | 1 | Mini-Fit Jr 4-pin | SYS board power J1c |
| TE Superseal 1-967628-1 (3-pin) | 1 | Superseal 1.5 | Throttle grip connector J15 |
| SMBJ5.0A TVS | 1 | Littelfuse | Throttle signal protection (at SYS ADC) |
| NUP2105L TVS | 1 | CAN protection | SYS CAN node |
| 1N4007 flyback diode | 7 | — | SYS relay coils (gear×3, lights×3, GPIO27) |
| TLP281 optoisolator | 3 | Toshiba | SYS gear sense (D/S/R) |
| SMCJ90CA TVS | 3 | Littelfuse | SYS gear line TVS (72 V) |
| 3AG 1A fuse + FHAC0001ZXJ | 3 | Littelfuse | SYS gear output fuses |
| SMCJ18A TVS | 1 | Littelfuse | Bulb power protection |
| Braided PET sleeve, 19 mm | 1.5 m | — | Harness wrap |
| Heat-shrink labels | 30 | — | Wire markers, both ends |

### H2 — Power Distribution

Includes 72 V battery cabling, DC-DC converter, fuse blocks, and chassis ground straps at the rear.

| Item | Qty | Notes |
|------|-----|-------|
| Wire, 6 AWG orange (SXL) | 1.5 m | Battery (+) → ANL fuse → 72 V bus → motor controller |
| Wire, 6 AWG black (SXL) | 0.5 m | Battery (−) → chassis bond strap (0.2 m); motor ctrl (−) → chassis strap (0.2 m) |
| Wire, 14 AWG orange (GXL) | 2 m | 72 V bus → DC-DC input (rear to center run) |
| Wire, 8 AWG red (GXL) | 2.5 m | DC-DC output → forward fuse block (JP2) |
| Wire, 8 AWG black (GXL) | 0.3 m | DC-DC (−) → chassis strap |
| Wire, 12 AWG red (GXL) | 1 m | Forward fuse block → EPS-C |
| Wire, 12 AWG red (GXL) | 0.7 m | Rear fuse block → SEB |
| Wire, 12 AWG black (GXL) | 0.5 m | EPS-C (−) → chassis strap; SEB (−) → chassis strap |
| Anderson SB50 + SB50G boot | 1 pair | Battery disconnect |
| ANL fuse holder + 60 A fuse | 1 | At battery (+) terminal |
| MEGA inline fuse holder + 15 A fuse | 1 | At 72 V bus, DC-DC tap point |
| ATO/ATC fuse block, 6-circuit | 2 | One at rear (SEB, MTR, always-on), one at center (EPS-C, Jetson, RT, SYS, lights) |
| ATO fuses: 40,30,25,5,3×3,15,2 A | 10 | Per branch |
| 3AG 1 A fuse + Littelfuse FHAC0001ZXJ holder | 6 | Gear output lines (×2 paths ×3 gears) |
| DT04-2P | 1 | DC-DC 72 V input connector |
| DTP04-2P | 1 | DC-DC 12 V output connector |
| M6 ring terminals (6 AWG) | 4 | Chassis bond straps |
| M6 ring terminals (8–12 AWG) | 8 | Device ground straps |
| M6 stainless bolts + serrated washers | 6 | Chassis bond points (bare metal prep) |
| Dielectric grease | 1 tube | Corrosion protection at bond points |
| Split loom, 25 mm orange | 1 m | 72 V conduit |
| Split loom, 19 mm black | 2 m | 12 V conduit |
| Heat-shrink labels | 25 | Wire markers, both ends |

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
| NUP2105L TVS | 8 | ON Semi, SOT-23 | CAN protection, one per node |
| Braided PET sleeve, 12 mm | 3 m | — | Trunk wrap |
| Heat-shrink labels | 30 | — | Wire markers, both ends |

### H4 — MTR / Sensor (1.5 m, Rear Motor Area)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 18 AWG red | 2 m | MTR STM32 power |
| Wire, 18 AWG black | 0.3 m | MTR (−) → chassis strap (0.2 m) |
| Wire, 14 AWG red | 1 m | 12 V to relay module COM |
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
| NUP2105L TVS | 1 | CAN protection | MTR CAN node |
| Braided PET sleeve, 19 mm | 2 m | — | Harness wrap |

### H5 — Steering / Front (1.0 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 12 AWG red | 1.5 m | EPS-C 12 V power |
| Wire, 12 AWG black | 0.3 m | EPS-C (−) → chassis strap (0.2 m) |
| Shielded pair, 22 AWG | 1.5 m | Belden 9841 | CAN drop |
| Shielded quad, 24 AWG | 1.5 m | Belden 8772 | Front wheel encoder (TBD sensor) |
| DTP04-2P + 0460-202-1631 | 1 | Deutsch DTP | EPS-C power J21a |
| DT04-4P + 0462-201-2031 | 1 | Deutsch | EPS-C CAN J21b |
| TE Superseal 1.5 4-pin | 1 | 1-967629-1 | Front encoder J22 |
| PESD5V0S2UT TVS | 1 | Nexperia | Front encoder protection |
| NUP2105L TVS | 1 | CAN protection | EPS-C CAN node |
| Braided PET sleeve, 16 mm | 1.5 m | — | Harness wrap |

### H6 — Brake Module (1.0 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 12 AWG red | 1.5 m | SEB 12 V power |
| Wire, 12 AWG black | 0.3 m | SEB (−) → chassis strap (0.2 m) |
| Shielded pair, 22 AWG | 1.5 m | Belden 9841 | CAN drop |
| DTP04-2P + 0460-202-1631 | 1 | Deutsch DTP | SEB power J23a |
| DT04-4P + 0462-201-2031 | 1 | Deutsch | SEB CAN J23b |
| NUP2105L TVS | 1 | CAN protection | SEB CAN node |
| Braided PET sleeve, 16 mm | 1.5 m | — | Harness wrap |

### H7 — Lighting Rear (2.0 m)

| Item | Qty | Part Number | Notes |
|------|-----|-------------|-------|
| Wire, 14 AWG red | 2 m | Lighting bus from accy relay |
| Wire, 18 AWG red | 5 m | Individual lamp feeds (×3) |
| Wire, 18 AWG black | 1 m | Lamp (−) → chassis straps (×3, each ≤0.15 m local) |
| Wire, 16 AWG red/white | 2 m | Brake light (always-on rail) |
| DT04-2P + 0462-201-16141 | 3 | Deutsch | Lamp connectors J24 (left turn), J25 (right turn), J26 (brake) |
| SMCJ18A TVS | 3 | Littelfuse | Per-lamp transient protection |
| Split loom, 12 mm | 2.5 m | — | Harness wrap |

### H8 — Chassis Ground Straps

Short local straps from each device's negative terminal to the nearest clean frame point. No long ground return wires — the chassis is the return path.

| Item | Qty | Notes |
|------|-----|-------|
| Wire, 6 AWG black (SXL) | 0.5 m | Battery (−) strap + motor controller (−) strap (×2, each ≤0.2 m) |
| Wire, 8 AWG black (GXL) | 0.3 m | DC-DC (−) strap |
| Wire, 12 AWG black (GXL) | 0.5 m | EPS-C (−) strap + SEB (−) strap (×2) |
| Wire, 16 AWG black (GXL) | 0.3 m | Jetson (−) strap |
| Wire, 18 AWG black (GXL) | 1.0 m | RT (−), SYS (−), MTR (−) straps + lamp grounds (×6) |
| M6 ring terminals (6 AWG) | 4 | Battery and motor controller straps |
| M6 ring terminals (8–12 AWG) | 8 | Mid-current device straps |
| M5 ring terminals (16–18 AWG) | 12 | Low-current device and lamp straps |
| M6 stainless bolts + serrated washers | 8 | Bond points (grind to bare metal, grease, torque) |
| M5 stainless bolts + serrated washers | 12 | Smaller device bond points |
| Dielectric grease | 1 tube | All chassis bond points |
| Split loom, 10 mm | 2 m | Strap protection where routed along frame |

---

## 10. Reference Tables

### 10.1 CAN Message → Physical Node Wiring

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

## Appendix: Assembly Checklist

- [ ] Continuity test on every wire before terminating
- [ ] Flyback diode polarity verified on all 13 relay coils (cathode to +12 V, anode to switched side)
- [ ] CAN termination: 60 Ω ±5% between CAN_H and CAN_L at any node (both terminators installed)
- [ ] CAN_GND connected to chassis at exactly ONE point (JP2 center)
- [ ] Shield drains bonded to chassis at exactly ONE point (JP2), floating at far ends
- [ ] Every device ground strap to chassis: <0.1 Ω (bare metal, greased, tight)
- [ ] Battery disconnected: 72 V positive to chassis >100 kΩ (no short before power-up)
- [ ] Power-up: DC-DC output 12.0–13.8 V; LDO outputs 3.3 V ±5% and 5 V ±5%
- [ ] CAN bus traffic verified on both buses
- [ ] ESTOP test: all relays open, MCP4725 outputs 0 V, brake light ON

---

*Document version 1.0. Generated from architecture.md and all three config.h files, 2026-06-25.*
