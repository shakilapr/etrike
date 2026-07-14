# RT ↔ SYS CAN Bus Bench Test Plan

**Goal:** Validate CAN communication between RT-ESP32 and SYS-ESP32 on the
low-level CAN bus, using CANalyst-II to monitor traffic and inject signals that
mimic absent third-party hardware (MTR, EPS-C, SEB, Host). No motor, steering
rack, brake actuator, or high-voltage power required.

---

## 1. How CAN Works (Physical Layer)

Before connecting anything, it helps to know what's happening electrically.

### 1.1 Two Wires, One Signal

A CAN bus is a **differential pair**: two wires — CAN_H (high) and CAN_L (low).
The signal is the *voltage difference* between them, not the absolute voltage on
either wire. This makes CAN immune to electrical noise: if a motor or relay
induces a spike, it hits both wires equally and the *difference* stays clean.

```
         recessive (bit = 1)          dominant (bit = 0)
         ┌─────────────────┐          ┌─────────────────┐
CAN_H    │     ~2.5 V       │          │     ~3.5 V       │
         │                  │          │               ██│
CAN_L    │     ~2.5 V       │          │  ██             │
         │                  │          │     ~1.5 V       │
         └─────────────────┘          └─────────────────┘
         ΔV ≈ 0 V                     ΔV ≈ 2 V
```

**Recessive** (logical 1): both wires sit at ~2.5 V. This is the idle state.
**Dominant** (logical 0): CAN_H is pulled to ~3.5 V and CAN_L to ~1.5 V — a
~2 V differential. A dominant bit on the bus "wins" over a recessive bit, which
is how arbitration works (lower CAN ID = higher priority).

### 1.2 The Pieces

Each node on the bus needs three things to talk CAN:

```
ESP32-S3                    SN65HVD230                  CAN Bus
┌──────────┐              ┌──────────────┐
│          │              │              │
│  TWAI    │   TX  ───────│ D   (TXD)    │
│  Controller│  RX  ───────│ R   (RXD)    │─── CAN_H ────┐
│  (inside │              │              │─── CAN_L ────┤
│   chip)  │              │  RS (mode)   │              │
│          │              │  VCC 3.3V    │              │ bus
│          │              │  GND         │              │
│          │              └──────────────┘              │
└──────────┘                                            │
                                                    other nodes
```

| Part | What It Does |
|------|-------------|
| **TWAI Controller** | Inside the ESP32-S3. Handles the CAN protocol — bit timing, arbitration, CRC, ACK, error counters. Talks to the transceiver via TX and RX pins (GPIO 5 and 4 on both RT and SYS). |
| **WCMCU-230 / SN65HVD230 Transceiver** | Small breakout board (~2×3 cm) between the ESP32 and the physical bus. Uses the TI SN65HVD230 chip (3.3V CAN transceiver). The same module is sold under multiple brand names — **WCMCU-230**, **CJMCU-230**, or just "SN65HVD230 CAN module" — they all contain the same TI chip. Converts the TWAI's single-ended 3.3V logic signals (TX/RX) into the differential CAN_H/CAN_L signals the bus needs. |
| **MCP2515 SPI CAN Module** | Separate SPI-to-CAN controller (Microchip MCP2515 + TJA1050 transceiver). RT uses one for its high bus. For this bench test the module is **available but not used** — RT's MCP2515 high bus stays disconnected. Could optionally serve as a second bus monitor if combined with the debug-esp32 firmware. |
| **CANalyst-II** | USB analyzer that listens *passively* on the bus (it never ACKs or arbitrates). Has two independent channels — we use Channel 0 for the low bus. The PC-side driver (WinUSB via Zadig) lets our debug-tool backend read and inject frames. |

### 1.3 WCMCU-230 / SN65HVD230 Transceiver Module

Your CAN transceiver module (sold as **WCMCU-230**, **CJMCU-230**, or generically
as "SN65HVD230 CAN module") looks like this:

```
     ┌─────────────────────┐
     │  VCC  GND  CAN_H    │
     │   ●    ●    ●       │  ← 3-pin terminal block
     │  CAN_L  CTX CRX     │     to CAN bus
     │   ●      ●   ●      │  ← 3-pin header to ESP32
     └─────────────────────┘
```

Common pin labels vary by batch. Match yours to the table below:

| Label on Module | Connects To | Notes |
|----------------|-------------|-------|
| VCC / 3V3 | ESP32 3.3V | **Must be 3.3V, not 5V** — the SN65HVD230 is a 3.3V chip |
| GND | ESP32 GND | Common ground reference |
| CTX / TX / TXD / D | ESP32 GPIO 5 | TWAI TX → transceiver TXD input |
| CRX / RX / RXD / R | ESP32 GPIO 4 | Transceiver RXD output → TWAI RX |
| CAN_H | Bus CAN_H | Differential high (yellow/green wire) |
| CAN_L | Bus CAN_L | Differential low (white/brown wire) |

> **Pin label confusion:** On WCMCU-230 modules, the pins are often silk-screened
> as **CTX** (CAN TX) and **CRX** (CAN RX). These connect to the ESP32's TX (GPIO5)
> and RX (GPIO4) respectively — CTX→GPIO5, CRX→GPIO4. If you see no frames after
> power-up, try swapping these two wires on one board.

The module only needs **4 wires**: VCC, GND, CTX, CRX. The RS (mode select)
pin is pulled to GND by an onboard 10 kΩ resistor — the transceiver is locked
in high-speed mode automatically. No external connection needed.

**Counterfeit warning:** Some WCMCU-230 modules from AliExpress/Amazon ship with
fake or marginal SN65HVD230 chips that can *receive* but cannot *transmit* reliably.
If one board's frames appear in the monitor but the other's don't, swap the
transceiver modules between the two boards — if the problem follows the module,
replace it.

### 1.4 Termination — Why 120Ω

CAN is a transmission line. Without termination, the fast edge transitions
(~50 ns rise time) reflect off the open ends of the bus and cause data
corruption. A **120Ω resistor between CAN_H and CAN_L at each physical end**
of the bus absorbs these reflections.

```
    [RT] ──── CAN_H ──── [SYS] ──── CAN_H ──── [CANalyst-II]
      │                    │                     │
    120Ω                 120Ω                    │
      │                    │                     │
    [RT] ──── CAN_L ──── [SYS] ──── CAN_L ──── [CANalyst-II]
             
    Total bus impedance: 120Ω ∥ 120Ω = 60Ω  ← measure this with power off
```

- **Two terminators minimum** — one at each end. With only RT and SYS on the
  bus, both must have termination. The CANalyst-II is a middle tap and must
  NOT be terminated (it already has one internally for its monitoring mode).
- **Most SN65HVD230 modules have an onboard 120Ω resistor** controlled by a
  jumper, solder bridge, or DIP switch. Look for a pair of pads labeled
  "120Ω" or "TERM" and bridge them with solder, or set the jumper to ON.
- **If you're unsure**, measure resistance between CAN_H and CAN_L on the
  module with power off. You should see ~120Ω. If you see >10kΩ, termination
  is not enabled — solder the bridge.

### 1.5 CANalyst-II as a Passive Listener

The CANalyst-II is a USB-connected dual-channel CAN analyzer. In this test:

| Channel | Connected To | Role |
|---------|-------------|------|
| Ch0 | Low bus (CAN_H, CAN_L, GND) | Monitor all traffic + inject frames |
| Ch1 | Nothing | Unused |

It operates in **listen-only mode** — it never acknowledges frames or
participates in arbitration. This means it can't accidentally disrupt the bus.
The debug-tool backend reads frames from Ch0 and can inject frames (which the
CANalyst-II sends as a normal CAN node would, with arbitration and ACK).

---

## 2. Architecture Under Test

```
                         HIGH CAN BUS (500 kbit/s)
  ┌──────────────────────────────────────────────────────────────┐
  │                                                              │
  │  ┌──────────────────┐                     ┌──────────────┐   │
  │  │   RT ESP32-S3    │                     │ CANalyst-II  │   │
  │  │  MCP2515 (SPI)   │◄───────────────────►│ Ch1          │   │
  │  │  SCK=15 MOSI=16  │    inject 0x300,     │ terminator   │   │
  │  │  MISO=17 CS=18   │    0x301, 0x7FC      │ 120Ω ON      │   │
  │  │  INT=7           │                     └──────────────┘   │
  │  └──────┬───────────┘                                        │
  │         │                                                    │
  └─────────┼────────────────────────────────────────────────────┘
            │  RT is the CAN gateway — forwards high↔low
            │
  ┌─────────┼────────────────────────────────────────────────────┐
  │         │            LOW CAN BUS (500 kbit/s)                 │
  │  ┌──────┴───────────┐         ┌──────────────────┐           │
  │  │   RT ESP32-S3    │         │   SYS ESP32-S3   │           │
  │  │  TWAI (built-in) │         │  TWAI (built-in) │           │
  │  │  TX=GPIO5 RX=4   │         │  TX=GPIO5 RX=4   │           │
  │  │       │          │         │       │          │           │
  │  │   WCMCU-230      │         │   WCMCU-230      │           │
  │  │   120Ω TERM ON   │         │   120Ω TERM ON   │           │
  │  └──────────────────┘         └──────────────────┘           │
  │           │                            │                     │
  └───────────┼────────────────────────────┼─────────────────────┘
              │                            │
              └──────────┬─────────────────┘
                         │
                  ┌──────┴───────┐
                  │ CANalyst-II  │
                  │ Ch0          │
                  │ terminator   │
                  │ OFF (60Ω bus)│
                  └──────────────┘
```

- **High bus:** RT MCP2515 ↔ CANalyst-II Ch1. We inject `0x300` (Host drive),
  `0x301` (Host brake), `0x7FC` (Host heartbeat) here. CANalyst-II Ch1 provides
  the sole 120Ω terminator.
- **Low bus:** RT WCMCU-230 ↔ SYS WCMCU-230 ↔ CANalyst-II Ch0. RT and SYS
  both terminate (60Ω total). CANalyst-II Ch0 is a middle tap.
- **RT is the gateway** — receives Host commands on the high bus, forwards
  `0x204`/`0x205`/`0x169` to the low bus. Forwards `0x011`/`0x120`/`0x206`/`0x600`
  low→high for telemetry.
- **MTR, EPS-C, SEB are absent** — we inject their frames on the low bus.

---

## 3. Hardware Setup

### 3.1 Complete Parts List

| # | Part | Qty | Details |
|---|------|-----|---------|
| 1 | ESP32-S3 DevKit board | 2 | RT + SYS. Any ESP32-S3 dev board with boot/release buttons. |
| 2 | WCMCU-230 CAN module | 2 | SN65HVD230 3.3V transceiver. RT + SYS each get one for the low bus. |
| 3 | MCP2515 SPI CAN module | 1 | RT's high bus. MCP2515 + TJA1050, 8-pin SPI header + 3-pin screw terminal. |
| 4 | CANalyst-II USB analyzer | 1 | Dual-channel. **Ch0 → low bus**, **Ch1 → high bus**. Two green 3-pin pluggable terminals. |
| 5 | USB cable (data) | 3 | One per ESP32 (flash + serial), one for CANalyst-II. |
| 6 | Female-female Dupont jumpers | 16 | 20 cm. 4 per WCMCU-230 + 7 for MCP2515 SPI + spares. |
| 7 | Solid-core hookup wire, 22 AWG | 2 m each | CAN_H (yellow), CAN_L (green), GND (black). For both bus backbones. |
| 8 | Multimeter | 1 | Measure resistance (termination) and voltage (bus health). |

**On-module components** (no separate purchase needed):

| Component | On Which Module | Value | Purpose |
|-----------|----------------|-------|---------|
| 120Ω SMD resistor | WCMCU-230 (×2) | 120 Ω | CAN bus termination. Enabled via solder jumper or 2-pin header shunt. |
| 120Ω resistor | CANalyst-II Ch1 | 120 Ω | Software-switched terminator for the high bus. |
| 10 kΩ pull-down | WCMCU-230 | 10 kΩ | Pulls RS to GND — locks transceiver in high-speed mode. Onboard, no user action. |
| 100 nF decoupling cap | WCMCU-230 | 100 nF | VCC-GND power rail filtering. Onboard. |

### 3.2 Controller Wiring — RT ESP32-S3

RT is the CAN gateway — it has **two** CAN modules:

| Module | Interface | CAN Bus | This Test |
|--------|----------|---------|-----------|
| **WCMCU-230** (SN65HVD230) | TWAI controller (built-in) | **Low** | ✅ Active — RT↔SYS traffic |
| **MCP2515 SPI** | SPI (GPIO15/16/17/18/7) | **High** | ✅ Active — injected Host commands, only with 3.3 V-safe level translation |

The ESP32-S3-DevKitC-1 has two 22-pin headers. Hold the board with the USB port
pointing **down**. The left strip is **J1**, the right strip is **J3**.

```
       ESP32-S3-DevKitC-1 (USB facing DOWN)
       ┌──────────────────────────────────────────────────┐
       │  ┌──── J1 ──────────┐  ┌──── J3 ──────────────┐ │
       │  │ 1  3V3         ● │  │ ● GND              1  │ │
       │  │ 2  3V3         ● │  │ ● TX    (GPIO43)   2  │ │  ← UART0 serial, not CAN
       │  │ 3  RST         ● │  │ ● RX    (GPIO44)   3  │ │
       │  │ 4  GPIO4  (4)  ● │  │ ● GPIO1  (1)       4  │ │
       │  │ 5  GPIO5  (5)  ● │  │ ● GPIO2  (2)       5  │ │
       │  │ 6  GPIO6  (6)  ● │  │ ...                    │ │
        │  │ ...              │  │ ● GPIO15               │ │  ← MCP2515 SCK
        │  │ ...              │  │ ● GPIO16               │ │  ← MCP2515 MOSI
        │  │ ...              │  │ ● GPIO17               │ │  ← MCP2515 MISO
        │  │ ...              │  │ ● GPIO18               │ │  ← MCP2515 CS
        │  │ ...              │  │ ● GPIO7                │ │  ← MCP2515 INT
       │  │ 21  5V         ● │  │ ● GND              21  │ │
       │  │ 22  GND        ● │  │ ● GND              22  │ │
       │  └─────────────────┘  └───────────────────────┘ │
       └──────────────────────────────────────────────────┘
```

#### Module A — WCMCU-230 (Low Bus, Active)

Config in `rt-esp32/src/config.h`:
```cpp
constexpr int kCanLowTxGpio = 5;   // TWAI TX → transceiver CTX
constexpr int kCanLowRxGpio = 4;   // TWAI RX → transceiver CRX
```

**⚠️ Do NOT use J3 pins 2-3 labeled "TX"/"RX" — those are GPIO43/44 (UART0
serial console). CAN uses GPIO4 and GPIO5 on J1.**

| Connection | GPIO | J1 Pin | Silkscreen | Wire | WCMCU-230 Pin |
|-----------|------|--------|-----------|------|--------------|
| Power | — | 1 | **3V3** | Dupont F-F, red | **VCC** |
| Ground | — | 22 | **GND** | Dupont F-F, black | **GND** |
| CAN TX | **GPIO5** | 5 | **5** | Dupont F-F, blue | **CTX** |
| CAN RX | **GPIO4** | 4 | **4** | Dupont F-F, green | **CRX** |
| Bus high | — | — | — | 22 AWG, yellow | **CAN_H** (screw term) |
| Bus low | — | — | — | 22 AWG, green | **CAN_L** (screw term) |
| Bus ground | — | — | — | 22 AWG, black | **GND** (screw term) |
| Termination | — | — | — | shunt / solder blob | **120Ω jumper** |

- 120Ω jumper **MUST BE ON** — RT is a bus endpoint

#### Module B — MCP2515 SPI (High Bus, Active)

Config in `rt-esp32/src/config.h`:
```cpp
constexpr int kSpiSckGpio  = 15;
constexpr int kSpiMosiGpio = 16;
constexpr int kSpiMisoGpio = 17;
constexpr int kSpiCsGpio   = 18;
constexpr int kMcpIntGpio  = 47;
```

The MCP2515 module is RT's second CAN interface. In this test we inject `0x300`
(Host drive) and `0x7FC` (Host heartbeat) on the high bus via CANalyst-II Ch1,
and watch RT forward `0x204`/`0x205`/`0x169` to the low bus.

| Connection | GPIO | Silkscreen | Wire | MCP2515 Pin |
|-----------|------|-----------|------|------------|
| SPI clock | **GPIO15** | board GPIO15 | Dupont F-F | **SCK** |
| SPI MOSI | **GPIO16** | board GPIO16 | Dupont F-F | **MOSI** (SI) |
| SPI MISO | **GPIO17** | board GPIO17 | Dupont F-F | **MISO** (SO) |
| SPI chip select | **GPIO18** | board GPIO18 | Dupont F-F | **CS** |
| Interrupt | **GPIO47** | board GPIO47 | Dupont F-F | **INT** |
| Power | — | module-specific | Dupont F-F, red | **VCC** |
| Ground | — | **GND** (J1-22) | Dupont F-F, black | **GND** |
| Bus high | — | — | 22 AWG, yellow | **CAN_H** (screw term) |
| Bus low | — | — | 22 AWG, green | **CAN_L** (screw term) |

- **MCP2515 CAN_H/CAN_L connect to the high bus backbone** — this is a separate physical pair from the low bus.
- **ESP32-S3 GPIOs are not 5 V tolerant.** A 5 V MCP2515/TJA1050 module must use level translation for every MCU-facing SPI/INT signal. A 3.3 V controller/transceiver design is the alternative.
- **No termination needed on the MCP2515 module** — the CANalyst-II Ch1 provides the 120Ω terminator for the high bus (software-enabled via the backend).

### 3.3 Controller Wiring — SYS ESP32-S3

SYS has **one** CAN module: WCMCU-230 on the low bus only. The MCP2515 high
bus is RT's responsibility. Config in `sys-esp32/src/config.h`:
```cpp
constexpr int kCanTxGpio = 5;   // TWAI TX → transceiver CTX
constexpr int kCanRxGpio = 4;   // TWAI RX → transceiver CRX
```

Same physical wiring as RT:

| Connection | GPIO | J1 Pin | Silkscreen | Wire | WCMCU-230 Pin |
|-----------|------|--------|-----------|------|--------------|
| Power | — | 1 | **3V3** | Dupont F-F, red | **VCC** |
| Ground | — | 22 | **GND** | Dupont F-F, black | **GND** |
| CAN TX | **GPIO5** | 5 | **5** | Dupont F-F, blue | **CTX** |
| CAN RX | **GPIO4** | 4 | **4** | Dupont F-F, green | **CRX** |
| Bus high | — | — | — | 22 AWG, yellow | **CAN_H** (screw term) |
| Bus low | — | — | — | 22 AWG, green | **CAN_L** (screw term) |
| Bus ground | — | — | — | 22 AWG, black | **GND** (screw term) |
| Termination | — | — | — | shunt / solder blob | **120Ω jumper** |

> **If a board's frames never appear:** Try swapping CTX ↔ CRX on that board's
> WCMCU-230. Some module batches have the silk screen labels swapped.

### 3.4 Monitor Wiring — CANalyst-II

The CANalyst-II rear panel has two green 3-pin pluggable terminal blocks.
**Both channels are used** — Ch0 on the low bus, Ch1 on the high bus.

```
         CANalyst-II rear panel
    ┌───────────────────────────────┐
    │  ┌──── Ch0 ────┐ ┌── Ch1 ──┐ │
    │  │             │ │         │ │
    │  │ H  L  G     │ │ H  L G  │ │
    │  │ ●  ●  ●     │ │ ●  ● ●  │ │  ← pluggable green terminals
    │  └──┬──┬──┬────┘ └─┬──┬──┬─┘ │
    │     │  │  │         │  │  │   │
    └─────┼──┼──┼─────────┼──┼──┼───┘
          │  │  │         │  │  │
     yellow green black yellow green black
          │  │  │         │  │  │
     LOW BUS backbone    HIGH BUS backbone
     (shared with        (shared with
      RT + SYS WCMCU)     RT MCP2515 only)
```

| Channel | Bus | Connects To | Termination |
|---------|-----|------------|-------------|
| **Ch0** | Low | RT WCMCU-230 + SYS WCMCU-230 | ❌ Off (RT + SYS provide 120Ω ∥ 120Ω = 60Ω) |
| **Ch1** | High | RT MCP2515 | ✅ On (sole terminator — MCP2515 has none) |

### 3.5 Bus Backbones — Low and High

There are **two independent CAN buses**. They share no electrical connections.

#### Low Bus (RT ↔ SYS)

```
   RT WCMCU-230          SYS WCMCU-230         CANalyst-II Ch0
   screw terminals       screw terminals       pluggable terminal
   ┌──────────┐         ┌──────────┐         ┌──────────────┐
   │ CAN_H ●──┼────┬────┼──● CAN_H │    ┌────┼──● H         │
   │ CAN_L ●──┼──┬─┼────┼──● CAN_L │    │ ┌──┼──● L         │
   │ GND   ●──┼──│─┼────┼──● GND   │    │ │  │              │
   └──────────┘  │ │     └──────────┘    │ │  └──────────────┘
                 │ │                     │ │
        yellow ──┘ │                     │ │
        green  ───┘                     │ │
        black ─────────────────────────┘ │
        black ───────────────────────────┘

   Termination: RT 120Ω ∥ SYS 120Ω = 60Ω (both WCMCU-230 jumpers ON, CANalyst-II OFF)
```

#### High Bus (RT MCP2515 ↔ CANalyst-II)

```
   RT MCP2515                      CANalyst-II Ch1
   screw terminals                 pluggable terminal
   ┌──────────┐                   ┌──────────────┐
   │ CAN_H ●──┼── yellow ────────┼──● H         │
   │ CAN_L ●──┼── green  ────────┼──● L         │
   │ GND   ●──┼── black  ────────┼──● G         │
   └──────────┘                   └──────────────┘

   Termination: CANalyst-II Ch1 = 120Ω (software-ON via backend).
   MCP2515 module has NO termination — CANalyst-II is the sole terminator.
```

| Bus | Nodes | CAN_H Wire | CAN_L Wire | GND Wire | Termination |
|-----|-------|-----------|-----------|---------|-------------|
| Low | RT WCMCU, SYS WCMCU, CANalyst-II Ch0 | yellow | green | black | 60Ω (two 120Ω ∥) |
| High | RT MCP2515, CANalyst-II Ch1 | yellow | green | black | 120Ω (CANalyst-II Ch1 only) |

> Use **different colored wire** for high vs low bus to avoid accidentally
> bridging them. E.g., yellow/green/black for low bus, orange/blue/gray for high.

### 3.6 Pre-Power Checklist

Before plugging in USB:

**RT — WCMCU-230 (low bus):**
- [ ] J1-1 **"3V3"** → WCMCU-230 **VCC** (red Dupont)
- [ ] J1-22 **"GND"** → WCMCU-230 **GND** (black Dupont)
- [ ] J1-5 **GPIO5** (silkscreen "5") → WCMCU-230 **CTX** (blue Dupont)
- [ ] J1-4 **GPIO4** (silkscreen "4") → WCMCU-230 **CRX** (green Dupont)
- [ ] 120Ω termination jumper **ON**

**RT — MCP2515 (high bus):**
- [ ] **GPIO15** → MCP2515 **SCK** (through level translation if module is 5 V)
- [ ] **GPIO16** → MCP2515 **MOSI** (through level translation if module is 5 V)
- [ ] **GPIO17** ← MCP2515 **MISO** (through level translation if module is 5 V)
- [ ] **GPIO18** → MCP2515 **CS** (through level translation if module is 5 V)
- [ ] **GPIO47** ← MCP2515 **INT** (through level translation if module is 5 V)
- [ ] MCP2515 **VCC** matches the selected module and level-translation design
- [ ] MCP2515 **GND** → J1-22 "GND"

**SYS — WCMCU-230 (low bus):**
- [ ] J1-1 **"3V3"** → WCMCU-230 **VCC** (red Dupont)
- [ ] J1-22 **"GND"** → WCMCU-230 **GND** (black Dupont)
- [ ] J1-5 **GPIO5** (silkscreen "5") → WCMCU-230 **CTX** (blue Dupont)
- [ ] J1-4 **GPIO4** (silkscreen "4") → WCMCU-230 **CRX** (green Dupont)
- [ ] 120Ω termination jumper **ON**

**Low bus backbone:**
- [ ] CAN_H: RT WCMCU → SYS WCMCU → CANalyst-II Ch0 **H** (yellow)
- [ ] CAN_L: RT WCMCU → SYS WCMCU → CANalyst-II Ch0 **L** (green)
- [ ] GND: RT WCMCU → SYS WCMCU → CANalyst-II Ch0 **G** (black)

**High bus backbone:**
- [ ] CAN_H: RT MCP2515 → CANalyst-II Ch1 **H** (orange or different color)
- [ ] CAN_L: RT MCP2515 → CANalyst-II Ch1 **L** (blue or different color)
- [ ] GND: RT MCP2515 → CANalyst-II Ch1 **G** (gray)

**Multimeter checks (power off):**
- [ ] Low bus CAN_H ↔ CAN_L = **~60 Ω** (two 120Ω ∥)
- [ ] High bus CAN_H ↔ CAN_L = **~120 Ω** (CANalyst-II Ch1 terminator)
- [ ] Low bus CAN_H ↔ high bus CAN_H = **∞** (buses are isolated)
- [ ] All CAN pins → GND > 1 kΩ (no shorts)

**PC side:**
- [ ] CANalyst-II USB plugged in, Zadig WinUSB driver installed

### 3.7 Power-On Voltage Check

After plugging in both ESP32s (USB power):

- **CAN_H to GND:** ~2.5 V (recessive/idle state)
- **CAN_L to GND:** ~2.5 V (recessive/idle state)
- **CAN_H to CAN_L:** ~0 V (idle — no frames transmitting yet)

When the boards start sending frames, you'll see brief pulses where CAN_H
rises to ~3.5 V and CAN_L drops to ~1.5 V. A multimeter is too slow to
catch these — the debug-tool UI will show you the frames directly.

## 4. Software & Firmware

### 4.1 Install Debug Tool Dependencies

```powershell
# Install dependencies (one-time)
cd debug-tool\backend && npm install
cd debug-tool\ui && npm install

# CANalyst-II: Download Zadig from https://zadig.akeo.ie/
#  1. Plug in CANalyst-II
#  2. Run Zadig as Administrator
#  3. Options → List All Devices
#  4. Select "CANalyst-II" (or "STM32 Virtual ComPort") → WinUSB driver → Replace Driver
```

### 4.2 Flash Firmware

Both RT and SYS firmware work **as-is — no code changes required.** Flash the
current `main` branch:

```powershell
cd rt-esp32  && pio run -t upload
cd sys-esp32 && pio run -t upload
```

Verify each board boots via serial monitor (115200 baud):
- RT prints: `RT ESP32-S3 boot` … `Ready — 8 tasks`
- SYS prints: `SYS ESP32-S3 initializing...` … `Ready — 15 tasks running. Mode=MANUAL`

> **Note:** RT may log `MCP2515 not in config mode after reset` — this is expected;
> the MCP2515 (high bus) is not installed. RT continues booting normally.

---

## 5. Start the Debug Tool

### 5.1 Backend (CANalyst-II mode — dual bus)

```powershell
cd debug-tool\backend
$env:CAN_TRANSPORT = "canalystii"
$env:CANALYST_BITRATE = "500000"
$env:CANALYST_CH0_BUS = "low"
$env:CANALYST_CH1_BUS = "high"
$env:CANALYST_CH1_TERM = "true"    # Ch1 is sole terminator on high bus
npm run dev
```

Expected output:
```
CANalyst-II bridge: connected (device 0)
  ch0 → low bus   (500000 bit/s)
  ch1 → high bus  (500000 bit/s, terminator ON)
Server listening on http://127.0.0.1:3000
```

### 5.2 UI

```powershell
cd debug-tool\ui
npm run dev
```

Open **http://localhost:5173** in a browser.

---

## 6. What Happens Naturally (No Injection)

With both ESP32s powered and both CAN buses wired, open the UI at
http://localhost:5173. Use the **bus tabs** to switch views.

### High Bus tab

Frames RT produces with no Host input:

| CAN ID | Name | Sender | Period | Content |
|--------|------|--------|--------|---------|
| `0x7FD` | RT_HEARTBEAT | RT | 2 Hz | `alive_ctr` incrementing |
| `0x210` | RT_STATE_RPT | RT | 10 Hz | `mode=0, safety_state=0, reversing=0` |
| `0x220` | RT_PID_RPT | RT | 10 Hz | all zeros (shadow PID, no MTR feedback) |

RT also **forwards low→high**: `0x011` (SYS safety), `0x600` (SYS diag). These
won't appear until SYS is powered and sending them on the low bus.

### Low Bus tab

| CAN ID | Name | Sender | Period | Content |
|--------|------|--------|--------|---------|
| `0x7FD` | RT_HEARTBEAT | RT | 2 Hz | `alive_ctr` incrementing |
| `0x7FE` | SYS_HEARTBEAT | SYS | 10 Hz | `alive_ctr` incrementing |
| `0x011` | SYS_SAFETY_STS | SYS | 5 Hz | `estop_active=0, heartbeat_ok=1` |
| `0x110` | SYS_MODE_CMD | SYS | on change | `mode=0` (MANUAL) |
| `0x204` | RT_DRIVE_CMD | RT | 100 Hz | `speed=0, gear=N` (idle — no Host cmd yet) |
| `0x600` | SYS_DIAG_RPT | SYS | 1 Hz | `mode=0, estop=0, hb_ok=1` |

RT also **forwards high→low**: `0x302` (if injected on high bus). When Host `0x300`
is injected, RT generates `0x205` (brake cmd, 50 Hz) and `0x169` (steer cmd, 50 Hz)
on the low bus.

### Baseline Checks

| Check | Where | How |
|-------|-------|-----|
| RT heartbeat OK | Low bus `0x011` | `heartbeat_ok=1` |
| No ESTOP | Low bus `0x011` | `estop_active=0` |
| RT high bus alive | High bus `0x7FD` | counter increments |
| SYS alive | Low bus `0x7FE` | counter increments |
| Mode = MANUAL | Low bus `0x110` | `mode=0` |
| RT sending idle drive | Low bus `0x204` | `speed=0, gear=N` at 100 Hz |

---

## 7. Mimic Absent Nodes — Injection Guide

Four nodes are missing. Inject the high-bus ones to exercise the full pipeline,
and the low-bus ones to simulate actuators.

| Node | Bus | What to Inject | Why |
|------|-----|---------------|-----|
| **Host (Jetson)** | High | `0x300`, `0x301`, `0x7FC` | RT needs these to generate `0x204`/`0x205`/`0x169` on the low bus |
| **MTR (STM32)** | Low | `0x120`, `0x206` | EGAS L2 check, RT PID telemetry |
| **EPS-C (steering)** | Low | `0x201` | RT steering state machine — prevents FAULT after 5s |
| **SEB (brake)** | Low | `0x721` | SYS brake staleness check — prevents log spam |

### 7.1 Quick-Start: Inject All at Once

Save as `start-bench-injections.ps1`:

```powershell
# start-bench-injections.ps1
# Mimics Host (high bus) + MTR + EPS-C + SEB (low bus) for RT↔SYS bench test.
# Requires debug-tool backend running on :3000 with CANalyst-II (dual bus).

param(
  [string]$Backend = "http://localhost:3000"
)

$headers = @{ "Content-Type" = "application/json" }

# ── HIGH BUS: Host (Jetson) ─────────────────────────────────────
$highBus = @(
  @{ bus="high"; id="0x300"; dlc=8; data=@(0,0,0,0,0,0,0,0); ms=10;  desc="Host drive (0 mm/s, N) → RT forwards as 0x204" },
  @{ bus="high"; id="0x301"; dlc=4; data=@(0,0,0,0);         ms=20;  desc="Host brake (0 kPa)" },
  @{ bus="high"; id="0x7FC"; dlc=1; data=@(1);               ms=500; desc="Host heartbeat (alive_ctr=1)" }
)

# ── LOW BUS: MTR, EPS-C, SEB ───────────────────────────────────
$lowBus = @(
  @{ bus="low";  id="0x120"; dlc=2; data=@(0,0);             ms=10;  desc="MTR throttle (0 mm/s)" },
  @{ bus="low";  id="0x206"; dlc=4; data=@(0,0,0,0);         ms=20;  desc="MTR motor feedback (speed=0, N)" },
  @{ bus="low";  id="0x201"; dlc=8; data=@(1,0,0,0,0,0,0,0); ms=10; desc="EPS-C status (angle=0, OK)" },
  @{ bus="low";  id="0x721"; dlc=8; data=@(1,0,0,0,0,0,0,0); ms=10; desc="SEB status (stroke=0, OK)" }
)

$all = @($highBus) + @($lowBus)

Write-Host "Starting periodic CAN injections..." -ForegroundColor Cyan
foreach ($inj in $all) {
  $body = @{
    action      = "start"
    bus         = $inj.bus
    id          = $inj.id
    dlc         = $inj.dlc
    data        = $inj.data
    interval_ms = $inj.ms
  } | ConvertTo-Json

  try {
    $null = Invoke-RestMethod -Uri "$Backend/api/cmd/periodic" -Method Post -Headers $headers -Body $body
    Write-Host "  OK  [$($inj.bus)] $($inj.desc) — $($inj.id) @ $($inj.ms)ms" -ForegroundColor Green
  } catch {
    Write-Host "  FAIL [$($inj.bus)] $($inj.desc) — $($_.Exception.Message)" -ForegroundColor Red
  }
}

Write-Host ""
Write-Host "All injections running. Open http://localhost:5173 to monitor both buses." -ForegroundColor Cyan
```

Run it:
```powershell
cd E:\doc\etrike
.\start-bench-injections.ps1
```

### 7.2 Inject Individually (via REST)

```powershell
$backend = "http://localhost:3000"
$headers = @{ "Content-Type" = "application/json" }

# ── HIGH BUS — Host (Jetson) ────────────────────────────────────

# Host drive command — 2.0 m/s, D gear, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="high"; id="0x300"; dlc=8; data=@(0,0,7,0xD0,0,0,0,1); interval_ms=10
} | ConvertTo-Json)

# Host heartbeat — 2 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="high"; id="0x7FC"; dlc=1; data=@(1); interval_ms=500
} | ConvertTo-Json)

# ── LOW BUS — MTR, EPS-C, SEB ───────────────────────────────────

# MTR throttle — 0 mm/s, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x120"; dlc=2; data=@(0,0); interval_ms=10
} | ConvertTo-Json)

# MTR motor feedback — 0 mm/s, gear N, 50 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x206"; dlc=4; data=@(0,0,0,0); interval_ms=20
} | ConvertTo-Json)

# EPS-C status — angle 0°, OK, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x201"; dlc=8; data=@(1,0,0,0,0,0,0,0); interval_ms=10
} | ConvertTo-Json)

# SEB status — stroke 0, OK, 100 Hz
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="start"; bus="low"; id="0x721"; dlc=8; data=@(1,0,0,0,0,0,0,0); interval_ms=10
} | ConvertTo-Json)

# Stop a specific injection (example)
Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
  action="stop"; bus="high"; id="0x300"
} | ConvertTo-Json)
```

### 7.3 Inject via UI

The **Injector** tab offers a form-based interface:
1. Select **Low Bus**
2. Pick the CAN ID from the dropdown (filtered to low-bus IDs only)
3. Fill in decoded field values
4. Click **▶ Send Periodic** for repeating injection, or **Send Once** for a single frame

The keyboard shortcuts also work on the low bus:
| Key | Low Bus Action |
|-----|---------------|
| `W` / `S` | `0x204` speed ±200 |
| `A` / `D` | `0x169` angle ±5° |
| `Space` ×2 | `0x001` ESTOP |
| `B` / `R` | `0x205` brake kPa set/release |
| `Esc` | Zero `0x204` + `0x205` + `0x169` |

---

## 8. Test Scenarios

### 8.1 Scenario A — Heartbeat Exchange (Passive)

**Goal:** Confirm both CAN buses are alive.

1. Start backend + UI (section 5). Do NOT run injections yet.
2. Power RT and SYS.
3. **High Bus tab:** verify `0x7FD`, `0x210` from RT.
4. **Low Bus tab:** verify `0x7FD` (RT), `0x7FE` (SYS), `0x011`, `0x204`, `0x600`.

**Pass:** Both buses show frames. `heartbeat_ok=1` in `0x011`. No ESTOP.

### 8.2 Scenario B — Full Pipeline: Host→RT→SYS

**Goal:** Inject `0x300` on the high bus, watch RT forward `0x204` to the low
bus, verify SYS consumes it.

1. Start all baseline injections (section 7.1).
2. Inject Host drive at 2.0 m/s on the high bus:
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="high"; id="0x300"; dlc=8; data=@(0,0,7,0xD0,0,0,0,1)
   } | ConvertTo-Json)
   ```
   (speed_mmps=2000 = 0x07D0 big-endian in bytes 2-3, gear=1 [D] in byte 6)

3. **High Bus tab:** injected `0x300` appears.
4. **Low Bus tab (within 10 ms):** RT generates `0x204{speed=2000, gear=D}`.
5. SYS dispatch updates `g_setpoint_speed_mmps` → 2000. DAC output changes
   proportionally (2000/3000 × Vref on MCP4725 I2C 0x60).

**Pass:** `0x300` on high bus → `0x204` on low bus with matching speed/gear.
The PipelineView in the debug-tool shows the 0x300→0x204 chain.

### 8.3 Scenario C — ESTOP via CAN

**Goal:** Verify ESTOP propagation and both nodes' reactions.

1. Inject ESTOP (requires `confirm_estop: true`):
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x001"; dlc=0; data=@(); confirm_estop=$true
   } | ConvertTo-Json)
   ```

2. Observe:
   - SYS `0x011` → `estop_active=1`
   - SYS `0x110` → mode changes to ESTOP (mode=2)
   - RT's `t_dispatch` → queues `SafetyEvent::ESTOP` → control loop zeroes setpoints
   - RT sends `0x001` on both buses (low bus via TWAI, high bus via MCP2515 — the
     latter fails silently since MCP2515 is disconnected)
   - SYS indicator bulbs switch (AUTO → off, MANUAL → off in ESTOP)
   - SYS 12V power relay opens (`kPower12vRelay` → off)
   - SYS DAC output → 0V

**Pass:** Both nodes enter ESTOP. `0x011` shows `estop_active=1`. RT `0x204`
drops to speed=0, gear=N.

### 8.4 Scenario D — RT Heartbeat Loss → SYS ESTOP

**Goal:** Verify SYS detects RT heartbeat timeout and enters ESTOP.

1. Stop the RT board (disconnect USB or press RST and hold).
2. Watch SYS behavior:
   - After 1000ms (`kHeartbeatTimeoutMsRt`): `0x011` → `heartbeat_ok=0`
   - SYS safety task: `estop_triggered = true` (heartbeat not OK)
   - SYS enters ESTOP, sends CAN `0x001`, broadcasts `0x011` with `estop_active=1`
   - SYS DAC → 0V, 12V relay opens
3. Reconnect RT.
4. After RT resumes sending `0x7FD`: SYS `heartbeat_ok` returns to 1.
   ESTOP must be manually cleared (START button or mode long-press per gap #11).

**Pass:** SYS detects heartbeat loss within ~1s. SYS enters ESTOP.

### 8.5 Scenario E — Mode Change (MANUAL ↔ AUTO)

**Goal:** Verify mode transitions work over CAN.

1. Inject AUTO mode:
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x110"; dlc=1; data=@(1)
   } | ConvertTo-Json)
   ```

2. Observe: `0x110` shows mode=1 (AUTO). SYS motor task switches from
   ADC pass-through to CAN setpoint mode. SYS indicator shows AUTO bulb on.

3. Inject MANUAL mode:
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/send" -Method Post -Headers $headers -Body (@{
     bus="low"; id="0x110"; dlc=1; data=@(0)
   } | ConvertTo-Json)
   ```

**Pass:** Both transitions complete without ESTOP. SYS `0x110` reflects the
change. Indicator bulbs follow mode.

### 8.6 Scenario F — SYS 0x204 Staleness

**Goal:** Verify that when `0x204` stops arriving, SYS zeros the speed setpoint
(but does NOT ESTOP — staleness zeroes, doesn't ESTOP).

1. Stop the periodic `0x204` injection (and stop RT if it's also sending 0x204):
   ```powershell
   Invoke-RestMethod -Uri "$backend/api/cmd/periodic" -Method Post -Headers $headers -Body (@{
     action="stop"; bus="low"; id="0x204"
   } | ConvertTo-Json)
   ```
   (If RT is still sending its own 0x204, this test won't work — you'd need to
   modify RT or stop both sources.)

2. After 200ms (`kSetpointStaleMs`): SYS sets `g_setpoint_speed_mmps=0`,
   `g_setpoint_gear=N`.

3. Restart `0x204` injection — speed setpoint recovers on next frame.

**Pass:** SYS logs no ESTOP. SYS zeros speed internally. SYS recovers when
`0x204` resumes.

---

## 9. What Each Injection Mimics

| Real Node | Bus | CAN IDs It Sends | What to Inject | Why |
|-----------|-----|-----------------|----------------|-----|
| **Host (Jetson)** | High | `0x300` (≤100 Hz), `0x301`, `0x7FC` (2 Hz) | All three | RT consumes `0x300` → generates `0x204`/`0x169` on low bus. `0x7FC` needed for RT heartbeat tracking. Without Host input RT sends idle `0x204{speed=0,N}`. |
| **MTR (STM32)** | Low | `0x120` (100 Hz), `0x206` (50 Hz) | Both | SYS EGAS L2 checks `0x206` actual vs `0x204` cmd in AUTO mode. RT forwards `0x120` low→high for telemetry. |
| **EPS-C (steering)** | Low | `0x201` (100 Hz), `0x202` (10 Hz) | `0x201` min | RT steering needs `0x201` angle feedback. Without it, LISTEN_SYNC times out after 5s → FAULT. |
| **SEB (brake)** | Low | `0x721` (100 Hz), `0x731` (10 Hz) | `0x721` min | SYS brake checks `0x721` staleness (100ms). Without it, SYS logs warnings every 1s. |

**Minimum injection set** for a quiet bench: `0x201` (keep RT steering happy),
`0x721` (keep SYS brake happy), `0x120` + `0x206` (keep EGAS L2 happy).

If you skip all injections, RT and SYS still exchange heartbeats and basic status
frames. SYS stays in MANUAL mode. RT steering stays in BOOT_WAIT/LISTEN_SYNC.
This is fine for basic connectivity testing.

---

## 10. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No frames in UI | CANalyst-II not connected | Check USB, Zadig driver, `CAN_TRANSPORT=canalystii` |
| Only one board's frames visible | CAN wiring open/short, or one WCMCU-230 TX not working | Check CAN-H/CAN-L continuity. Verify 120Ω termination. Swap WCMCU-230 modules between boards — if the silent board follows the module, the module's SN65HVD230 may be counterfeit. |
| One board receives but doesn't transmit | Counterfeit/fake SN65HVD230 chip | Common on WCMCU-230 modules from AliExpress. Transceiver can listen but not drive the bus. Swap modules to confirm, then replace the bad one. |
| SYS stuck in ESTOP | RT heartbeat missing (`0x7FD`) | Check RT is powered, CAN wired correctly, CTX/CRX not swapped |
| `heartbeat_ok=0` in `0x011` | RT `0x7FD` not arriving at SYS | SYS TWAI RX issue or CAN bus wiring |
| RT steering FAULT | No `0x201` (EPS-C status) injected | Start `0x201` periodic injection (section 7) |
| SYS CAN bus-off errors | No termination resistor | Add 120Ω across CAN-H/CAN-L on both WCMCU-230 modules (jumper ON) |
| CANalyst-II `connect timeout` | Wrong device or driver | Re-run Zadig, check Device Manager for "WinUSB" device |
| `Cmd too long` or API errors | Backend not running | Start backend first (`npm run dev` in `debug-tool/backend`) |
| RT serial: `MCP2515 not in config mode` | Normal — MCP2515 not installed | Expected. RT continues booting. No action needed. |

### Quick CAN Bus Health Check

Measure with a multimeter (power off):
- **CAN-H to CAN-L:** ~60Ω (two 120Ω in parallel) if both modules have termination
- **CAN-H to GND:** >1kΩ (should not be shorted)
- **CAN-L to GND:** >1kΩ (should not be shorted)
- **CAN-H to VCC (3.3V):** >1kΩ

With power on and bus idle:
- **CAN-H:** ~2.5V (recessive)
- **CAN-L:** ~2.5V (recessive)
- During active traffic, CAN-H swings to ~3.5V, CAN-L to ~1.5V (dominant).

---

## 11. Code Changes Required

**None.** Both RT and SYS firmware work as-is for this bench test.

What happens for each subsystem with the partial bus:

| Subsystem | Behavior |
|-----------|----------|
| RT `Mcp2515Driver::init()` | Returns `false` — no MCP2515 on SPI. `app_main` does not check the return value. RT boots normally. |
| RT high-bus RX/TX tasks | Spin harmlessly — `send()`/`receive()` return `false` immediately when `m_initialized=false`. |
| RT control loop | Runs at 100 Hz. No Host commands arrive → `cmd={0,0}`. Physics resolves to zero speed/steer. Setpoint published to `g_setpoint_q`. |
| RT `t_can_tx_low` | Sends `0x204` at 100 Hz with `{speed=0, gear=N}`. Steering state machine gates drive (requires ACTIVE/ESTOP states). Sends `0x205` only in non-MANUAL mode. Sends `0x169` only in non-MANUAL mode. Sends `0x7FD` at 2 Hz. |
| RT forwarding | Low→high forwards go to MCP2515 (fail silently). High→low receives nothing (no high bus). No effect on the bench test. |
| SYS `task_safety` | GPIOs pulled up → ESTOP button and brake lever read as "not pressed." `heartbeat_ok()` checks RT `0x7FD`. EGAS L2 only runs in AUTO mode. |
| SYS `task_motor` | In MANUAL mode: reads physical throttle ADC → DAC. In AUTO mode: uses CAN `0x204` setpoint with staleness check. |
| SYS `task_brake` | Sends `0x7B9` to SEB in MANUAL/ESTOP (suppressed in AUTO per gap #12). If `0x721` missing >100ms, logs staleness warning (no ESTOP). |
| SYS mode manager | Starts in MANUAL. Listens for button presses (none pressed due to pull-ups). Transitions on CAN `0x110` or `0x001`. |

### Optional Firmware Tweaks

If you want RT to generate non-zero drive commands without a Host, add this
to `rt-esp32/src/main.cpp` in the control task, after `cmd = {0, 0}`:

```cpp
// BENCH TEST: generate a small forward speed without Host
// Remove before connecting to real hardware!
if (xQueueReceive(g_cmd_q, &cmd, 0) != pdTRUE) {
    cmd = {500, 0, uint8_t(can::Gear::D)};  // 0.5 m/s forward, D gear
}
```

This makes RT send `0x204{speed=500, gear=D}` at 100 Hz, which SYS will use in
AUTO mode to output a non-zero DAC voltage. **Do not commit this change.**

---

## 12. Shutdown Procedure

1. Stop periodic injections (or just stop the backend — all injections auto-cancel):
   ```powershell
   # Stop each injection
   foreach ($id in @("0x120","0x206","0x201","0x721","0x204")) {
     Invoke-RestMethod -Uri "http://localhost:3000/api/cmd/periodic" -Method Post `
       -Headers @{"Content-Type"="application/json"} `
       -Body (@{action="stop"; bus="low"; id=$id} | ConvertTo-Json) 2>$null
   }
   ```
2. Ctrl+C in the backend terminal.
3. Ctrl+C in the UI terminal.
4. Disconnect USB power from RT and SYS.

---

## 13. Hardware Verification Results (2026-07-03)

Both RT and SYS boards were tested with vehicle firmware on the low CAN bus.
No Host, MTR, EPS-C, or SEB connected.

### 13.1 Hardware Under Test

| Board | Chip | MAC | Notes |
|-------|------|-----|-------|
| RT | ESP32-S3 rev v0.2, 8MB PSRAM | `80:b5:4e:c7:d0:34` | Dual CAN (TWAI + MCP2515 via SPI) |
| SYS | ESP32-S3 rev v0.2, 8MB PSRAM | `80:b5:4e:c5:b9:4c` | TWAI only, USB-Serial/JTAG |

### 13.2 Low CAN Bus (TWAI) — Working

Bidirectional traffic at 500 kbit/s verified with two independent tests:

1. **Minimal test** (`can-test/src/main.cpp`): bare `app_main` loop, no RTOS tasks.
   Each board sends 0x555 with 4-byte counter every 500ms. Both boards receive
   each other's frames. Zero TX failures, TEC=0, REC=0, zero bus errors.

2. **Vehicle firmware**: RT and SYS running production `vehicle` environment.
   Both initialized TWAI at 500 kbit/s. Zero CAN TX failures on the low bus
   (each board ACKs the other's frames at the controller level).

### 13.3 High CAN Bus (MCP2515) — Working

RT's MCP2515 SPI communication verified with dedicated SPI test
(`can-test/src/spi_test.cpp`). Chip responds correctly: `CANSTAT=0x80`
(Configuration mode after reset). SPI bus stable at 8 MHz for 2+ minutes
continuous polling, zero errors.

In vehicle firmware, MCP2515 operates in Normal mode. Intermittent TX failures
(~17%) on the high bus are expected — no Host node present to ACK frames.
The bus-off recovery mechanism works correctly.

### 13.4 Firmware Stability

| Metric | RT | SYS |
|--------|-----|-----|
| Boot crashes | 0 | 0 |
| Watchdog events | 0 | 0 |
| Stack overflows | 0 | 0 |
| CAN TX failures (low bus) | 0 | 0 |
| CAN TX failures (high bus) | 29/168 (17%) — no Host | N/A |

### 13.5 Issues Found and Fixed

**CONFIG_FREERTOS_HZ = 100 (not 1000).** ESP-IDF Kconfig defaulted to 100 Hz
(10ms tick) despite `-D CONFIG_FREERTOS_HZ=1000` in build flags. sdkconfig.h's
unconditional `#define` overrides compiler `-D` flags. This caused:

- `pdMS_TO_TICKS(5)` = 0 → `xTaskDelayUntil` assert in RT `t_can_tx_low`
- `pdMS_TO_TICKS(1)` = 0 → MCP2515 SPI continuous polling → bus lockup + watchdog
- Stack corruption on SYS from incorrect timing (GPIO 308/238 errors)

**Fix:** `protocol/patch_sdkconfig.py` — PlatformIO pre-build script that
patches generated `sdkconfig.h` after CMake configure. Sets `CONFIG_FREERTOS_HZ=1000`
and `CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144`.

**SYS task stacks too small for 1ms tick.** Minimum was 1536 bytes — increased
all stacks ~20-33% (min now 2048). Main task stack 3584→6144.

**SYS NVS corruption** from 6600+ crash-loop reboots. Required full chip erase
(`pio run --target erase`) before clean flash.

### 13.6 Expected Bench Behavior (Vehicle Firmware)

With vehicle firmware on bench (no other ECUs):

| Symptom | Cause | Fix for clean bench testing |
|---------|-------|----------------------------|
| RT "Command stale" every 100ms | No Host sending 0x300 | Use `bench` env or inject 0x300 via CANalyst-II |
| RT high CAN TX intermittent fails | No Host to ACK MCP2515 frames | Use `bench` env (MCP2515 in ListenOnly) |
| SYS ESTOP loop (MTR ACK timeout) | No MTR to respond to ESTOP | Use `bench` env (`CONFIG_BYPASS_MTR_ABSENT`) |
| SYS floating ESTOP button | GPIO1 NC, floating = "pressed" | Pull GPIO1 high or use `bench` env (`TESTING` flag) |

### 13.7 Test Code

Minimal test projects at `can-test/`:
- `can-test/src/main.cpp` — TWAI send/receive, no RTOS tasks
- `can-test/src/spi_test.cpp` — MCP2515 SPI register dump and verification
- `can-test/platformio.ini` — `twai` and `spi` environments

Usage:
```bash
cd can-test
pio run -e twai -t upload --upload-port COM6   # TWAI test
pio run -e spi  -t upload --upload-port COM6   # SPI test (swap main.cpp first)
pio device monitor --port COM6
```

---

## 14. References

- [Architecture Overview](../architecture.md) — system topology and message catalog
- [CAN Protocol](../protocol/generated/cpp/protocol.h) — message ID constants and struct layouts
- [Debug Tool Architecture](../debug-tool/debug-tool-architecture.md) — tool design and API
- [RT Config](../rt-esp32/src/config.h) — RT timing and GPIO constants
- [SYS Config](../sys-esp32/src/config.h) — SYS timing and GPIO constants
