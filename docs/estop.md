# E-STOP — Emergency Stop System Reference

> **Vehicle:** E-Trike Drive-by-Wire Control System
> **Generated:** 2026-07-05
> **Status:** Comprehensive reference from all project sources

---

## Table of Contents

1. [Overview & Design Principles](#1-overview--design-principles)
2. [ESTOP State — Subsystem Actions](#2-estop-state--subsystem-actions)
3. [ESTOP Trigger Paths — The 9+ Safety Layers](#3-estop-trigger-paths--the-9-safety-layers)
4. [ESTOP Exit Logic](#4-estop-exit-logic)
5. [Steering During ESTOP — Two-Tier Response](#5-steering-during-estop--two-tier-response)
6. [Heartbeat System — Full Liveness Matrix](#6-heartbeat-system--full-liveness-matrix)
7. [Alive Counter & Frozen Detection](#7-alive-counter--frozen-detection)
8. [CAN Frames — ESTOP & Safety](#8-can-frames--estop--safety)
9. [Mode State Machine](#9-mode-state-machine)
10. [Listen Before Speaking — Post-ESTOP Recovery](#10-listen-before-speaking--post-estop-recovery)
11. [External Watchdog — Last-Resort Reset](#11-external-watchdog--last-resort-reset)
12. [ESTOP Rate Limiting](#12-estop-rate-limiting)
13. [EGAS 3-Level Motor Safety Architecture](#13-egas-3-level-motor-safety-architecture)
14. [Emergency Response Matrix](#14-emergency-response-matrix)
15. [Testing the Emergency System](#15-testing-the-emergency-system)
16. [Configuration Constants](#16-configuration-constants)
17. [Related Documents](#17-related-documents)

---

## 1. Overview & Design Principles

ESTOP is the system's **absorbing safety state**. When entered, all actuators return to their safe positions and the vehicle stops. ESTOP persists until deliberately cleared by the rider — it never self-clears.

### Design Principles

| # | Principle | Implementation |
|---|-----------|---------------|
| 1 | **ESTOP bypasses queues.** | Safety task preempts and writes directly to actuators — no queue delay. |
| 2 | **ESTOP is an absorbing state.** | Once entered, only deliberate human action (START button or power-cycle) exits. No automatic recovery, no CAN exit. |
| 3 | **ESTOP exit goes to MANUAL, never AUTO.** | Rider resumes direct control after any emergency. |
| 4 | **NC wiring for all safety inputs.** | Cut wires and disconnected plugs read as ESTOP, not "everything fine." |
| 5 | **Threshold + duration for all fault checks.** | Single-sample glitches don't trigger ESTOP — faults must persist. |
| 6 | **Fail-safe by default.** | De-energized relays = Neutral. Zero throttle voltage = motor stop. NC ESTOP button = pressed when disconnected. |
| 7 | **Fail-visible where possible.** | Brake light always illuminates when any braking source is active. Both mode bulbs OFF = ESTOP. |
| 8 | **Independent safety layers.** | Hardware (ESTOP button, watchdog IC) → CAN (0x001, heartbeats) → Software (staleness, following error, clamps). No single failure defeats them all. |

### Physical Controls

| Control | GPIO | Type | Function |
|---------|------|------|----------|
| **ESTOP button** | SYS GPIO1, MTR kEstopGpio (TBD STM32 pin) | NC (normally-closed), active-low, red mushroom | Press → ESTOP. Dual-path to independent MCUs. |
| **START button** | SYS GPIO41 | Momentary, green | ESTOP → MANUAL. Short press in STEER_FAULT resets steering SM. Long press (3s) + throttle zero → force-activate steering at 0° (MANUAL only). |
| **MODE button** | SYS GPIO11 | Momentary | Toggle MANUAL↔AUTO. Short press ignored in ESTOP. Long press (3s) in ESTOP → MANUAL (secondary exit path). |
| **Brake lever** | SYS GPIO2 | Digital input | Always works. SYS → CAN 0x7B9 → SEB. Has priority over Jetson brake commands in AUTO. |

### ESTOP as Absorbing State

```
MANUAL ←──→ AUTO          (MODE button toggle)
   │          │
   │  ESTOP button / CAN 0x001 / HB timeout / Following error / Command stale / SEB L3
   │          │
   ▼          ▼
 ┌──────────────┐
 │    ESTOP     │  ◀── absorbing state
 └──────┬───────┘
        │
        │ START button (GPIO41) or MODE long-press 3s (GPIO11)
        │ → always goes to MANUAL, never AUTO
        │ Power-cycle → MANUAL (ultimate fallback)
        ▼
      MANUAL
```

Cannot exit ESTOP via: CAN command, MODE button short-press, automatic timeout.

---

## 2. ESTOP State — Subsystem Actions

| Subsystem | Action | Implementation Detail |
|-----------|--------|----------------------|
| **Motor throttle** | MCP4725 DAC = 0 V | Instant motor kill — zero voltage to motor controller. `mtr-stm32/src/mcp4725_dac.h` — I2C write with finite timeout. |
| **Gear selection** | All relays OFF → Neutral | 72V gear lines de-energized. All gear MOSFETs OFF. |
| **Brake (SEB)** | Stroke = max (~27 mm, raw=1140) | Full hydraulic brake pressure via CAN 0x7B9 at 50 Hz. `kBrakeMaxStroke = 27.0f` mm. |
| **Steering (EPS-C)** | Obstacle: hold angle (dyn-clamped) → silent-stop after 500ms. Non-obstacle: ramp to 0° at 20°/s via active 0x169. | Two-tier response. Fallback to silent-stop if following error persists >1s (mechanical jam). |
| **DC-DC converter** | CAN 0x012 enable = **1** (maintains 12V) | Keeps MCUs, CAN transceivers, and brake light powered. Safety tasks must run. |
| **12V accessory relay** | GPIO40 OFF | Cuts headlight, turn signals, mode bulbs — non-safety loads only. |
| **Brake light** | **ON** | Powered from always-on DC-DC rail (independent of accessory relay). |
| **Mode indicator bulbs** | Both AUTO and MANUAL OFF | Dark dashboard = ESTOP. Dedicated ESTOP red bulb on GPIO20. |
| **Ready bulb** | OFF | Green ready bulb (GPIO17) OFF. |
| **Throttle pass-through** | Ignored | ADC reads ignored, DAC forced to 0. |
| **Gear pass-through** | Ignored | All gear relays forced OFF regardless of selector. |

### Brake Light OR Logic (fail-visible)

```
brake_light_on = brake_lever_pressed()     // GPIO2 — physical lever (local)
              OR (mode == Estop)            // ESTOP — full brake (local)
              OR g_light_state.brake_light; // Jetson CAN 0x302 — supplemental only
```

All sources are **local to SYS** — no CAN round-trip needed for physical lever or ESTOP. The physical braking state always wins.

---

## 3. ESTOP Trigger Paths — The 9+ Safety Layers

### Layer 1: Physical ESTOP Button (fastest path)

Big red mushroom button wired **normally-closed (NC)** from 3.3 V to SYS GPIO1.
MTR connection remains blocked until its direct ESTOP hardware exists.

| Property | Value |
|----------|-------|
| GPIO | SYS: GPIO1; MTR: planned, not implemented |
| Type | NC (normally-closed), active-low |
| Detection | SYS `safety_task` @ 20 Hz; MTR direct ISR is planned only |
| CAN redundancy | Also broadcasts CAN `0x001` on low bus |

**Fail-safe by construction:**
```
Normal operation: GPIO reads HIGH (NC contact supplies 3.3 V) → system runs
Button pressed:   GPIO reads LOW (NC contact opens; external pull-down) → ESTOP
Wire cut/broken:  GPIO reads LOW (external pull-down) → ESTOP
Power loss:       GPIO reads LOW → ESTOP
```

**MTR STM32 direct path:** Not implemented. It is a release blocker for vehicle motor actuation, not an available EGAS Level 3 path.

### Layer 2: CAN `0x001` SAFETY_ESTOP

Any node can broadcast `0x001` on either CAN bus to trigger ESTOP.

| Property | Value |
|----------|-------|
| CAN ID | `0x001` — highest priority on both buses (wins every arbitration) |
| DLC | 0 (empty frame — the ID itself IS the signal) |
| Senders | Jetson, RT, SYS, MTR |
| Forwarding | RT transparently forwards between buses (bypasses gateway queue) |

**Why each node might trigger:**
- **Jetson:** Perception stack detects imminent collision that obstacle sensor missed.
- **RT:** Steering following error exceeds threshold, or SYS heartbeat lost.
- **SYS:** Physical ESTOP button pressed (redundant CAN path), or RT heartbeat lost, or SEB L3 fault detected.
- **MTR:** Local ESTOP GPIO triggered, or motor fault detected.

### Layer 3: Heartbeat Timeout — Node Crash Detection

Each MCU sends a 1-byte alive counter on its own CAN ID. A frozen counter = a frozen node. Full details in [§6](#6-heartbeat-system--full-liveness-matrix).

### Layer 4: Command Staleness — Jetson/MTR Hang Detection

| Check | Watcher | CAN ID | Timeout | Action |
|-------|---------|--------|---------|--------|
| `0x300` staleness | RT `watchdog` task | HOST_DRIVE_CMD | 500ms | Zero `0x204` + stop `0x169` |
| `0x204` staleness | SYS `motor` task | RT_DRIVE_CMD | 200ms | Zero speed + Neutral gear |
| `0x204` staleness | MTR | RT_DRIVE_CMD | 200ms | Local throttle cut + gear OFF |

The 500ms RT timeout allows for brief Jetson hiccups. The 200ms SYS/MTR timeout is tighter because stale actuator data is immediately dangerous.

### Layer 5: Steering Following Error — Mechanical Fault Detection

RT compares commanded steering angle (`0x169`) against actual angle reported by EPS-C (`0x201 SES_StrAngle`) at 100 Hz.

| Parameter | Value |
|-----------|-------|
| Error threshold | **Speed-scaled:** `max(2°, 0.25 × dynamic_limit)` — 2° at 25 km/h, 4.5° at 10 km/h, 10° at 2 km/h |
| Persistence duration | >300ms (30 ticks at 100 Hz) |
| Action | ESTOP |

**Why speed-scaled:** At high speed, the dynamic clamp limits steering to ~5°. A fixed 5° threshold would require 100% error to trigger. At low speed (40° limit), 5° is only 12.5% — too sensitive for parking maneuvers.

### Layer 6: Dynamic Angle Clamp — Rollover Prevention

Maximum allowable steering angle is inversely proportional to speed, enforced in RT's `physics_resolve()` before the command reaches EPS-C:

| Speed | Max angle |
|-------|-----------|
| 2 km/h | ~40° |
| 10 km/h | ~18° |
| 25 km/h | ~5° |

Formula: `max_angle = lerp(5.0, 40.0, clamp((speed - 2) / (25 - 2), 0, 1))`

### Layer 7: Software Hard-Stops — Mechanical Protection

All commanded steering angles clamped to ±40° regardless of source. Prevents motor from pushing against physical end-stops.

### Layer 8: External Watchdog — Last-Resort Hardware Reset

Each ESP32-S3 has a dedicated external watchdog IC (TPS3850) on a separate chip. Full details in [§11](#11-external-watchdog--last-resort-reset).

| Node | Toggle GPIO | Toggled by | Period | Timeout |
|------|------------|-----------|--------|---------|
| RT | GPIO21 | `control_task` | 100 Hz (every 10ms) | ~100ms |
| SYS | GPIO47 | `safety_task` | 20 Hz (every 50ms) | ~100ms |

### Layer 9: Brake Following-Error Monitor — Actuator Feedback Verification

SYS compares commanded brake stroke/pressure (from `0x7B9`) against actual feedback (from `0x721`):

| Check | Threshold | Debounce | Action |
|-------|-----------|----------|--------|
| Stroke following error | abs(cmd − actual) > 3mm | 100ms (10 frames) | Log fault, set diag flag |
| SEB Level 3 fault | `SEB_Error_Status == 3` | Immediate | Log SEB severe fault |
| 0x721 staleness | No frame for >100ms | — | SEB comms lost — brake system offline |

### Additional Trigger: SEB L3 Fault Escalation

SYS subscribes to `0x731 SEB_ErrInfo`. 14 of 23 documented brake faults are L3 (severe). Any active L3 bit → SYS triggers CAN `0x001` ESTOP.

### Additional Trigger: EPS-C L3 Fault Escalation

RT subscribes to `0x202 SES_ErrInfo`. 8 steering faults are L3 (angle sensor primary/secondary open-circuit/out-of-range × 4 + torque sensor T1/T2 open-circuit/out-of-range × 4). Any active L3 bit → RT triggers ESTOP.

### Additional Trigger: MTR Motor Fault (EGAS L2)

SYS compares `0x204 RT_MotorSpeed` (commanded) vs `0x206 MTR_ActualSpeed` (feedback). Mismatch >500 mm/s persisting >500ms → CAN `0x001` ESTOP.

### Additional Trigger: MTR ESTOP GPIO

MTR STM32 has direct ESTOP button GPIO. On trigger: local throttle cut + gear OFF + CAN `0x001` broadcast.

### Additional Trigger: CAN Bus-Off

TWAI TEC > 255 (bus-off condition) → log, auto-recover. ESTOP if persistent. During bus-off, node cannot transmit; peer heartbeat timeout catches it.

---

## 4. ESTOP Exit Logic

### Exit Paths

| Path | Trigger | Result |
|------|---------|--------|
| **Primary** | START button (GPIO41) short press | ESTOP → MANUAL |
| **Secondary** | MODE button (GPIO11) long-press 3 seconds | ESTOP → MANUAL |
| **Ultimate fallback** | Power-cycle (key OFF → ON) | Boot → MANUAL |

### NOT valid exit paths
- CAN command (any ID)
- MODE button short-press (explicitly ignored in ESTOP)
- Automatic timeout recovery
- Mode command via CAN 0x110

### Two independent GPIOs on separate physical buttons
Ensures no single button failure locks the rider in ESTOP.

### ESTOP Exit Sequence (detailed)

```
1. Rider presses START button (GPIO41) or holds MODE button 3s (GPIO11).
2. Brake transitions from max stroke to lever-controlled immediately.
   (Rider can release brake lever — brake follows lever position.)
3. Motor and gear transition to MANUAL pass-through immediately.
   (Throttle grip → ADC → CAN → MTR → DAC. Gear selector → relays.)
4. Steering ramp completion (non-obstacle ESTOP only):
   - RT continues centering ramp at 20°/s via active 0x169 until 0° reached.
   - Mode transition for steering is DEFERRED until ramp completes.
   - This guarantees the vehicle is centered before rider takes over.
   - Up to 2 seconds from full lock (~40°).
5. Once centered, steering becomes fully manual (EPS-C standalone).
   RT stops transmitting 0x169.
6. MANUAL indicator bulb illuminates when full transition is complete.
```

### Why Steering Ramp is Deferred

Without the deferral, pressing START mid-ramp would stop `0x169` transmission immediately, leaving EPS-C to comm-fault at whatever off-center angle the ramp had reached. A 30° steering lock in MANUAL mode (where EPS-C is standalone) would make the vehicle unrideable.

### START Button Health Monitoring

`diag_task` monitors ESTOP duration. If vehicle remains in ESTOP for >30 seconds with no START button activity, a diagnostic flag is set in CAN `0x600 SYS_DIAG_RPT`. Catches stuck/disconnected START button before rider discovers it at roadside.

### MODE Button Long-Press Implementation

```cpp
// mode_manager.cpp — runs at 10 Hz (tick period = 100ms)
if (m_mode == can::Mode::Estop) {
    if (mode_btn_pressed) {
        if (++m_estop_longpress_ctr >= (kEstopLongPressMs / 100)) {
            // kEstopLongPressMs = 3000, so ≥30 ticks
            set_mode(can::Mode::Manual);
            m_estop_longpress_ctr = 0;
            return true;  // caller sends CAN 0x110
        }
    } else {
        m_estop_longpress_ctr = 0;  // released before timeout → reset
    }
}
```

### Debounce

500ms debounce after any button-initiated mode transition (`kDebounceMs = 500`). Prevents bouncing contacts from causing multiple transitions.

---

## 5. Steering During ESTOP — Two-Tier Response

### 5.1 Obstacle-Triggered ESTOP

Triggered by obstacle detection (Jetson perception or RT obstacle sensor — distance ≤ 300mm):

1. **Hold current angle, clamped to dynamic angle limit.** If current angle exceeds speed-safe envelope (e.g., 15° at speed where limit is 5°), ramp down to limit at 20°/s, then hold.
2. After **500ms** of hold at clamped angle: **silent-stop** (stop transmitting `0x169`).
3. EPS-C enters internal comm-fault timeout.

**Why dynamic clamp applies:** During emergency braking while cornering, lateral load transfer reduces rear wheel grip. Large steering angle under hard braking approaches rollover threshold: `a_y = v²/L·tan(δ) > g·w/(2h)`. The clamp prevents this.

**Straight-line case:** If δ ≈ 0° when obstacle detected, hold angle is 0° — behavior identical regardless of clamp.

### 5.2 Non-Obstacle ESTOP

For all other triggers (button press, heartbeat loss, following error, command stale):

1. **Ramp to 0° at 20°/s** via active `0x169` transmission.
2. Continue transmitting during ramp so EPS-C tracks centering.
3. Hold at 0° once centered.
4. **Ramp is non-interruptible by START button** — mode transition defers.

**Rationale:** Straight wheels are the safest default for a stopped vehicle.

### 5.3 Mechanical Jam Fallback

If centering ramp encounters persistent following error (speed-scaled threshold for >1s during ramp):

1. **Fall back to silent-stop** — linkage is likely mechanically jammed.
2. Stop transmitting `0x169`.
3. **Steer SM transitions to `STEER_FAULT`** explicitly.
4. EPS-C timeout-faults internally.

**Recovery from STEER_FAULT:**
- **START short-press** → reset to `STEER_LISTEN_SYNC` (waits for `0x201`, syncs current angle).
- **START long-press (3s) + throttle at zero** → force-activate with target=0° (MANUAL mode only; AUTO locked out with blinking AUTO bulb).
- If neither works (EPS-C remains fault-locked): **power-cycle** as final recovery.

### Steering State Machine States

| State | Value | Description |
|-------|-------|-------------|
| `STEER_BOOT_WAIT` | 0 | 500ms power-on delay, no TX |
| `STEER_LISTEN_SYNC` | 1 | Wait for 0x201, read angle, wait for aligned |
| `STEER_ACTIVE` | 2 | Normal operation, transmit 0x169 at 50 Hz |
| `STEER_ESTOP_RAMP` | 3 | ESTOP ramp-to-zero at 20°/s (non-obstacle) |
| `STEER_ESTOP_HOLD` | 4 | ESTOP hold at clamped angle (obstacle) |
| `STEER_FAULT` | 5 | Timeout or ESTOP, stop transmitting |

---

## 6. Heartbeat System — Full Liveness Matrix

### Heartbeat Frames

| CAN ID | Sender | Receiver(s) | Bus | DLC | Period | Signals |
|--------|--------|-------------|-----|-----|--------|---------|
| `0x7FD` | RT → SYS | SYS | Low | 2 | 2 Hz (500ms) | byte 0=alive_ctr, byte 1=health_flags |
| `0x7FD` | RT → Jetson | Jetson | High | 2 | 2 Hz (500ms) | byte 0=alive_ctr, byte 1=health_flags |
| `0x7FE` | SYS → RT | RT | Low | 2 | 10 Hz (100ms) | byte 0=alive_ctr, byte 1=health_flags |
| `0x7FC` | Jetson → RT | RT | High | 1 | 2 Hz (500ms) | byte 0=alive_ctr |
| `0x7FB` | PWT → RT, SYS | RT, SYS | Low | — | — | byte 0=alive_ctr |

**Critical:** RT sends `0x7FD` independently on both buses with **separate counters**. Heartbeat frames are NEVER bridged between buses — each bus is an independent liveness domain.

### Health Flags Byte (byte 1 of 0x7FD, 0x7FE)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | heartbeat_ok | 1 = node believes its own heart is beating |
| 1 | estop_active | 1 = node believes ESTOP is active |
| 2 | mode_auto | 1 = node believes mode is AUTO (RT only) |
| 3 | can_ok | 1 = CAN controller is healthy (RT only) |
| 4-7 | reserved | — |

### Liveness Monitoring Matrix (who watches whom)

| Monitor | Watchee | CAN ID | Bus | Period | Timeout | Missed Frames | Action on Loss |
|---------|---------|--------|-----|--------|---------|---------------|----------------|
| **SYS** | RT | `0x7FD` | Low | 2 Hz | **1000ms** | 2 missed | ESTOP (AUTO only) |
| **RT** | SYS | `0x7FE` | Low | 10 Hz | **200ms** | 2 missed | RT brake takeover: transmits 0x7B9 with stroke=max (full brake). Also broadcasts CAN 0x001. MTR kills motor + gear locally. Brake gap ≤220ms. |
| **RT** | Jetson | `0x7FC` | High | 2 Hz | **1500ms** | 3 missed | **Assisted stop** (NOT ESTOP): zero 0x204 speed, stop 0x169, 0x205 brake=2000 kPa (~2 MPa moderate brake). Transition SYS to MANUAL. Brake light ON. Rider can override with lever. DC-DC stays on. |
| **Jetson** | RT | `0x7FD` | High | 2 Hz | 1500ms | 3 missed | Stop publishing `/cmd_vel` |

### SYS Heartbeat at 10 Hz — Why So Fast?

SYS heartbeat is at 10 Hz (100ms period) specifically for fast brake-loss detection. If SYS dies, RT must detect it within 200ms and take over 0x7B9 brake command. At 25 km/h (~7 m/s), 200ms = 1.4m travel. Standard 2 Hz would mean 1000ms detection → 7m of uncontrolled travel.

### 0x204 Staleness — Faster Than Heartbeat

SYS also monitors `0x204 RT_DRIVE_CMD` staleness at 200ms (20 missed frames at 100 Hz). This catches RT crash faster than the 1000ms heartbeat timeout. Two independent checks: data-quality (200ms staleness) + node-liveness (1000ms heartbeat).

### 0x206 Staleness (MTR)

SYS monitors `0x206 MTR_MOTOR_FBK` staleness at 200ms. MTR comms lost → CAN `0x001` ESTOP.

### Startup Grace Period

Heartbeat checks are masked for the first **3 seconds** (3000ms) after boot to prevent false ESTOP before first frames arrive.

```cpp
bool heartbeat_ok() const {
    int64_t now = get_time_us();
    int64_t last = m_last_hb_us.load();
    if (last == 0)
        return (now < int64_t(shared::kStartupGracePeriodMs) * 1000);  // 3000ms
    return (now - last) < int64_t(kHeartbeatTimeoutMsRt) * 1000;
}
```

---

## 7. Alive Counter & Frozen Detection

### Why Not Just "Frame Present"?

A frozen CPU's CAN controller can keep DMA-ing the last frame from its TX mailbox forever — perfectly timed 50 Hz stream, but the node is dead. The **alive counter** defeats this.

```
Frame 1: alive_ctr = 0x01
Frame 2: alive_ctr = 0x02
Frame 3: alive_ctr = 0x03   ← CPU freezes here
Frame 4: alive_ctr = 0x03   ← CAN controller replays frame 3 forever
Frame 5: alive_ctr = 0x03   ← receiver detects: same counter twice → node is dead
```

### Implementation

```cpp
void SafetyMonitor::feed_heartbeat_rt(uint8_t alive_ctr) {
    // Frozen counter detection: same value as last = stuck CAN controller
    if (m_hb_ever_seen.load(std::memory_order_relaxed) && alive_ctr == m_last_hb_ctr) {
        return;  // frozen — don't update timestamp → will time out
    }
    m_last_hb_ctr = alive_ctr;
    m_last_hb_us.store(get_time_us());
    m_hb_ever_seen.store(true, std::memory_order_relaxed);
}
```

### Dual Heartbeat Independence

RT maintains **separate uint8_t counters** for low bus and high bus via `DualHeartbeat::tick_low()` / `tick_high()`. If one bus fails, the other continues independently. Counters wrap naturally at 256.

```cpp
class DualHeartbeat {
    void tick_low(can::Frame& out, uint8_t health_flags=0) {
        out.id = can::kIdRtHeartbeatLow;  // 0x7FD
        out.dlc = 2;
        out.put_u8(0, ++m_ctr_low);       // independent counter
        out.put_u8(1, health_flags);
    }
    void tick_high(can::Frame& out, uint8_t health_flags=0) {
        out.id = can::kIdRtHeartbeatHigh; // 0x7FD
        out.dlc = 2;
        out.put_u8(0, ++m_ctr_high);      // independent counter
        out.put_u8(1, health_flags);
    }
    uint8_t m_ctr_low = 0;
    uint8_t m_ctr_high = 0;
};
```

### Heartbeat vs External Watchdog

| Failure | Heartbeat catches? | External Watchdog catches? |
|---------|:------------------:|:--------------------------:|
| CPU hung, CAN controller alive | ✓ (same counter twice) | ✗ (watchdog GPIO may still toggle from DMA) |
| CPU hung, CAN controller dead | ✓ (no frames) | ✓ (independent IC) |
| Crystal failure | ✓ (no frames) | ✓ (independent oscillator) |
| CAN transceiver dead | ✓ (no frames from that node) | ✗ (MCU is fine) |
| Power brownout | ✓ (no frames) | ✓ (separate power-on-reset) |
| Latch-up (ESD, overvoltage) | ✓ (no frames) | ✓ (external IC unaffected) |

**Neither alone is sufficient.** Both together cover the failure space.

---

## 8. CAN Frames — ESTOP & Safety

### Primary ESTOP Frames

| CAN ID | Name | Bus | DLC | Period | Purpose |
|--------|------|-----|-----|--------|---------|
| `0x001` | SAFETY_ESTOP | Both | 0 | Event | Empty frame — the CAN ID itself IS the ESTOP signal. Highest priority. Any node can send. RT bridges bidirectionally. |
| `0x011` | SYS_SAFETY_STS | Both | 3 | 5 Hz | SYS→RT(→Jetson): `SYS_EstopActive` (byte 0), `SYS_HeartbeatOk` (byte 1), `SYS_LightState` (byte 2). |
| `0x110` | SYS_MODE_CMD | Low | 1 | On change | SYS→RT,MTR: mode enum (0=Manual, 1=Auto, 2=ESTOP). Low bus only. |
| `0x210` | RT_STATE_RPT | Both | 6 | 10 Hz | RT→Jetson,SYS: `RT_Mode`, `RT_SafetyState` (byte 1 bits 0-1: 0=Normal, 1=InternalEstop, 2=Fault), `RT_EstopReason` (byte 1 bits 4-7), `RT_Reversing`, `RT_RxOverflow`, `RT_TaskHealth`, `RT_SteerState`. |
| `0x600` | SYS_DIAG_RPT | Both | 8 | 1 Hz | SYS→RT(→Jetson): `SYS_DiagMode`, `SYS_DiagBrakeEngaged`, `SYS_DiagBrakeFault`, `SYS_DiagHeartbeatOk`, `SYS_DiagEstopActive`, `SYS_DiagFreeHeapKb`, `SYS_DiagTec`, `SYS_DiagRec`. |

### ESTOP Reason Codes (0x210 byte 1 bits 4-7)

| Value | Enum Name | Trigger |
|-------|-----------|---------|
| 0 | None | No ESTOP |
| 1 | Button | Physical ESTOP button pressed |
| 2 | Heartbeat | Heartbeat timeout (any node) |
| 3 | FollowingError | Steering following error |
| 4 | Obstacle | Obstacle within stop distance |
| 5 | CanEstop | CAN 0x001 received |
| 6 | BusOff | CAN bus-off condition |
| 7 | Internal | Internal fault |

### Actuator Command Frames — ESTOP Behavior

| CAN ID | Name | Normal | ESTOP Behavior |
|--------|------|--------|----------------|
| `0x169` | VCU_SES_REQ (steering cmd) | 50 Hz active control | Obstacle: hold→silent-stop. Non-obstacle: ramp→0°→silent-stop. Rolling counter + checksum. |
| `0x7B9` | VCU_SEB_REQ (brake cmd) | 50 Hz, mode-dependent | Stroke=max (27mm, raw=1140). Rolling counter + checksum. |
| `0x204` | RT_DRIVE_CMD (motor cmd) | 100 Hz speed+gear | Speed=0, Gear=N |
| `0x012` | SYS_DCDC_CMD | On change | Enable=1 (ON — MCUs need power) |

### CAN Forwarding Rules for ESTOP

```
0x001 forwarding: RT transparently forwards between buses (bypasses gateway queue)
    Low bus 0x001 → High bus 0x001
    High bus 0x001 → Low bus 0x001

0x011 (SYS_SAFETY_STS): Forwarded low→high by RT
0x210 (RT_STATE_RPT): Sent independently on both buses
0x600 (SYS_DIAG_RPT): Forwarded low→high by RT

Heartbeats (0x7FC/0x7FD/0x7FE): NEVER bridged. Each bus is independent.
```

---

## 9. Mode State Machine

### States

```cpp
enum class Mode : uint8_t {
    Manual = 0,  // Rider in direct control
    Auto   = 1,  // Jetson driving
    Estop  = 2   // Emergency stop (absorbing state)
};
```

### Transitions

```
Power-on → MANUAL (always)
MANUAL ←→ AUTO (MODE button short press)
MANUAL → ESTOP (ESTOP button, CAN 0x001, heartbeat timeout, following error, command stale, SEB L3)
AUTO   → ESTOP (same triggers)
ESTOP  → MANUAL (START button short press, MODE button long-press 3s, or power-cycle)
```

### Mode Effect on Subsystems

| Subsystem | MANUAL | AUTO | ESTOP |
|-----------|--------|------|-------|
| Steering | EPS-C standalone (RT monitors 0x201, no TX) | RT active control via 0x169 | Obstacle: hold→silent. Non-obstacle: ramp→0°→silent. |
| Brake | Lever → SYS → 0x7B9 (Stroke mode) | Jetson 0x301 → RT → 0x205 → SYS → 0x7B9 | 0x7B9 stroke=max |
| Throttle | Grip ADC → CAN → MTR DAC | Jetson 0x300 → RT PID → MTR DAC | DAC=0V |
| Gear | Selector → TLP281 → MTR relays | Jetson 0x300 gear → MTR relays | All OFF (Neutral) |
| Lights | Handlebar switches → GPIOs | Jetson 0x302 → GPIOs | Brake ON, all others OFF |
| DC-DC | ON | ON | ON (MCUs need power) |
| 12V Relay | ON | ON | OFF (non-safety loads cut) |

### Implementation Pattern

```cpp
// SYS mode_manager.cpp
// MODE button toggles MANUAL↔AUTO, ignored in ESTOP.
// START button exits ESTOP→MANUAL.
// MODE long-press (3s) exits ESTOP→MANUAL (secondary path).

bool ModeManager::tick(bool mode_btn_pressed, bool start_btn_pressed) {
    // MODE button long-press (3s) in ESTOP → MANUAL (gap #11)
    if (m_mode == can::Mode::Estop) {
        if (mode_btn_pressed) {
            if (++m_estop_longpress_ctr >= (kEstopLongPressMs / 100)) {  // 30 ticks @10Hz
                set_mode(can::Mode::Manual);
                return true;
            }
        } else {
            m_estop_longpress_ctr = 0;
        }
    }
    
    // START button falling edge → ESTOP→MANUAL
    if (falling_edge(m_prev_start_btn, start_btn_pressed)) {
        if (m_mode == can::Mode::Estop) {
            set_mode(can::Mode::Manual);
            return true;
        }
    }
    
    // MODE button falling edge → toggle MANUAL↔AUTO (ignored in ESTOP)
    if (falling_edge(m_prev_mode_btn, mode_btn_pressed)) {
        if (m_mode == can::Mode::Manual) set_mode(can::Mode::Auto);
        else if (m_mode == can::Mode::Auto) set_mode(can::Mode::Manual);
        return true;
    }
    return false;
}
```

### ESTOP Cannot Be Set via CAN 0x110

```cpp
void ModeManager::set_from_can(uint8_t m) {
    // Only MANUAL (0) and AUTO (1) are selectable via CAN 0x110.
    // ESTOP is a safety state triggered exclusively by hardware button,
    // CAN 0x001, or safety faults — never via mode command.
    if (m <= 1) set_mode(static_cast<can::Mode>(m));
}
```

---

## 10. Listen Before Speaking — Post-ESTOP Recovery

When a CAN actuator powers on or recovers from ESTOP/watchdog reset, it has no idea what state the vehicle is in. If the controller immediately sends a position command, the actuator will jerk to that target.

### LBS State Machine (Used by both Steering and Brake)

```
Power-on / ESTOP exit / Watchdog reset
    │
    ▼
┌──────────────┐
│  BOOT_WAIT   │  500ms delay — do NOT transmit command frames
│              │  Actuator is powering up, may not be ready
└──────┬───────┘
       │ 500ms elapsed
       ▼
┌──────────────┐
│ LISTEN_SYNC  │  Wait for first status frame (0x201 or 0x721)
│              │  Extract current physical position (angle or stroke)
│              │  Set active_target = current_physical_position
│              │  Wait for alignment_status bit == 1
└──────┬───────┘
       │ Status received + aligned
       ▼
┌──────────────┐
│   ACTIVE     │  Transmit command at 50 Hz continuously
│              │  First frame commands "stay where you are"
│              │  Then follow controller targets with rate limiting
└──────┬───────┘
       │ Timeout (no status for >2s) or ESTOP
       ▼
┌──────────────┐
│    FAULT     │  Stop transmitting
│              │  Actuator will timeout-fault internally
└──────────────┘
```

### EPS-C (Steering) LBS Parameters

| Parameter | Value |
|-----------|-------|
| Status ID | `0x201` SES_STATUS (100 Hz from EPS-C) |
| Position field | `SES_StrAngle` (u16, 0.1°/bit, offset -3000) |
| Alignment field | `SES_INF_Angle_Status` (bit 0 of byte 0) |
| Command ID | `0x169` VCU_SES_REQ (50 Hz to EPS-C) |
| Boot wait | 500ms |
| Sync timeout | 5000ms → STEER_FAULT |

### SEB (Brake) LBS Parameters

| Parameter | Value |
|-----------|-------|
| Status ID | `0x721` SEB_STATUS (100 Hz from SEB) |
| Position field | `SEB_Stroke_Value` (u16, 0.05 mm/bit, offset -30) |
| Alignment field | `SEB_Alignment_Status` (bit 0 of byte 0) |
| Command ID | `0x7B9` VCU_SEB_REQ (50 Hz to SEB) |
| Boot wait | 500ms |
| Sync timeout | 2000ms → BRAKE_DEGRADED |

**BRAKE_DEGRADED mode:** If SEB boot sync times out, SYS enters BRAKE_DEGRADED (not terminal FAULT). The brake lever remains functional — SYS transmits 0x7B9 with lever-based stroke defaults (0mm released, 15mm pressed) without needing sync. Recovers to BRAKE_ACTIVE when first valid 0x721 arrives.

### Comm-Fault Timeout (The Speaking Obligation)

Once ACTIVE, the controller has an ongoing duty to transmit at 50 Hz:
- **EPS-C:** No `0x169` for >20ms → internal comm fault → locks or goes limp.
- **SEB:** No `0x7B9` for >20ms → similar timeout.

Silence is interpreted as controller failure. This is why ESTOP uses "silent-stop" — deliberately stopping transmission to trigger the actuator's internal fault response.

---

## 11. External Watchdog — Last-Resort Reset

Each ESP32-S3 has a dedicated external watchdog IC (TPS3850) on a separate chip. This catches failures that internal watchdogs miss.

### Why External?

| Failure mode | Internal WDT catches? | External WDT catches? |
|-------------|:--------------------:|:---------------------:|
| Task deadlock | ✓ (if idle starved) | ✓ (safety task stops toggling) |
| Infinite ISR | ✗ (ISR preempts WDT) | ✓ (safety task never runs) |
| Crystal oscillator failure | ✗ (no clock) | ✓ (independent RC oscillator) |
| Flash corruption | ✗ (WDT is code too) | ✓ (independent silicon) |
| Voltage brownout | ✗ (WDT browns out too) | ✓ (separate POR) |
| Latch-up (ESD) | ✗ (silicon locked) | ✓ (external IC unaffected) |

### Configuration

| Node | Toggle GPIO | Toggled by | Period | Timeout | Travel at 25km/h |
|------|------------|-----------|--------|---------|-------------------|
| RT | GPIO21 | `control_task` | 100 Hz (10ms) | ~100ms | ~0.7m |
| SYS | GPIO47 | `safety_task` | 20 Hz (50ms) | ~100ms | ~0.7m |

### On Watchdog Reset

1. ESP32-S3 EN pin pulled LOW → MCU resets.
2. All GPIOs go to high-impedance input.
3. Motor controller sees 0V throttle (MCP4725 loses I2C, output = 0V).
4. All gear relays de-energize (GPIOs float → transistor OFF → relay OPEN).
5. **Brake behavior during reset window (critical gap):** SEB continues internal control loop with last received 0x7B9. After 20ms without new frame, SEB enters comm-fault. **Empirically unverified** — if SEB holds pressure: ~20ms window (safe). If SEB releases: ~2.5s window until SYS reboots + brake LBS completes. A hardware brake-hold relay gated by TPS3850 RST line is recommended.
6. After ~200ms startup delay, ESP32 reboots → MANUAL mode → LBS sequences → normal operation.
7. Recovery time from watchdog fire to first valid CAN frame: **<500ms**.

### Post-Reset Firmware

```cpp
void app_main() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_PANIC || reason == ESP_RST_WDT) {
        // External watchdog likely triggered — log
    }
    // Default to MANUAL mode, run LBS sequences for all actuators
}
```

---

## 12. ESTOP Rate Limiting

Prevents a corrupted node from flooding the bus with persistent ESTOP frames (denial-of-service).

### Per-Node Rate Limit

| Parameter | Value |
|-----------|-------|
| Window | 500ms sliding window |
| Max frames | 2 per window |
| DLC=1 with sender-ID byte | Gap #14, not yet implemented |

### Implementation

```cpp
// In shared_config.h
constexpr int64_t kEstopBroadcastMinIntervalUs = 250'000;  // 250ms min between sends

inline bool should_send_estop_now(std::atomic<int64_t>& last_sent_us, int64_t now_us) {
    int64_t last = last_sent_us.load(std::memory_order_relaxed);
    if (now_us - last < kEstopBroadcastMinIntervalUs) return false;
    last_sent_us.store(now_us, std::memory_order_relaxed);
    return true;
}
```

### Receiver-Side Rate Limiting (SYS)

```cpp
// In simulation — SYS tracks ESTOP frame timestamps
private estopTimestamps: number[] = [];

private trackEstopEvent(nowMs: number): void {
    this.estopTimestamps.push(nowMs);
    // Prune entries older than 500ms
    const windowMs = 500;
    this.estopTimestamps = this.estopTimestamps.filter(t => nowMs - t <= windowMs);
    if (this.estopTimestamps.length > 2) {
        console.warn(`[SYS] ESTOP rate-limit: >2 frames in ${windowMs}ms window`);
    }
    this.safety.setEstop(true);  // Still processes for safety
}
```

**Important:** Rate limiting logs a warning but still processes the ESTOP — safety takes priority over rate limiting. The limit prevents denial-of-service, not safety response.

---

## 13. EGAS 3-Level Motor Safety Architecture

The separation of MTR (STM32) from SYS (ESP32-S3) follows the EGAS 3-level electronic throttle monitoring concept (ISO 26262 ASIL-C):

```
Level 3: Hardware — ESTOP button wired direct to both MTR and SYS
         TPS3850 external watchdog on each MCU. No software, no CAN.
         ESTOP press → MTR cuts throttle + gear instantly (local).

Level 2: Function Monitor — SYS ESP32-S3
         Monitors MTR via CAN: compares 0x204 setpoint vs 0x206 feedback.
         Mismatch >500 mm/s for >500ms → CAN 0x001 ESTOP.
         Also handles QM body functions (lights, DCDC, indicators).

Level 1: Function Controller — MTR STM32
         Normal actuation: reads sensors, drives MCP4725 DAC + gear relays.
         MANUAL: pass-through from grip/gear. AUTO: follows CAN 0x204.
         No wireless, no OS, minimal attack surface.
```

### EGAS Design Principles

| Principle | Implementation |
|-----------|---------------|
| **Freedom from interference** | Separate MCUs — a SYS crash/hang cannot block motor kill |
| **ASIL decomposition** | MTR (QM level sensor reads) + SYS monitor (ASIL-B comparison) → combined ASIL-C |
| **Independent safe state path** | ESTOP button wired to both MCUs — MTR cuts throttle locally, zero CAN delay |
| **Diverse monitoring** | SYS compares commanded speed vs actual from 0x206 — mismatch → ESTOP |

---

## 14. Emergency Response Matrix

| Trigger | Speed | Gear | Brake | Steering | DCDC | 12V Acc | Brake Light | Exit |
|---------|-------|------|-------|----------|------|---------|-------------|------|
| **ESTOP button** | 0V | N | Max (27mm) | Obstacle: hold(dyn-clamped)→silent. Non-obstacle: ramp→0° | ON | OFF | ON | START/MODE-long→MANUAL |
| **CAN 0x001** | 0V | N | Max | Same as button | ON | OFF | ON | START/MODE-long→MANUAL |
| **RT heartbeat lost** (SYS detects) | 0V | N | Max | Same as button | ON | OFF | ON | START/MODE-long→MANUAL |
| **SYS heartbeat lost** (RT detects) | 0V (MTR local) | N (MTR local) | Max (RT takeover 0x7B9) | EPS-C timeout* (RT still alive) | ON (RT alive) | OFF | ON | SYS reboot→re-sync; MANUAL |
| **Steering follow err** | 0V | N | Max | Obstacle: hold(dyn-clamped)→silent. Non-obstacle: ramp→0° | ON | OFF | ON | START/MODE-long→MANUAL |
| **Jetson HB lost** (RT detects) | 0 mm/s | N | Moderate (2000 kPa via 0x205) | Stop 0x169 | ON | ON | ON | Auto-recover; MANUAL |
| **0x300 stale** (500ms) | 0 mm/s (coast) | N | Lever only | Stop 0x169 | ON | ON | Normal | Auto-recover |
| **0x204 stale** (200ms SYS) | 0 mm/s | N | Lever only | No change | ON | ON | Normal | Auto-recover |
| **0x204 stale** (200ms MTR) | 0V (local cut) | N (local cut) | Lever only | No change | ON | ON | Normal | Auto-recover |
| **Watchdog reset** | 0V (MTR local) | N (MTR local) | SEB timeout* | EPS-C timeout* | OFF→ON | OFF | OFF→ON | Reboot→MANUAL |
| **CAN bus-off** | Depends | Depends | Depends | Depends | Depends | Depends | Depends | Auto-recover or ESTOP if persistent |

> \* **Watchdog reset brake window (critical):** See §11. SEB behavior during SYS reset is empirically unverified. A hardware brake-hold relay gated by TPS3850 RST line is recommended.

### Response Levels (Graduated)

| Level | Trigger | Response | Reversible? |
|-------|---------|----------|-------------|
| **ESTOP** (full) | Button, CAN 0x001, heartbeat loss, follow err, SEB L3 | Full brake, motor kill, gear OFF, steering safe-state, 12V cut | START button only → MANUAL |
| **Assisted Stop** | Jetson heartbeat loss | Zero speed, moderate brake (2000 kPa), stop steering | Auto-recover when Jetson returns |
| **Coast Stop** | Command staleness (0x300, 0x204) | Zero speed setpoint, gear N | Auto-recover when commands resume |
| **Watchdog Reset** | MCU hang (external WDT fire) | Hardware reset → safe state → reboot → MANUAL | Post-boot: MANUAL mode |

---

## 15. Testing the Emergency System

### 15.1 ESTOP Button Test (Pre-Ride)

1. Vehicle stationary, MANUAL mode.
2. Press ESTOP button.
3. Verify: brake light ON, mode indicators OFF, throttle grip produces no response, gear selector produces no response.
4. Press START button → verify transition to MANUAL mode.
5. Verify: throttle and gear respond again.

### 15.2 CAN ESTOP Test

1. Vehicle stationary, on stands (wheels off ground).
2. Trigger CAN `0x001` from a CAN analyzer tool.
3. Verify same ESTOP behavior as physical button.

### 15.3 Heartbeat Timeout Test

1. Vehicle stationary, on stands, AUTO mode.
2. Disconnect SYS from CAN bus (unplug SN65HVD230).
3. Verify: within 1000ms, RT detects SYS heartbeat loss and broadcasts CAN `0x001`.
4. Verify: ESTOP behavior activates.
5. Reconnect and restart.

### 15.4 Steering Following Error Test

1. Vehicle stationary, on stands, AUTO mode.
2. Mechanically block the steering linkage.
3. Command a steering angle change via Jetson (or simulated CAN).
4. Verify: within 300ms of exceeding speed-scaled error threshold, ESTOP triggers.

### 15.5 Command Staleness Test

1. Vehicle stationary, on stands, AUTO mode.
2. Stop Jetson ROS 2 bridge (or disconnect high CAN).
3. Verify: within 500ms, RT zeros `0x204` speed and stops `0x169`.
4. Verify: SYS detects `0x204` staleness within 200ms, forces speed=0 and gear=N.

### 15.6 External Watchdog Test

1. Comment out the WDT toggle in `safety_task` (SYS) or `control_task` (RT).
2. Flash and run.
3. Verify: MCU resets within ~100ms.
4. Verify: during reset window, motor controller sees 0V throttle.
5. Restore toggle and re-flash.

### 15.7 Dynamic Angle Clamp Test

1. Vehicle stationary, on stands, AUTO mode.
2. Send a CAN `0x300` command with speed=25 km/h and yaw_rate requesting 40° steering.
3. Verify: RT clamps steering to ~5° (not 40°).
4. Verify: `0x169` transmitted angle is ≤5°.

### 15.8 ESTOP Latch Test (PCR3)

From `native-test/test/test_estop_latch.cpp`:
1. ESTOP event → run_safety_checks returns ESTOP actions (zero_setpoints, brake, disable_steering).
2. ESTOP persists across cycles (latch, not one-shot) — verified with no new ESTOP injection across 3 cycles.
3. SYS mode change to non-ESTOP → latch clears.
4. ESTOP + same-cycle mode change to non-ESTOP: ESTOP wins (had_estop_this_cycle guard).
5. ESTOP clears on next mode change after ESTOP processed.

### 15.9 Heartbeat Recovery Test

From `native-test/test/test_heartbeat_recovery.cpp`:
1. Fresh heartbeat → no timeout, normal operation.
2. Timeout (>200ms) → seb_takeover=true, zero setpoints.
3. Heartbeat recovered → seb_takeover=false, system resumes.
4. Host heartbeat timeout (>1500ms) → assisted stop brake=2000 kPa.
5. Startup grace period suppresses heartbeat checks.
6. Host heartbeat recovery → assisted stop condition clears.

### 15.10 Dual Heartbeat Independence Test

From `native-test/test/test_dual_heartbeat.cpp`:
1. Both counters start at 0 after init.
2. Low and high counters increment independently.
3. Tick low bus only — high stays unchanged.
4. Tick high bus only — low stays unchanged.
5. Counter wrap at 256 — independent.
6. Health flags can be set independently per bus.

---

## 16. Configuration Constants

### Shared (`shared/shared_config.h`)

| Constant | Value | Description |
|----------|-------|-------------|
| `kObstacleStopMM` | 300 | Obstacle distance triggering ESTOP (mm) |
| `kObstacleClearMM` | 3000 | Obstacle considered clear (mm) |
| `kMaxSpeedFwdMmps` | 3000 | Max forward speed (mm/s) |
| `kMaxSpeedRevMmps` | 500 | Max reverse speed (mm/s) |
| `kLowSpeedThreshMmps` | 50 | Below this = low speed |
| `kHostCmdStaleTimeoutMs` | 500 | RT watchdog for 0x300 staleness |
| `kHeartbeatTimeoutMsHost` | 1500 | Host heartbeat timeout |
| `kStartupGracePeriodMs` | 3000 | Heartbeat check grace period at boot |
| `kEstopBroadcastMinIntervalUs` | 250,000 | Min 250ms between 0x001 broadcasts |
| `kMaxBrakeKpa` | 5000 | SEB physical limit (5 MPa) |
| `kAssistStopKpa` | 2000 | Jetson loss → assisted stop pressure |

### SYS (`sys-esp32/src/config.h`)

| Constant | Value | Description |
|----------|-------|-------------|
| `kEstopGpio` | 1 | ESTOP button input |
| `kBrakeLeverGpio` | 2 | Brake lever input |
| `kStartBtnGpio` | 32 | START button (ESTOP exit) |
| `kModeBtnGpio` | 11 | MODE button (toggle + long-press exit) |
| `kHeartbeatIntervalMs` | 100 | SYS heartbeat at 10 Hz |
| `kHeartbeatTimeoutMsRt` | 1000 | RT heartbeat loss timeout |
| `kSetpointStaleMs` | 200 | 0x204 staleness timeout |
| `kSafetyCheckHz` | 20 | Safety task frequency |
| `kDebounceMs` | 500 | Button debounce period |
| `kBrakeMaxStroke` | 27.0 | ESTOP brake stroke (mm) |
| `kBrakeManualStroke` | 15.0 | Manual lever brake stroke (mm) |
| `kEgasSpeedThresholdMmps` | 500 | EGAS L2 speed mismatch threshold |
| `kEgasFaultDurationMs` | 500 | EGAS L2 fault persistence |
| `kBrakeFollowingErrRaw` | 60 | Brake following error = 3mm |
| `kBrakeFollowingErrMs` | 100 | Brake error persistence |
| `kSebStatusTimeoutMs` | 100 | SEB status staleness timeout |
| `kEstopLongPressMs` | 3000 | MODE button long-press for ESTOP exit |
| `kMtrEstopAckTimeoutMs` | 100 | MTR ESTOP ACK timeout |
| `kMtrFbkStaleMs` | 200 | MTR feedback staleness timeout |
| `kEstopRateLimitWindowMs` | 500 | ESTOP rate limit window |
| `kEstopRateLimitMax` | 2 | Max ESTOP frames per window |

### RT (`rt-esp32/src/config.h`)

| Constant | Value | Description |
|----------|-------|-------------|
| `kSteerFollowingErrMinDeg` | 2.0 | Floor for following error threshold |
| `kSteerFollowingErrFactor` | 0.25 | × dynamic_limit → threshold |
| `kSteerFollowingErrMs` | 300 | Must persist before ESTOP |
| `kSteerCmdRateHz` | 50 | Steering command rate (20 ms contract period) |
| `kAngleClampBaseDeg` | 40.0 | Max steering at 2 km/h |
| `kAngleClampMinDeg` | 5.0 | Min steering at 25 km/h |
| `kSteerEstopRampDegS` | 20.0 | ESTOP ramp-to-zero rate |
| `kSteerEstopHoldMs` | 500 | Obstacle ESTOP hold then silent-stop |
| `kHeartbeatIntervalMs` | 500 | RT heartbeat at 2 Hz |
| `kHeartbeatTimeoutMsSys` | 200 | SYS heartbeat timeout (2 missed at 10 Hz) |
| `kWdtToggleGpio` | 21 | Watchdog toggle GPIO |

---

## 17. Related Documents

| Document | Content |
|----------|---------|
| `docs/emergency-system.md` | Complete ESTOP system, trigger paths, response matrix, rider's guide |
| `docs/defense-in-depth-safety.md` | Layered safety approach, each layer detailed |
| `notes/heartbeat-monitoring.md` | Liveness detection, alive counter, FTTI rationale |
| `notes/distributed-safety-patterns.md` | Defense in depth, NC wiring, fail-safe vs fail-operational |
| `docs/external-watchdog.md` | TPS3850 watchdog IC — timeout, safe state, testing |
| `docs/hardware-safety.md` | Per-component fail-safe behavior, power-up sequence |
| `docs/listen-before-speaking.md` | CAN actuator safe bootstrapping |
| `docs/operator-manual.md` | Rider's guide, controls, emergency procedures |
| `docs/hil-safety-test-plan.md` | 19 HIL safety test scenarios |
| `docs/traceability-matrix.md` | HARA hazards → safety goals → reqs → implementation → tests |
| `architecture.md` §3 | Mode state machine (MANUAL ↔ AUTO → ESTOP) |
| `architecture.md` §6.1 | EGAS 3-level motor safety |
| `architecture.md` §7.6 | Steering safety mechanisms |
| `architecture.md` §8.6 | Brake control, heartbeats, watchdog, physical controls |
| `can-dictionary.md` | Full CAN signal catalog (all IDs, bit layouts, period, type) |
| `shared/can/can_high.yaml` | High-bus CAN definitions (single source of truth) |
| `shared/can/can_low.yaml` | Low-bus CAN definitions (single source of truth) |
| `shared/shared_config.h` | Shared constants (safety timeouts, brake limits, vehicle geometry) |
| `sys-esp32/src/config.h` | SYS config (GPIOs, safety thresholds, timing) |
| `rt-esp32/src/config.h` | RT config (steering clamps, rates, watchdog GPIO) |
| `sys-esp32/src/safety_monitor.h` | SYS safety monitor class (heartbeat, ESTOP GPIO) |
| `sys-esp32/src/safety_monitor.cpp` | SYS safety monitor implementation (alive counter validation) |
| `rt-esp32/src/safety_monitor.h` | RT safety checks + ESTOP rate limiter |
| `sys-esp32/src/mode_manager.h` | SYS mode manager (button debounce, mode transitions) |
| `sys-esp32/src/mode_manager.cpp` | Mode FSM implementation (START/MODE button logic) |
| `rt-esp32/src/heartbeat.h` | DualHeartbeat class (independent per-bus counters) |
| `simulation/src/scenarios/estop-flow.ts` | ESTOP simulation scenario |
| `simulation/src/checks/safety-checker.ts` | Continuous safety violation detection in simulation |
| `simulation/src/controllers/sys-safety.ts` | SYS safety monitor simulation port |
| `simulation/src/ecus/rt.ts` | RT ECU simulation (safety checks, heartbeat monitoring) |
| `simulation/src/ecus/sys.ts` | SYS ECU simulation (ESTOP rate limiting, SEB L3 escalation) |
| `native-test/test/test_estop_latch.cpp` | ESTOP latch PCR3 verification |
| `native-test/test/test_dual_heartbeat.cpp` | Dual heartbeat independence tests |
| `native-test/test/test_heartbeat_recovery.cpp` | Heartbeat timeout + recovery tests |
| `notes/state-machine-design.md` | FSM patterns, absorbing state concept |
