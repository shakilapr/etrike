# E-Trike — Issues & Resolution Analysis

Re-verified 2026-06-11 against current [`architecture.md`](architecture.md) and
[`can-dictionary.md`](can-dictionary.md). Previous issues that have been resolved in the
updated architecture are noted as such.

---

## Critical

### C1. CAN dictionary heartbeat scheme incompatible with architecture

**Severity:** Critical — the two core documents prescribe different, incompatible
heartbeat mechanisms. Implementing from one without updating the other will produce a
non-functional system.

**What architecture.md says** (current, per-node IDs):

Each node gets its own heartbeat CAN ID:

| ID | Sender | Bus | Receiver(s) |
|----|--------|-----|-------------|
| `0x7FD` | RT | Both (independent per bus, NOT bridged) | SYS (low), Jetson (high) |
| `0x7FE` | SYS | Low only | RT |
| `0x7FC` | Jetson | High only | RT |

Design principle #3: *"One CAN ID = one sender per bus. Every CAN ID has exactly one
originator on a given bus. Each node's heartbeat uses its own ID."*

**What can-dictionary.md says** (old, shared ID):

A single `0x7FF` on each bus with multiple senders per bus:

- Low-level: `Sender: RT, SYS | Receiver(s): RT, SYS`
- High-level: `Sender: Jetson, RT | Receiver(s): Jetson, RT`

With the rationale: *"Why 0x7FF is NOT bridged: Each bus is an independent liveness
domain. Bridging would put two senders with different alive counters on the same bus
using the same CAN ID — impossible to distinguish which counter belongs to which node."*

**The conflict:**

| Aspect | architecture.md | can-dictionary.md |
|--------|----------------|-------------------|
| Heartbeat IDs | 3 (`0x7FC`, `0x7FD`, `0x7FE`) | 1 (`0x7FF`) |
| Senders per bus | 1 per ID | 2 per ID, distinguished by alive counter |
| Priority group | `0x7FC`–`0x7FE` (lowest range) | `0x7FF` (end of lowest range) |
| Liveness matrix | Per-ID monitoring (each watcher knows exactly which ID to watch) | Per-bus monitoring with counter disambiguation |

The architecture's per-node-ID scheme is superior (cleaner, no ambiguity, no software
demux). The CAN dictionary must be updated to match.

**Sources:**
- `architecture.md` §2.1, §2.2, §2.3, §6 (principle #3), §7.9, §8.6, §8.9, §9
- `can-dictionary.md` §1 (0x7FF low-level), §2 (0x7FF high-level), §3, §4, §5

**Solution:**

Update `can-dictionary.md` to replace the shared `0x7FF` with the three per-node
heartbeat IDs:

1. **Low-level bus:**
   - `0x7FD` — RT_HEARTBEAT: Sender=RT, Receiver=SYS, DLC=1, 2 Hz, u8 alive_ctr
   - `0x7FE` — SYS_HEARTBEAT: Sender=SYS, Receiver=RT, DLC=1, 2 Hz, u8 alive_ctr
   - Timeout: 1000ms (2 missed frames at 2 Hz)
   - Both must not be bridged to high bus
2. **High-level bus:**
   - `0x7FD` — RT_HEARTBEAT: Sender=RT, Receiver=Jetson, DLC=1, 2 Hz, u8 alive_ctr
   - `0x7FC` — JETSON_HEARTBEAT: Sender=Jetson, Receiver=RT, DLC=1, 2 Hz, u8 alive_ctr
   - Timeout: 1500ms (3 missed frames at 2 Hz)
3. Update the CAN ID summary tables (§3) and forwarding rules (§4, Category 3)
4. Update the liveness matrix to show per-ID monitoring
5. Update the priority groups table (§5)

---

### C2. Brake arbitration has no CAN path to SYS (Gap #1, unchanged)

**Severity:** Blocks all autonomous braking.

Jetson sends `0x301 HOST_BRAKE_REQ` (kPa) on high bus → RT max-select arbitrates →
result has **no CAN ID to reach SYS**. SYS commands the SEB via `0x720` and currently
only uses ESTOP + brake lever as triggers. Jetson deceleration requests and RT
obstacle-emergency braking never reach the brake actuator.

**Sources:** `architecture.md` §2.3 (Category 2), line 445, §12 Gap #1;
`can-dictionary.md` line 437.

**Solution:** New `0x205 RT_BRAKE_CMD` (RT→SYS, low bus, DLC 4, i32 brake_kpa, 50 Hz).
SYS then switches SEB to Pressure Mode and maps kPa → `VCU_SEB_Pre_Value_Req` MPa.

---

### C3. Pressure Mode mapping undefined (Gap #4, unchanged)

**Severity:** Blocks modulated autonomous braking even after Gap #1 is closed.

SYS commands SEB in Stroke Mode (1) only — fixed pushrod positions for binary triggers.
Pressure Mode (2), which compensates for pad wear and temperature, is "planned for AUTO"
but the kPa→MPa target mapping and the `VCU_SEB_Pre_Value_Req` field scale are still
TBD.

**Sources:** `architecture.md` §8.6 (SEB), §12 Gap #4;
`can-dictionary.md` line 244 (pressure scale TBD).

**Solution:** Obtain SEB pressure spec from SYNTREE, define raw-to-MPa conversion
constants, define Stroke→Pressure mode-switching protocol (hold position → switch mode →
ramp pressure target from current measured to desired).

---

## Medium

### M1. CAN dictionary still uses old message catalog — broader stale references

Beyond the heartbeat IDs (C1), the can-dictionary.md may have other stale references
that diverged when architecture.md was updated. A full cross-check is needed. Specific
areas to verify:

- `0x7FF` appears in the summary tables (§3), forwarding rules (§4 Category 3), and
  priority groups (§5) — all need updating
- Forwarding rules §4 Category 3: the "Both independent" row lists `0x7FF` but should
  list `0x7FD`, `0x7FE`, `0x7FC`
- Liveness matrix in can-dictionary.md §1 (0x7FF low-level) uses different timeouts
  (200ms) than architecture.md (1000ms) — this may have been the old value before
  the timeout rationale update

**Solution:** Audit every reference to `0x7FF` in can-dictionary.md and replace with
the correct per-node ID scheme. Sync timeout values (architecture.md is authoritative).

---

### M2. Brake Listen-Before-Speaking failure immobilizes vehicle (unchanged)

SYNC timeout → `BRAKE_FAULT` → lever inoperative. A CAN wiring fault or SEB power issue
at boot completely immobilizes the vehicle with no degraded-mode fallback.

**Sources:** `architecture.md` §8.6, §8.10.

**Solution:** Add mechanical brake bypass (physical cable from lever to master cylinder)
as true failsafe. At minimum, verify the SEB's unpowered hydraulic behavior — does it
release pressure (allowing the vehicle to be pushed) or hold it (immobilizing it)?

---

### M3. Steering sync timeout recovery is specified but untested

The architecture now defines steering sync recovery paths:

- Sync timeout → STEER_FAULT → short-press START → retry STEER_LISTEN_SYNC
- Long-press START (3s) + throttle at zero → force-activate with target=0° (MANUAL only,
  AUTO locked out)

These are well-designed recovery paths, but they introduce operational complexity:
- A rider must know the difference between short-press and long-press START
- Force-activation with target=0° means manual steering only (EPS-C holds centered)
- AUTO mode is permanently locked out until a successful sync

The 5-second sync timeout (increased from 2s) reduces false failures but the recovery
UX should be validated with actual riders.

**Sources:** `architecture.md` §7.6 (steer state machine, STEER_FAULT recovery).

---

### M4. No CAN message for Sport gear from Jetson (Gap #2, unchanged)

`0x300 HOST_DRIVE_CMD` carries speed + yaw but no gear field. RT resolves gear
automatically (v>0→D, v=0→N, v<0→R). Jetson cannot select Sport gear even though
`0x204 RT_DRIVE_CMD` supports it (enum values N=0, D=1, S=2, R=3).

**Sources:** `architecture.md` §12 Gap #2.

**Solution:** Reduce yaw rate from i32 to i24 in `0x300` (range ±3000 mrad/s fits
comfortably in ±8,388,608), freeing 8 bits for a `u8 gear` field. Single-frame, no
new CAN ID needed.

---

## Low

### L1. Wheel encoder loss does not trigger ESTOP (unchanged, intentional)

> "Wheel encoder missing (any): No pulses for >1s at known speed → Log warning;
> differential odometry degraded. **Does NOT trigger ESTOP.**"

Intentional design choice. Motor encoder + SYNTREE feedback provide independent speed
sensing, so losing a wheel encoder degrades odometry precision but isn't an immediate
safety hazard. Still worth documenting which functions depend on wheel encoder data.

**Sources:** `architecture.md` §7.10.

**Recommendation:** Add plausibility check between motor encoder speed and wheel
encoder speed. If divergence exceeds X% for Y seconds, log warning and consider
degrading AUTO mode to a speed limit.

---

### L2. GPIO overlap between RT and SYS is safe but worth noting

GPIOs shared across the two ESP32s (no electrical conflict since they're different
chips, but could confuse during wiring):

| GPIO | RT function | SYS function |
|------|------------|--------------|
| 1 | Encoder A (rear motor) | ESTOP button |
| 2 | Encoder B (rear motor) | Brake lever |
| 3 | Encoder A (front wheel) | Left turn switch |
| 6 | Encoder B (front wheel) | Right turn switch |
| 7 | Ultrasonic TRIG | Headlight switch |
| 10 | I2C SDA (IMU) | Throttle ADC read |
| 11 | I2C SCL (IMU) | Mode button |
| 12 | Encoder B (rear left) | Gear D sense |
| 13 | Encoder A (rear right) | Gear S sense |
| 14 | Encoder B (rear right) | Gear R sense |
| 21 | WDT toggle | Brake light output |

All safe since RT and SYS are separate ESP32s. But wiring documentation should
clearly label which GPIO belongs to which board.

---

### L3. Integration risks (unchanged observations)

| Risk | Mitigation |
|------|-----------|
| MCP2515 SPI reliability under 9-task load | Validate throughput at 100 Hz telemetry + 500 kbit/s CAN |
| I2C contention (MCP4725 DAC + optional IMU) | Add I2C mutex to SYS firmware |
| Power sequencing (DC-DC before ESP32 boot) | SYS waits 500ms after `0x012 enable=1` before actuation |
| Throttle ADC lacks galvanic isolation | Consider optoisolation (72V fault could reach ESP32 ADC via voltage divider) |

---

## Resolved (since previous review)

| # | Issue | Resolution |
|---|-------|-----------|
| R1 | EPS-C timeout-fault behavior TBD (old Gap #3) | **Resolved.** Two-tier ESTOP steering: obstacle → hold angle then silent-stop; non-obstacle → active ramp to 0° at 20°/s via `0x200`, fallback to silent-stop on mechanical jam. STEER_FAULT now has START-button recovery paths. Gap #3 struck through in §12. |
| R2 | Steering pre-ESTOP sequence missing | **Resolved.** Active centering is now the primary ESTOP path. EPS-C timeout-fault is only a last-resort fallback (CAN bus dead). |
| R3 | Topology diagram SYS RX showed `0x200` instead of `0x202` | **Fixed.** Current topology correctly shows `0x202` in SYS RX. |
| R4 | Category 3 omitted `0x200`/`0x202` | **Fixed.** architecture.md §2.3 Category 3 now lists `0x200` and `0x202` in the "Low only" row. |
| R5 | Heartbeat timeout ambiguity (old 200ms FTTI conflation) | **Resolved.** Separate rationale section added (§8.6). Heartbeat is the slow detection path (1000ms). Fast path is `0x202` staleness check at 200ms in SYS. Clear distinction between data-quality check and node-liveness check. |
| R6 | Shared `0x7FF` multiple-sender ambiguity | **Resolved** in architecture.md by switching to per-node heartbeat IDs. But this created C1 — the CAN dictionary was not updated to match. |

---

## Summary — current prioritized action items

| # | Priority | Issue | Action |
|---|----------|-------|--------|
| C1 | ~~P0~~ **DONE** | CAN dictionary heartbeat scheme stale | ✅ can-dictionary.md updated to `0x7FD`/`0x7FE`/`0x7FC`. All heartbeat sections, summary tables, forwarding rules, priority groups synced with architecture.md. |
| C2 | ~~P0~~ **DONE** | Brake path to SYS missing (Gap #1) | ✅ `0x205 RT_BRAKE_CMD` defined (RT→SYS, DLC=4, i32 kPa, 50 Hz). Added to both docs. Mode-switching protocol in architecture.md §8.6. |
| C3 | ~~P0~~ **DONE** | Pressure mode mapping undefined (Gap #4) | ✅ Verified SYNTREE SEB spec: `VCU_SEB_Pre_Value_Req` is u8 at bit 24 (byte 3, mode-muxed with Stroke), scale 0.05 MPa/bit, range 0–5 MPa, raw 0–100. Conversion: `seb_raw = kPa × 0.02`. Both docs updated. Gap #4 struck through. |
| M1 | ~~P1~~ **DONE** | Full can-dictionary.md audit | ✅ All stale refs fixed: timeouts (200→1000ms, 500→1500ms), `0x011 SYS_HeartbeatOk`, liveness matrix, forwarding rules, priority groups, `0x300` gear field. |
| M2 | **P1** | Brake sync failure immobilizes vehicle | Mechanical bypass cable + bench-test SEB unpowered behavior |
| M3 | **P2** | Steering sync recovery UX | Rider validation of short/long-press START behavior |
| M4 | ~~P2~~ **DONE** | No Sport gear from Jetson (Gap #2) | ✅ `0x300` repacked: i32 speed + i24 yaw + u8 gear. Both docs updated. RT passes gear through to `0x202`. |
| L1 | **P3** | Wheel encoder loss no ESTOP | Document rationale, add plausibility check |
| L2 | **P3** | GPIO overlap labeling | Clearly label per-board GPIOs in wiring docs |
| L3 | **P3** | Integration risks (SPI, I2C, power seq, ADC isolation) | Mutex, startup delay, validation, optoisolation |
