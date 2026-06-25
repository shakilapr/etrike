# Wiring Harness — Critical Issues Analysis

17 issues identified in review of `docs/wiring-harness.md` (2026-06-26).
Findings verified against `architecture.md`, `sys-esp32/src/config.h`, `mtr-stm32/src/config.h`, `rt-esp32/src/config.h`.

---

## Critical Safety Issues

### 1. EGAS Throttle DAC — No Hardware Arbitration

**Finding:** §1.4 says "Use a double-throw relay or diode-OR" but no relay is specified, no wiring diagram exists, and the text was removed entirely in the simplification pass. A firmware-only approach (both DACs hardwired to the motor controller through passive resistors) is unsafe.

**Root cause:** The orginal 1 kΩ resistor scheme (deleted Appendix C) assumed the idle MCU outputs exactly 0 V at all times. This fails on boot (GPIOs float before firmware init), watchdog reset (GPIOs go high-impedance momentarily), or firmware crash (DAC holds last value). The motor controller would see a blended voltage — partial throttle when none was commanded.

**Recommended solution:** A single 4PDT signal relay (or 4 × SPDT relays on one module) that physically switches the motor controller's throttle input and three gear inputs between the SYS path and the MTR path. One GPIO from either SYS or MTR drives the relay coil based on the mode state.

- **De-energized (default) = SYS path active.** Covers power-up, WDT reset, and ESTOP — all default to SYS controlling the motor, which is the safest state (SYS in ESTOP outputs 0 V throttle).
- **Energized = MTR path active.** Only in AUTO mode, when both MCUs are confirmed healthy via heartbeat.

| Signal | SYS source | MTR source | Motor controller pin |
|--------|-----------|-----------|---------------------|
| Throttle (0–5 V) | MCP4725 @ 0x60, GPIO15/16 | MCP4725 @ 0x61, PB6/PB7 | Throttle input |
| Gear D (72 V) | Relay GPIO33 | Relay PA3 | Gear D input |
| Gear S (72 V) | Relay GPIO34 | Relay PA4 | Gear S input |
| Gear R (72 V) | Relay GPIO35 | Relay PA5 | Gear R input |

**Relay:** Omron G6K-4P-DC12 (4PDT, 12 V coil, DIP) or Finder 55.34.9.012.0040 (4PDT, 12 V, PCB). Coil driven by a dedicated GPIO (e.g., SYS GPIO8 or a new pin) through an NPN transistor + flyback diode. Coil return to chassis.

**Mode-gate control:** SYS asserts the mode-gate GPIO HIGH only when mode == AUTO AND both heartbeats are valid (SYS 0x7FE and RT 0x7FD both OK). Any fault → GPIO LOW → relay de-energizes → SYS path takes over.

### 2. Gear Conflict — No Hardware Prevention

**Finding:** Both SYS and MTR relays can physically assert different gears on the same motor controller inputs simultaneously. §1.4 states this is "prevented in firmware." For 72 V drive-by-wire, firmware-only arbitration is insufficient for a hazard that could energize Drive and Reverse simultaneously.

**Root cause:** SYS and MTR gear relay outputs are directly wired in parallel to the motor controller gear inputs. If one MCU asserts D (72 V on gear D line) and the other asserts R (72 V on gear R line) due to a firmware fault, the motor controller sees both and its behavior is undefined (likely depends on the specific controller model).

**Recommended solution:** The same 4PDT mode-gate relay from Issue #1 switches the gear lines. Only the active MCU's relay outputs reach the motor controller. The inactive MCU's relays are physically disconnected. This eliminates the parallel-output hazard entirely.

### 3. MCP4725 I²C Address Conflict

**Finding:** Both MCP4725 DACs are at address 0x60. SYS uses I²C on GPIO15/16, MTR uses I²C on PB6/PB7. These are separate buses, so no functional collision. But if a developer connects a single I²C analyzer to both buses simultaneously (common during debugging), the address collision will corrupt reads. It's a latent trap.

**Recommended solution:** Set MTR's MCP4725 A0 pin to VCC (5 V) instead of GND. Address becomes 0x61. Update `mtr-stm32/src/config.h`:
```
constexpr uint8_t kThrottleDacI2cAddr = 0x61;  // MCP4725 (A0=VCC)
```
No change to SYS (keeps 0x60, A0=GND).

---

## Electrical / Power Issues

### 4. DC-DC CAN Baud Rate Mismatch — BUS KILLER

**Finding:** §4.6 flags that the DC-DC converter's J1939 protocol spec references 250 kbps, but the low CAN bus runs at 500 kbps. This is left as an unresolved warning. A 250 kbps node on a 500 kbps bus will sample at the wrong bit timing — every frame it transmits will be seen as an error by 500 kbps nodes, and it will see valid 500 kbps frames as errors. After enough errors the node enters bus-off state, but during the error period it corrupts bus traffic for all nodes.

**Recommended solution:** Before any CAN bus wiring, verify the DC-DC converter's actual baud rate capability. Options in order of preference:

1. **Reconfigure to 500 kbps.** Most J1939 DC-DC converters use a configurable CAN controller internally. Contact the manufacturer or check the configuration interface (often a dedicated config CAN ID or DIP switch).
2. **Dedicated CAN segment.** If fixed at 250 kbps, give the DC-DC converter its own CAN bus on a second TWAI controller. The ESP32-S3 has only one TWAI, so this requires an external MCP2515 on SYS (or use RT's second bus). SYS or RT acts as a 2-node gateway.
3. **Replace the DC-DC converter.** Select a unit with native 500 kbps support. This is the simplest long-term fix.

**Do not connect a 250 kbps node to the 500 kbps bus.** The warning in §4.6 must be escalated to a blocking requirement.

### 5. PTC Contradiction — RESOLVED

**Finding:** The reviewer flagged a contradiction between §5.3 (removing PTCs from EPS-C/SEB) and §7.2 (listing them). **Confirmed resolved** — both sections were removed in the simplification pass. No PTC references remain in the current file. No action needed.

### 6. F_seb Fuse Rated Above Wire Capacity

**Finding:** F_seb is 25 A. SEB wire is 12 AWG. Derated ampacity for 12 AWG GXL in-bundle is 20 A (per SAE J1128, 30% bundle derating). The fuse is 25% above the wire's safe capacity — a sustained 22 A load would overheat the wire without blowing the fuse.

**Recommended solution:** Reduce F_seb to 20 A ATO. The SEB pump draws 20 A peak per Syntree spec, which is within surge tolerance of a 20 A ATO fuse (ATO fuses tolerate brief inrush above rating). If the SEB trips a 20 A fuse during normal operation, the wire must be upgraded to 10 AWG (30 A derated), not the fuse increased.

### 7. 8 AWG Forward Run — Insufficient Headroom

**Finding:** The DC-DC → forward fuse block run carries 35 A through 8 AWG over 2.0 m. 8 AWG derated capacity is 40 A. The fuse (F_12v_main) is 40 A. At 35 A steady-state, the wire runs at 87.5% of derated capacity — no margin for bundle heating, ambient temperature, or voltage drop degradation. A 40 A fuse will never blow on a 35 A load, even as the wire cooks.

**Recommended solution:** Two approaches:

**Option A (recommended):** Move the DC-DC converter to the front/center (near JP2). 72 V runs forward at 5 A on 14 AWG (light, cheap, efficient). 12 V is generated right where the big loads are. The 8 AWG forward run disappears entirely. This is the correct design for a vehicle with rear battery and front loads.

**Option B:** Keep DC-DC at rear. Upgrade the forward run to 6 AWG (derated 55 A, plenty of headroom). Or reduce F_12v_main to 35 A and accept 12.5% fuse margin on a 35 A load.

Option A is strongly preferred — it saves copper weight, reduces voltage drop, and puts conversion where the loads are.

### 8. F_always Fuse Undersized

**Finding:** F_always is 2 A, powering brake light + all CAN transceivers + MCU keep-alive circuits. A single brake light bulb (incandescent) draws ~1.8 A at 12 V. CAN transceivers (4 × SN65HVD230) draw ~120 mA total. MCU keep-alive (3 × LDO quiescent) ~50 mA. Total steady-state: ~2.0 A — right at the fuse rating. Any transient (lamp inrush, capacitor charging) trips it. If this fuse opens, CAN and the brake light go dead simultaneously during ESTOP.

**Recommended solution:** Split the always-on rail into two fused circuits:
- **F_brake: 5 A slow-blow** — dedicated to the brake light. Slow-blow type handles lamp inrush.
- **F_can_mcu: 3 A** — CAN transceivers + MCU keep-alive. Low, stable load.

This prevents a brake light fault from killing CAN, and vice versa.

---

## CAN Bus Architecture Issues

### 9. CAN ID 0x120 Sender Mismatch

**Finding:** Message `0x120` is named `SYS_THROTTLE_STS` but the sender is MTR STM32. The naming convention in the protocol is `SENDER_CONTENT` (e.g., `RT_DRIVE_CMD` is sent by RT, `MTR_MOTOR_FBK` is sent by MTR). By this convention, 0x120 should be named `MTR_THROTTLE_STS`.

**Recommended solution:** This is a protocol naming bug, not a wiring bug. The harness document's §10.1 correctly lists the sender as MTR. Add a footnote: "CAN ID 0x120 is named SYS_THROTTLE_STS in the protocol but is physically sent by MTR STM32. This is a known naming inconsistency — the message carries throttle status, consumed by SYS and forwarded by RT."

### 10. CAN ID 0x302 — Forwarding Loop Risk

**Finding:** 0x302 `HOST_LIGHT_CMD` appears on both buses (Jetson → high bus → RT, RT → low bus → SYS). This is correct gateway behavior but the forwarding rule is not documented. If RT accidentally forwards 0x302 from low bus back to high bus, a loop forms.

**Recommended solution:** Document the forwarding rule explicitly in §10.1:
```
0x302 HOST_LIGHT_CMD: Jetson → high bus → RT → low bus → SYS (one-way only).
RT must NOT forward 0x302 received on low bus back to high bus.
```
Same rule applies to all bridged messages: 0x001, 0x011, 0x120, 0x206, 0x210, 0x220, 0x310, 0x311, 0x600.

---

## Mechanical / Routing Issues

### 11. BOM Wire Lengths vs Harness Length

**Finding:** H5 is declared as 1.0 m in §1.1, but the H5 BOM lists EPS-C 12 AWG power wire at 1.5 m and CAN drop at 1.5 m. The 0.5 m difference is a legitimate service loop allowance (0.2 m at each connector + routing tolerance), not an error — but it's not explained.

**Recommended solution:** Add a note to §9: "BOM wire lengths include 150–200 mm service loop at each connector plus 5–10% routing tolerance beyond the nominal harness length in §1.1."

### 12. Left Frame Rail — CAN + 12 V High-Current Co-located

**Finding:** §8.3 routes the CAN backbone (signal-critical) and 12 V distribution (8 AWG, 35 A) along the same left frame rail. §8.2 requires ≥50 mm separation between 12 V high-current and signal cables. This is a self-contradiction.

**Recommended solution:** Move the 12 V distribution (8 AWG red) to the right frame rail. The left rail carries only CAN backbone + CAN_GND + sensor cables (all low-current/signal). Right rail carries all power: 72 V gear, motor power, 12 V distribution, EPS-C/SEB power.

### 13. 72 V Gear Lines Near Motor Power — Noise Coupling

**Finding:** §8.3 places 18 AWG gear output lines on the right rail alongside 6 AWG motor controller power (50 A with significant di/dt from PWM switching). The gear lines feed TLP281 optoisolator inputs with a ~33 kΩ current-limiting resistor — this high impedance makes them susceptible to capacitively coupled noise from the adjacent motor power cable. A false trigger on a gear sense line would be interpreted as a gear change.

**Recommended solution:** Route the 72 V gear sense lines (from the gear selector switch to the TLP281 optoisolators) on the left rail with other signal cables, not on the right rail with motor power. The gear output lines (from relays to motor controller) are low-impedance relay-driven signals and can stay on the right rail. Revised routing:

```
Left Frame Rail:                     Right Frame Rail:
  CAN backbone (H3)                    72 V motor power (6 AWG)
  CAN_GND (18 AWG blk/wht)             72 V DC-DC input (14 AWG)
  Sensor cables (throttle, enc.)       12 V distribution (8 AWG)  ← moved from left
  Gear sense lines (20 AWG org)  ← moved from right
  12 V low-current (MCU power)
```

---

## Design Gaps

### 14. MTR ESTOP Wiring Not Documented

**Finding:** `mtr-stm32/src/config.h` defines `kEstopGpio = 1` (PA1) with the comment "Shared with SYS GPIO1 (separate MCU, different physical pin)." The ESTOP button is a single NC switch wired to TWO GPIOs on two different MCUs — a Y-splice in the harness. This is the most safety-critical wire in the vehicle. It is not explicitly called out in any harness BOM or wiring diagram.

**Recommended solution:** Add an explicit ESTOP wiring section to H1 BOM:
```
| ESTOP button NC loop | 2-conductor 22 AWG, twisted | 1.5 m |
  Button terminal 1 → GND (chassis, local)
  Button terminal 2 → Y-splice → SYS GPIO1 AND MTR PA1 (both with 10k pull-up to 3.3V at MCU end)
```
Both MCUs must see the same physical wire. The Y-splice must be at a junction where both MCU harnesses meet (JP3, handlebar area). Solder + adhesive heat-shrink, no crimp connector on this splice.

### 15. Front Encoder Sensor TBD — Protection May Be Wrong

**Finding:** The front wheel encoder is "TBD sensor" throughout. H5 BOM specifies a TE Superseal 1.5 4-pin connector and PESD5V0S2UT TVS, which assumes a 5 V encoder. If the selected encoder is 12 V Hall-effect or has open-collector outputs, the TVS rating and pull-up configuration will be wrong.

**Recommended solution:** Add a constraint: "Select a 5 V incremental quadrature encoder (AB-phase, 1–1000 PPR) compatible with ESP32-S3 PCNT peripheral. Common options: CUI AMT11 series, Broadcom HEDS-9700, or generic Hall-effect AB encoder modules." This ensures the 5 V TVS and 4-pin connector (5 V, GND, A, B) are correct. If a 12 V encoder is later chosen, the TVS must be upgraded to SMBJ15A and a level shifter added.

### 16. Rear Left/Right Encoders — No Harness Provision

**Finding:** RT config.h defines `kEncRearLeftA=9, kEncRearLeftB=12, kEncRearRightA=13, kEncRearRightB=14` as TBD sensors. §7.3 mentions them with TVS arrays. But there is no connector, wire, or routing specification for these in any harness BOM.

**Recommended solution:** Add an optional sub-section to H4 BOM for future rear differential encoders. Use the same connector and TVS pattern as the front encoder (TE Superseal 1.5 4-pin + PESD5V0S2UT). Note them as "TBD — connectors provisioned, wiring not installed." This provides a clear expansion path without committing to sensor selection now.

### 17. Canonical CAN Message Table Location

**Finding:** §10.1 (CAN Message → Physical Node Wiring) duplicates the CAN message catalog from `architecture.md` §2. The duplication creates a maintenance burden — any protocol change must be updated in both files. The harness doc has already drifted (e.g., some high-bus receivers omitted).

**Recommended solution:** Replace §10.1 with a cross-reference: "For the complete CAN message catalog including IDs, senders, receivers, DLC, and periods, see `architecture.md` §2 and `shared/can/can_signals.yaml`. The table below lists only the physical node locations added by this harness document." Keep only the Physical Location column alongside the existing CAN ID and Name columns, dropping sender/receiver columns that duplicate architecture.md.

---

## Summary of Required Changes

| # | Severity | Issue | Fix |
|---|----------|-------|-----|
| 1 | **Critical** | No hardware DAC arbitration | Add 4PDT mode-gate relay, coil driven by SYS GPIO8 |
| 2 | **Critical** | No hardware gear arbitration | Same mode-gate relay switches gear lines |
| 3 | Low | MCP4725 address collision | Set MTR A0=VCC → addr 0x61 |
| 4 | **Critical** | DC-DC baud rate mismatch | Escalate to blocking requirement; verify before build |
| 5 | — | PTC contradiction | Already resolved |
| 6 | **High** | F_seb 25 A > 12 AWG rating | Reduce F_seb to 20 A |
| 7 | **High** | 8 AWG forward run no headroom | Move DC-DC to front; or upgrade to 6 AWG |
| 8 | **High** | F_always 2 A too small | Split into F_brake 5A slow + F_can_mcu 3A |
| 9 | Low | CAN 0x120 naming | Add footnote, no wiring change |
| 10 | Low | 0x302 forwarding loop risk | Document one-way forwarding rule |
| 11 | Low | BOM vs harness length | Add service loop note |
| 12 | **High** | CAN + 12V on same rail | Move 12V to right rail |
| 13 | Medium | Gear sense near motor power | Move gear sense to left rail |
| 14 | **Critical** | MTR ESTOP not documented | Add ESTOP Y-splice to H1 BOM |
| 15 | Medium | Front encoder TBD | Specify 5V encoder constraint |
| 16 | Low | No rear encoder provision | Add optional connectors to H4 BOM |
| 17 | Low | Duplicate CAN table | Cross-reference architecture.md |
