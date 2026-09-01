# RT-AURIX-Lite Wiring

**Hardware wiring / harness reference for the consolidated RT-only AURIX TC375 controller
on the AURIX™ Lite Kit V2.**

> **Status:** This is a **planned** wiring reference. Connections are classified by how much
> they are proven (see §2 status legend). Nothing here has been bench-validated yet; the
> document is the source of truth for harness design, not evidence of a working build.
>
> **Sources:** board facts from [`aurix.md`](aurix.md) (Lite Kit V2 Rev 2.2) and the TC37x
> datasheet. Design decisions from [`architecture.md`](architecture.md) §9.

---

## 1. Document status / safety note

- This is **not** an electrical schematic. External-module diagrams in §10 are
  **functional interconnects** — component-level resistor/capacitor/reset networks are only
  added after the exact parts are selected (then they belong in KiCad).
- All AURIX I/O is **3.3 V logic**.
- Power rails on X1/X2/X302/Shield2Go/mikroBUS have **no reverse-current protection**:
  do **not** back-feed `VEXT`, `+5V`, `+3V3`, or `VDD_USB` while the board is powered.
- The Lite Kit on-board LDOs (G1 3.3 V, G2 5 V) are limited to **1 A** total.

---

## 2. Wiring summary / status legend

Every connection row uses the same status vocabulary:

| Status | Meaning |
|--------|---------|
| **BOARD-FIXED** | Exists on the Lite Kit as delivered; not selectable. |
| **DATASHEET-VERIFIED** | The pin's alternate function is confirmed by the TC37x datasheet. |
| **DESIGN-SELECTED** | Chosen for this design's harness (exists only in the planned harness, not on the board). |
| **BRING-UP-TBD** | Depends on bring-up verification (electrical, routing, timing, or part selection). |

Column convention: `| Signal | AURIX pin | Connector | Direction | Active level | External destination | Status | Source |`

---

## 3. Power connections

| Signal | AURIX / board net | Connector | Direction | Active level | External destination | Status | Source |
|--------|-------------------|-----------|-----------|--------------|----------------------|--------|--------|
| VIN | X3 DC plug (7–14 V recommended; 5–40 V abs) | X3 | In | + | External DC supply | BOARD-FIXED | `aurix.md` §2.1 |
| USB 5 V | X4 micro-USB (~4.5 V after D1) | X4 | In | + | USB host / charger | BOARD-FIXED | `aurix.md` §2.1 |
| +5V | G2 IFX27001TFV50 output | X302-5 / mikroBUS-10 / Shield2Go-1 (via R39) | Out | +5 V | External 5 V loads | BOARD-FIXED | `aurix.md` |
| +3V3 | G1 IFX27001TFV33 output (VEXT rail) | X1-2 / X2-39 / X302-2,4 / mikroBUS-7 / Shield2Go-7 | Out | +3.3 V | MCU + logic loads | BOARD-FIXED | `aurix.md` |
| VDD_USB | D1 output (USB path) | X1-39 / X2-2 | Out | ~4.5 V | USB-derived rail | BOARD-FIXED | `aurix.md` |
| VEXT | external power rail (3.3 V) | X1-2 (+3V3/VEXT) / X302-2 (IOREF) | Out | 3.3 V | level shifters / shields | BOARD-FIXED | `aurix.md` |
| GND | board ground | X1-1,40 / X2-1,40 / X302-6,7 / mikroBUS-8,9 | — | 0 V | system ground | BOARD-FIXED | `aurix.md` |

> Alternate supply options (only when X4 is unpowered): +5 V on X302-5, or +5 V on any
> `VDD_USB`, or +7–14 V on X302-8 `VIN` — see `aurix.md` §2.1.

---

## 4. CAN_LOW

| Signal | AURIX pin | Connector | Direction | Active level | External destination | Status | Source |
|--------|-----------|-----------|-----------|--------------|----------------------|--------|--------|
| CAN_LOW_TX | `P20.8` (TXCAN0) | on-board TLE9251VSJ TXD | Out | CAN dominant/recessive | on-board transceiver → CANH/CANL header | BOARD-FIXED | `aurix.md` Table 5 |
| CAN_LOW_RX | `P20.7` (RXCAN0B) | on-board TLE9251VSJ RXD | In | — | on-board transceiver | BOARD-FIXED | `aurix.md` Table 5 |
| CAN_LOW_STB | `P20.6` (CAN_STB) | on-board TLE9251VSJ STB | Out | **drive LOW** to enable normal mode | on-board transceiver | BOARD-FIXED | `aurix.md` §2.5 |
| CANH | — | CAN header pin 1 | I/O | differential | EPS-C / SEB / MTR bus | BOARD-FIXED | `aurix.md` |
| CANL | — | CAN header pin 2 | I/O | differential | EPS-C / SEB / MTR bus | BOARD-FIXED | `aurix.md` |

**CAN_LOW termination:** on-board **120 Ω** between CANH/CANL is present. Per the
two-endpoint rule, the far physical end of the low bus still needs a second 120 Ω
termination if the Lite Kit is one end.

---

## 5. CAN_HIGH

| Property | Value | Status |
|----------|-------|--------|
| Logical bus | `CAN_HIGH` | DESIGN-SELECTED |
| TX pin | `P15.0` / TXCAN2 | DATASHEET-VERIFIED |
| RX pin | `P15.1` / RXCAN2 | DATASHEET-VERIFIED |
| Connector | mikroBUS pin 13 (TX) / pin 14 (RX) | BOARD-VERIFIED |
| MCMCAN module/node + iLLD `IfxCan_*Pin` | **CAN0 Node 2** — `IfxCan_TXD02_P15_0_OUT`, `IfxCan_RXD02A_P15_1_IN` | DATASHEET-VERIFIED (iLLD pinmap) |

```
AURIX Lite Kit                          CAN_HIGH transceiver (TBD)

mikroBUS 13 / P15.0 / TXCAN2 ────────►  TXD
mikroBUS 14 / P15.1 / RXCAN2 ◄────────  RXD

+3V3 (or +5V per part) ──────────────►  supply (VCC/VIO per part)
GND ──────────────────────────────────►  GND

                                        CANH ───── CAN_HIGH_H
                                        CANL ───── CAN_HIGH_L
```

> **Functional interconnect — not an electrical schematic.**
>
> **Tradeoff:** using P15.0/P15.1 for CAN_HIGH consumes the mikroBUS **ASCLIN1 UART**
> function (TXD0_MB/RXD0_MB). The mikroBUS connector is otherwise unused in this design.
>
> **CAN_HIGH termination:** external-transceiver PCB termination configurable / DNP by
> default; actual termination depends on physical bus endpoints.

---

## 6. Safety / rider inputs

| Signal | AURIX pin | Connector | Direction | Active level | External destination | Status | Source |
|--------|-----------|-----------|-----------|--------------|----------------------|--------|--------|
| ESTOP_BTN | `P00.0` | X2-3 | In (pull-up) | LOW = ESTOP (NC) | ESTOP push-button (shared with MTR) | physical: BOARD-VERIFIED; GPIO use: DESIGN-SELECTED; ERU routing: BRING-UP-TBD | `aurix.md` X2; arch §9.2 |
| BRAKE_LEVER | `P00.1` | X2-4 | In (pull-up) | LOW = lever pressed | brake lever switch | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |
| START_BTN | `P00.2` | X2-5 | In (pull-up) | LOW = pressed | START button (exits ESTOP) | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |
| MODE_BTN | `P00.3` | X2-6 | In (pull-up) | LOW = pressed | MODE button (MANUAL↔AUTO) | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |
| SW_LEFT_TURN | `P00.8` | X2-9 | In (pull-up) | LOW = on | handlebar left-turn switch | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |
| SW_RIGHT_TURN | `P00.10` | X2-11 | In (pull-up) | LOW = on | handlebar right-turn switch | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |
| SW_HEADLIGHT | `P00.11` | X2-14 | In (pull-up) | LOW = on | handlebar headlight switch | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |

> **ESTOP note:** the pin's physical availability is verified; its GPIO/interrupt/ERU
> configuration for the ESTOP safety path is **BRING-UP-TBD** and must be bench-proven.

---

## 7. Body outputs (relays → 12 V lamps / indicators)

| Signal | AURIX pin | Connector | Direction | Active level | External destination | Status | Source |
|--------|-----------|-----------|-----------|--------------|----------------------|--------|--------|
| LIGHT_LEFT | `P33.10` | X2-38 | Out | high = relay on | relay → 12 V left-turn lamp | DESIGN-SELECTED | `aurix.md` X2; arch §9.2 |
| LIGHT_RIGHT | `P33.11` | X1-3 | Out | high = relay on | relay → 12 V right-turn lamp | DESIGN-SELECTED | `aurix.md` X1; arch §9.2 |
| BRAKE_LIGHT | `P33.12` | X1-4 | Out | high = relay on | relay → 12 V brake lamp | DESIGN-SELECTED | `aurix.md` X1; arch §9.2 |
| HEADLIGHT | `P33.13` | X1-5 | Out | high = relay on | relay → 12 V headlamp | DESIGN-SELECTED | `aurix.md` X1; arch §9.2 |
| BULB_AUTO | `P21.4` | X1-19 | Out | high = on | relay → AUTO mode indicator | DESIGN-SELECTED | `aurix.md` X1; arch §9.2 |
| BULB_MANUAL | `P21.5` | X1-22 | Out | high = on | relay → MANUAL mode indicator | DESIGN-SELECTED | `aurix.md` X1; arch §9.2 |
| RELAY_12V | `P21.0` | X1-15 | Out | high = on | relay → 12 V accessory bus | DESIGN-SELECTED | `aurix.md` X1; arch §9.2 |

> The Lite Kit exposes only GPIO — a **relay driver board** is required (not on board). See
> §10.3 functional interconnect.

---

## 8. External watchdog

| Signal | AURIX pin | Connector | Direction | Active level | External destination | Status | Source |
|--------|-----------|-----------|-----------|--------------|----------------------|--------|--------|
| WDT_WDI | `P33.1` | X2-29 | Out | edge toggling within TPS3850-Q1 window | TPS3850-Q1 WDI | DESIGN-SELECTED; bring-up: verify level + window timing | arch §9.2 |
| WDT_RESET | — | — | In | active-low | TPS3850-Q1 RESET → MCU/system reset path | BRING-UP-TBD | TPS3850-Q1 |
| WDT_WDO | — | — | Out | — | TPS3850-Q1 WDO → diagnostic/fault input (if used) | BRING-UP-TBD | TPS3850-Q1 |

> **Correction history:** the prior `P20.9 (X1-28)` assignment was wrong — X1-28 is
> `P20.14`; `P20.9` is package pin 127 on the optional-flash INT path and is **not exposed**
> on X1. `P33.1`/X2-29 is selected instead.
>
> **WDI timing:** not a fixed 100 Hz wire fact. The TPS3850-Q1 is a **window watchdog** —
> WDI must transition inside the configured valid window. Service rate/window is a
> software/config decision based on the TPS3850-Q1 CWD/SET selection; see
> `architecture.md`.

---

## 9. Debug / programming

| Signal | AURIX pin | Connector | Direction | Notes | Status |
|--------|-----------|-----------|-----------|-------|--------|
| USB (miniWiggler) | — | X4 micro-USB | I/O | on-board miniWiggler; debug via DAS; ASCLIN0 USB-serial | BOARD-FIXED |
| DAP connector | TMS/TCK/DAP0-3, /TRST, /PORST, VREF | 10-pin DAP | I/O | external debugger; LED5 must be off (miniWiggler inactive) | BOARD-FIXED |
| ASCLIN0 UART | `P14.0`/`P14.1` | — | I/O | miniWiggler USB-serial path — **not available for CAN** | BOARD-FIXED |

---

## 10. External hardware (functional interconnects)

### 10.1 CAN_HIGH transceiver (TBD)

Shown in §5. Requirements (from `architecture.md` §9.1.2): ISO 11898-2, 500 kbit/s,
TC375-compatible 3.3 V I/O, automotive-qualified preferred, defined standby/enable,
external CANH/CANL, configurable termination.

### 10.2 TPS3850-Q1 watchdog

```
AURIX P33.1 / X2-29 ────────────►  TPS3850-Q1 WDI
TPS3850-Q1 RESET ───────────────►  MCU/system reset path (active-low)
TPS3850-Q1 WDO ─────────────────►  diagnostic/fault input (optional)

supply ─────────────────────────►  TPS3850-Q1 VDD
GND ────────────────────────────►  TPS3850-Q1 GND
```

### 10.3 Relay driver board (lights / indicators / 12 V)

```
AURIX GPIO (P33.10/11/12/13, P21.4/5, P21.0) ──► relay driver inputs
relay driver outputs ──────────────────────────► 12 V lamp / accessory loads
12 V rail ─────────────────────────────────────► relay common / coil supply
GND ───────────────────────────────────────────► coil return / lamp return
```

---

## 11. Reserved / unavailable interfaces

Negative constraints (things that must **not** be assumed available):

| Interface / pin | Status | Reason |
|-----------------|--------|--------|
| mikroBUS ASCLIN1 UART (TXD0_MB/RXD0_MB on P15.0/P15.1) | **UNAVAILABLE** | Reassigned to CAN_HIGH (TXCAN2/RXCAN2). |
| P15.4 / P15.5 | **DO NOT USE FOR CAN** | No TC37x CAN alternate function (GPIO/GTM/I²C/QSPI/ERU only). |
| P14.0 / P14.1 | **NOT CAN** | ASCLIN0 USB-serial path (miniWiggler). |
| P20.9 | **NOT ON X1** | Package pin 127, optional-flash INT path (R68 → flash INT#/DNU). |
| P33.7 | keep free / repurpose deliberately | Connected to on-board Ethernet PHY interrupt (INT_ETH). |
| P22.0–P22.3 | conditionally available | Optional Semper flash circuitry. |
| P23.1 | conditionally available | Optional F-RAM circuitry. |
| P20.14 | conditionally available | Shield2Go MOSI on X1-28. |

---

## 12. Unresolved hardware decisions

| Decision | Status | Notes |
|----------|--------|-------|
| CAN_HIGH MCMCAN module/node | **RESOLVED** | CAN0 Node 2 (iLLD `IfxCan_PinMap_TC37x_LQFP176`). |
| CAN_HIGH iLLD `IfxCan_*Pin` symbols | **RESOLVED** | `IfxCan_TXD02_P15_0_OUT`, `IfxCan_RXD02A_P15_1_IN`. |
| CAN_HIGH transceiver part | BRING-UP-TBD | Requirements in architecture §9.1.2. |
| WDT_WDI electrical level / window timing | BRING-UP-TBD | Verify against TPS3850-Q1 selected CWD/SET config. |
| Relay driver board part / pinout | BRING-UP-TBD | Selected when the relay board is chosen. |
| WDI fallback GPIO candidates | DESIGN-SELECTED (fallback) | P33.2/X2-30, P33.3/X2-31, P00.9/X2-12. |

Free-pin classification for future harness use:

- **CLEAN HEADER GPIO:** P00.9, P00.12, P33.0…P33.6, P15.4, P15.5, P20.1.
- **CONDITIONALLY AVAILABLE / BOARD-FUNCTION-CONNECTED:** P22.x (Semper flash), P23.1
  (F-RAM), P20.14 (Shield2Go MOSI), P33.7 (Ethernet PHY INT).

---

## 13. Source references

- [`aurix.md`](aurix.md) — AURIX™ Lite Kit V2 board manual (Rev 2.2): connector tables,
  power, CAN0, debug, schematic pin maps.
- [`architecture.md`](architecture.md) — consolidated RT-only design, §9 hardware pins,
  §12 config constants.
- TC37x datasheet — alternate-function tables (TXCAN2/RXCAN2 on P15.0/P15.1; P15.4/P15.5
  not CAN-capable).
- TPS3850-Q1 datasheet — window-watchdog WDI/RESET/WDO behavior.

---

## 14. Change log

| Date | Change |
|------|--------|
| 2026-08-31 | Corrected CAN_HIGH: `P15.5`/`P15.4` (no TC37x CAN function) → **`P15.0`/`P15.1` (TXCAN2/RXCAN2)**, mikroBUS 13/14; noted mikroBUS ASCLIN1 UART loss. |
| 2026-08-31 | Corrected WDT WDI: `P20.9`/X1-28 (wrong; X1-28=`P20.14`) → **`P33.1`/X2-29** (DESIGN-SELECTED). Removed fixed "100 Hz" as a wire fact. |
| 2026-08-31 | Renamed watchdog part **TPS3850 → TPS3850-Q1**; CAN_HIGH transceiver → TBD with requirements; added two-120 Ω termination rule. |
