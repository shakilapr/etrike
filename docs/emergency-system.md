# Emergency Stop & Emergency Handling

The E-Trike emergency system uses **defense in depth** — multiple independent, overlapping safety layers. No single component failure defeats all protections. This document describes how the emergency stop (ESTOP) works, how the system handles other emergency conditions, and what the rider should expect.

---

## 1. ESTOP Overview

ESTOP is the system's **absorbing safety state**. When entered, all actuators return to their safe positions and the vehicle stops. ESTOP persists until deliberately cleared by the rider — it never self-clears.

### 1.1 What happens in ESTOP

| Subsystem | Action | Notes |
|-----------|--------|-------|
| **Motor throttle** | MCP4725 DAC = 0 V | Instant motor kill — zero voltage to controller |
| **Gear selection** | All relays OFF → Neutral | 72V gear lines de-energized |
| **Brake (SEB)** | Stroke = max (~27 mm) | Full hydraulic brake pressure, 50 Hz CAN 0x7B9 |
| **Steering (EPS-C)** | Obstacle trigger: hold angle, silent-stop after 500ms. Non-obstacle: ramp to 0° at 20°/s via active 0x169. Fallback to silent-stop if following error persists >1s (mechanical jam). | Two-tier response — see §2.5 |
| **DC-DC converter** | CAN 0x012 enable = **1** (maintains 12V for MCUs, CAN transceivers, and brake light — MCUs need power to run safety tasks) |
| **12V accessory relay** | GPIO40 OFF (cuts headlight, turn signals, mode bulbs — non-safety loads) |
| **Signal lights** | Brake light **ON** (powered from always-on DC-DC rail, not through accessory relay), all others OFF |
| **Mode indicators** | Both AUTO and MANUAL bulbs OFF | Dark dashboard = ESTOP |
| **Throttle pass-through** | Ignored | ADC reads ignored, DAC forced to 0 |
| **Gear pass-through** | Ignored | All gear relays forced OFF |

### 1.2 ESTOP exit — START button with deferred steering ramp completion

ESTOP can only be exited by pressing the **START button** (green, GPIO41 on SYS) or via **MODE button long-press** (GPIO11, 3-second hold). Both exit to **MANUAL mode** — the rider is always in direct control after an ESTOP, never in AUTO.

- **START button (GPIO41):** Short press exits ESTOP → MANUAL immediately (subject to steering ramp completion, see below).
- **MODE button (GPIO11):** Long-press (3 seconds) exits ESTOP → MANUAL. This is the secondary exit path — two independent GPIOs on separate physical buttons ensure no single button failure locks the rider in ESTOP.
- MODE button short-press is ignored in ESTOP (prevents accidental exit).
- CAN commands cannot exit ESTOP.
- No automatic timeout recovery.
- Power-cycle always exits ESTOP (reboot starts in MANUAL by default) — ultimate fallback.

```
ESTOP exit: START button → MANUAL mode
           MODE button long-press (3s) → MANUAL mode (secondary)
           Power-cycle → MANUAL mode (ultimate fallback)
           (NOT CAN command, NOT timeout)
```

**START button health monitoring:** The `diag_task` monitors ESTOP duration. If the vehicle remains in ESTOP for >30 seconds with no START button activity, a diagnostic flag is set in CAN `0x600 SYS_DIAG_RPT`. This catches a stuck or disconnected START button before the rider discovers it at roadside.

**Critical: steering ramp completion before silent handoff.** During non-obstacle ESTOP, RT is actively transmitting `0x169` to ramp steering to 0° at 20°/s (see §3.2). If the rider presses START before the ramp completes, **the mode transition is deferred for steering only** — RT continues the centering ramp to completion, then hands off silently to MANUAL mode. All other subsystems (brake, motor, lights) transition to MANUAL immediately.

**Why this matters:** Without the deferral, pressing START mid-ramp would stop `0x169` transmission immediately, leaving EPS-C to comm-fault at whatever off-center angle the ramp had reached. A 30° steering lock in MANUAL mode (where EPS-C is standalone and provides no active centering) would make the vehicle unrideable. See [[emergency-safety-analysis]] §1 for the full causal trace.

**The rider's experience:**
1. Press START button.
2. Brake transitions from max to lever-controlled immediately (rider can release brake).
3. Motor and gear transition to MANUAL pass-through immediately.
4. Steering continues to center itself (up to 2 seconds from full lock).
5. Once centered, steering becomes fully manual (EPS-C standalone).
6. The MANUAL indicator bulb illuminates when the full transition is complete.

---

## 2. ESTOP Trigger Paths — The 8 Safety Layers

### Layer 1: Physical ESTOP Button (fastest)

A **big red mushroom button** wired **normally-closed (NC)** to SYS GPIO1 and MTR STM32 kEstopGpio.

| Property | Value |
|----------|-------|
| GPIO | SYS: GPIO1, MTR: kEstopGpio (TBD STM32 pin) |
| Type | NC (normally-closed), active-low |
| Detection | SYS `safety_task` @ 20 Hz, MTR direct ISR |
| CAN redundancy | Also broadcasts CAN `0x001` on low bus |

**Fail-safe by construction:** A cut wire, disconnected plug, or power loss pulls the GPIO LOW — all of which trigger ESTOP. There is no failure mode where the button is broken but the system thinks it's fine.

```
Normal operation: GPIO reads HIGH (10k pull-up to 3.3V) → system runs
Button pressed:   GPIO reads LOW (switch connects to GND) → ESTOP
Wire cut/broken:  GPIO reads LOW (pull-up defeated) → ESTOP
Power loss:       GPIO reads LOW → ESTOP
```

**MTR STM32 direct path:** The ESTOP button is also wired directly to the MTR STM32 board. On ESTOP, MTR cuts throttle (MCP4725 = 0V) and gear (all relays OFF) locally — zero CAN dependency, zero software stack dependency. This is the EGAS Level 3 hardware path.

### Layer 2: CAN `0x001` SAFETY_ESTOP

Any node can broadcast `0x001` on either CAN bus to trigger ESTOP. This is the redundant communication path.

| Property | Value |
|----------|-------|
| CAN ID | `0x001` — highest priority on both buses (wins every arbitration) |
| DLC | 0 (DLC=1 with sender-ID byte is gap #14, not yet implemented) |
| Senders | Jetson, RT, SYS, MTR |
| Forwarding | RT transparently forwards between buses (bypasses gateway queue) |

**Rate limiting:** Each node processes at most **2 ESTOP frames per 500ms window**. The first frame triggers ESTOP; subsequent floods are ignored. This prevents a corrupted node from flooding the bus with persistent ESTOP (denial-of-service). The sender-ID byte enables per-sender rate limiting and diagnostic logging of which node triggered ESTOP.

**Why each node might trigger:**
- **Jetson:** Perception stack detects imminent collision that the obstacle sensor missed.
- **RT:** Steering following error exceeds threshold, or SYS heartbeat lost.
- **SYS:** Physical ESTOP button pressed (redundant CAN path), or RT heartbeat lost.
- **MTR:** Local ESTOP GPIO triggered, or motor fault detected.

### Layer 3: Heartbeat Timeout — Node Crash Detection

Each MCU sends a 1-byte alive counter on its own CAN ID (SYS at 10 Hz for fast brake-loss detection, RT and Host at 2 Hz). A frozen counter = a frozen node.

| Watcher | Watchee | CAN ID | Bus | Timeout | Action |
|---------|---------|--------|-----|---------|--------|
| SYS | RT | `0x7FD` | Low | 1000ms (2 missed) | ESTOP (AUTO only) |
| RT | SYS | `0x7FE` | Low | **200ms** (2 missed at 10 Hz) | **RT brake takeover:** RT immediately starts transmitting 0x7B9 with stroke=max (full brake) regardless of current mode. Also broadcasts CAN 0x001. MTR kills motor + gear locally. Brake gap ≤220ms. |
| RT | Jetson | `0x7FC` | High | 1500ms (3 missed) | **Assisted stop:** zero 0x204 speed, stop 0x169, 0x205 brake=2000 kPa (~2 MPa moderate brake), transition SYS to MANUAL. Brake light ON. Rider can override with lever. DC-DC stays on. |
| Jetson | RT | `0x7FD` | High | 1500ms (3 missed) | Stop publishing `/cmd_vel` |

**Alive counter validation:** A frame with the same counter as the previous frame = stuck CAN controller (MCU hung, controller DMA-ing from buffer). Treated as a missed heartbeat.

```cpp
bool heartbeat_is_fresh(uint8_t new_ctr) {
    if (new_ctr != last_alive_ctr) { last_alive_ctr = new_ctr; return true; }
    return false;  // frozen — same counter twice
}
```

**Startup grace period:** Heartbeat checks are masked for the first **3 seconds** after boot to prevent false ESTOP before the first frames arrive.

### Layer 4: Command Staleness — Jetson Hang Detection

RT monitors the last received `0x300 HOST_DRIVE_CMD` timestamp. If Jetson's planning stack or ROS→CAN bridge hangs:

| Check | Watcher | Timeout | Action |
|-------|---------|---------|--------|
| `0x300` staleness | RT `watchdog` task | 500ms | Zero `0x204` + stop `0x169` |
| `0x204` staleness | SYS `motor` task | 200ms | Zero speed + Neutral gear |

The 500ms RT timeout allows for brief Jetson hiccups. The 200ms SYS timeout is tighter because stale actuator data is immediately dangerous.

### Layer 5: Steering Following Error — Mechanical Fault Detection

RT compares commanded steering angle (`0x169`) against actual angle reported by EPS-C (`0x201 SES_StrAngle`) at 100 Hz.

| Parameter | Value |
|-----------|-------|
| Error threshold | **Speed-scaled:** `max(2°, 0.25 × dynamic_limit)` — 2° at 25 km/h, 4.5° at 10 km/h, 10° at 2 km/h |
| Persistence duration | >300ms |
| Action | ESTOP |

**Why speed-scaled:** At high speed, the dynamic clamp limits steering to ~5°. A fixed 5° threshold would require a 100% error (zero response) to trigger — missing an 80% authority loss. At low speed (40° limit), 5° is only 12.5% — too sensitive for parking maneuvers where tire scrub produces normal following errors. The speed-scaled threshold is tight at speed (where precision matters) and tolerant at low speed (where it doesn't).

This catches stuck linkage, rock jams, bent tie rods, EPS-C motor failure, encoder failure, and corrupted CAN frames.

### Layer 6: Dynamic Angle Clamp — Rollover Prevention

The maximum allowable steering angle is inversely proportional to vehicle speed, enforced in RT's `physics_resolve()` before the command reaches the actuator:

| Speed | Max angle |
|-------|-----------|
| 2 km/h | ~40° |
| 10 km/h | ~18° |
| 25 km/h | ~5° |

Even if Jetson commands a dangerous turn (bug, bad planning, or adversarial input), RT clamps it to the safe envelope.

### Layer 7: Software Hard-Stops — Mechanical Protection

All commanded steering angles are clamped to ±40° regardless of source. This prevents the motor from pushing against the physical end-stops (which could strip gears, burn the motor, or bend the linkage).

### Layer 8: External Watchdog — Last-Resort Hardware Reset

Each ESP32-S3 has a dedicated external watchdog IC (TPS3850) on a separate chip.

| Node | Toggle GPIO | Toggled by | Period | Timeout |
|------|------------|-----------|--------|---------|
| RT | GPIO21 | `control_task` | 100 Hz (every 10ms) | ~100ms |
| SYS | GPIO47 | `safety_task` | 20 Hz (every 50ms) | ~100ms |

If the MCU hangs and toggling stops, the watchdog IC asserts a hardware reset. On reset:
1. All GPIOs go high-impedance → motor controller sees 0V throttle, all gear relays open.
2. ESP32 reboots → starts in MANUAL mode → re-runs Listen Before Speaking sequences.
3. Recovery time from watchdog fire to first valid CAN frame: <500ms.

**The external watchdog catches failures that internal watchdogs miss:** crystal oscillator failure, voltage brownout, silicon latch-up from ESD.

### Layer 9: Brake Following-Error Monitor — Actuator Feedback Verification

The SEB provides rich feedback via CAN `0x721 SEB_STATUS` at 100 Hz: `SEB_Stroke_Value` (actual stroke position, 0.05mm resolution), `SEB_Pressure_Value` (actual hydraulic pressure, 0.05 MPa resolution), and `SEB_Error_Status` (fault level 0-3 including "severe, request shutdown"). This data is already on the CAN bus — no new sensors needed.

SYS compares the commanded brake stroke/pressure (from `0x7B9`) against the actual feedback (from `0x721`):

| Check | Threshold | Debounce | Action |
|-------|-----------|----------|--------|
| Stroke following error | abs(cmd − actual) > 3mm | 100ms (10 frames) | Log fault, set diag flag — cannot escalate beyond ESTOP (already max brake) |
| SEB Level 3 fault | `SEB_Error_Status == 3` | Immediate | Log SEB severe fault — SEB itself reports it cannot function |
| 0x721 staleness | No frame for >100ms | — | SEB comms lost — brake system offline |

**Why this matters even though ESTOP is already the maximum response:** If `0x7B9 stroke=max` is transmitted but SEB never receives or acts on it, the vehicle coasts without brakes. The motor is killed (safe) but there's no active deceleration. The brake monitor detects this condition and records it — essential for post-incident analysis and system improvement. During non-ESTOP operation, the monitor catches brake degradation before it becomes critical.

---

## 3. ESTOP Steering — Two-Tier Response

The steering behavior during ESTOP depends on what triggered it:

### 3.1 Obstacle-Triggered ESTOP

If ESTOP is triggered by an obstacle detection (Jetson perception, RT obstacle sensor):
1. **Hold current angle, clamped to the dynamic angle limit** for the current speed. If the current angle exceeds the speed-safe envelope (e.g., cornering at 15° when the dynamic limit at this speed is 5°), ramp the angle down to the limit at 20°/s, then hold.
2. After **500ms** of hold at the clamped angle: **silent-stop** (stop transmitting `0x169`).
3. EPS-C enters internal comm-fault timeout.

**Why the dynamic clamp applies to the hold angle:** During emergency braking while cornering, lateral load transfer reduces rear wheel grip. Holding a large steering angle under hard braking on a three-wheeled vehicle approaches the rollover threshold (see physics model §8: `a_y = v²/L·tan(δ) > g·w/(2h)`). The dynamic angle clamp normally prevents this for commanded angles — it must also apply to the ESTOP hold angle. At speed, the clamp reduces the hold angle to near-straight (~5° at 25 km/h), which is still compatible with "don't veer into adjacent lanes." At low speed (<2 km/h), the full steering range is permitted.

**Straight-line case is unchanged:** If the vehicle is traveling straight (δ ≈ 0°) when the obstacle is detected, the hold angle is 0° — well within the dynamic clamp at all speeds. The behavior is identical to the previous design.

See [[emergency-safety-analysis]] §4 for the full causal trace of the cornering rollover scenario.

### 3.2 Non-Obstacle ESTOP

For all other triggers (button press, heartbeat loss, following error, command stale):
1. **Ramp to 0° at 20°/s** via active `0x169` transmission.
2. Continue transmitting during the ramp so EPS-C tracks the centering.
3. Hold at 0° once centered.
4. **The ramp is non-interruptible by START button.** See §1.2 — the mode transition to MANUAL defers until the ramp completes. This guarantees the vehicle is centered before the rider takes over.

**Rationale:** Straight wheels are the safest default position for a stopped vehicle.

### 3.3 Mechanical Jam Fallback

If the centering ramp encounters persistent following error (speed-scaled threshold for >1s during the ramp):
1. **Fall back to silent-stop** — linkage is likely mechanically jammed.
2. Stop transmitting `0x169`.
3. **Steer SM transitions to `STEER_FAULT`** explicitly.
4. EPS-C timeout-faults internally.

**Recovery:** After exiting ESTOP to MANUAL, the steer SM is in `STEER_FAULT`. The rider has two recovery options (same as EPS-C boot sync failure):
- **START short-press** → reset to `STEER_LISTEN_SYNC` (waits for `0x201`, syncs current angle).
- **START long-press (3s) + throttle at zero** → force-activate with target=0° (MANUAL mode only; AUTO locked out with blinking AUTO bulb).
- If neither works (EPS-C remains fault-locked): **power-cycle** as final recovery.

**Rationale:** Don't burn out the steering motor fighting a mechanical jam. Once the jam is cleared (manually by the rider), the standard STEER_FAULT recovery path restores steering without requiring a full power-cycle.

---

## 4. Other Emergency Conditions

Beyond ESTOP, the system handles several emergency scenarios with graduated responses.

### 4.1 Jetson Failure

| Symptom | Detection | Response |
|---------|-----------|----------|
| Planning stack crash | No `0x300` for >500ms (RT watchdog) | Zero `0x204` speed + stop `0x169` steering |
| ROS→CAN bridge crash | No `0x300` for >500ms | Same as above |
| Jetson kernel panic | Heartbeat `0x7FC` frozen >1500ms | RT zeros setpoints |
| Jetson sends dangerous steering | Dynamic angle clamp (Layer 6) | Capped to safe envelope |

**Response is controlled stop, not ESTOP.** The vehicle coasts to a stop (motor zero, steering stops). The rider maintains manual brake and steering control. MANUAL mode remains available.

### 4.2 RT ESP32-S3 Failure

| Symptom | Detection | Response |
|---------|-----------|----------|
| RT crashes / hangs | SYS heartbeat monitor: `0x7FD` frozen >1000ms | ESTOP (AUTO only) — full brake, motor kill |
| RT control loop dies | SYS `0x204` staleness >200ms | Zero speed + Neutral (faster than heartbeat) |
| RT CAN TX fails | TWAI TX errors | Log, auto-recover; persistent → EPS-C timeout-fault |
| External watchdog fires | TPS3850 on GPIO21 | Hardware reset → safe state → reboot → MANUAL |

**Two independent checks on SYS:**
1. `0x204` staleness at 200ms → zero speed (fast path, data quality).
2. `0x7FD` heartbeat at 1000ms → ESTOP (slow path, node liveness).

### 4.3 SYS ESP32-S3 Failure

| Symptom | Detection | Response |
|---------|-----------|----------|
| SYS crashes / hangs | RT heartbeat monitor: `0x7FE` frozen >1000ms | RT broadcasts CAN `0x001` ESTOP |
| SYS throttle DAC fails | MCP4725 I2C NACK | Throttle = 0V (I2C bus reset) |
| SYS gear relay fails | Relay coil open | Gear = N (relay de-energizes — fail-safe) |
| External watchdog fires | TPS3850 on GPIO47 | Hardware reset → safe state → reboot → MANUAL |

**Physical fallbacks during SYS failure:**
- Brake lever: SYS reads GPIO2 and transmits CAN 0x7B9 to SEB. This is a **by-wire** path — the SEB is electro-hydraulic with no mechanical linkage from lever to caliper. If SYS fails completely (CAN TX dead), the brake lever cannot actuate the SEB. However, SYS uses BRAKE_DEGRADED (not terminal FAULT) on boot sync failure, so the lever works as long as SYS can transmit CAN frames. If SYS is fully dead (watchdog reset in progress), see §4.7 Electrical Faults for the watchdog brake window.
- Steering still responds to handlebars (EPS-C standalone in MANUAL).
- ESTOP button still works on RT (CAN `0x001`) and MTR (direct GPIO).

### 4.4 MTR STM32 Failure

| Symptom | Detection | Response |
|---------|-----------|----------|
| MTR crashes / hangs | `0x206 MTR_MOTOR_FBK` stops, `0x120` stops | SYS detects mismatch between 0x204 setpoint and 0x206 feedback → CAN `0x001` ESTOP |
| MTR ESTOP GPIO triggered | Direct GPIO read | Local throttle cut + gear OFF, CAN `0x001` broadcast |
| MTR I2C bus fault | MCP4725 NACK | Motor controller sees 0V (DAC output defaults to 0) |

The MTR STM32 is the EGAS Level 1 Function Controller for motor actuation. SYS ESP32-S3 is the Level 2 Function Monitor — it compares commanded vs actual speed and triggers ESTOP on mismatch.

### 4.5 CAN Bus Failure

| Failure | Detection | Response |
|---------|-----------|----------|
| Low CAN bus-off | TWAI TEC > 255 | Log, auto-recover. ESTOP if persistent. |
| High CAN bus-off | MCP2515 error flags | Log, auto-recover. Zero setpoints until restored. |
| CAN frame corruption | steer-by-wire checksum fail | Frame silently discarded. Rolling counter NOT incremented. |
| CAN transceiver dead | No frames from that node | Heartbeat timeout → ESTOP or controlled stop. |

**Bus-off recovery:** The CAN controller automatically attempts to recover after 128 occurrences of 11 recessive bits (standard CAN spec). During bus-off, the node cannot transmit. If recovery fails, the heartbeat timeout on the peer node triggers.

### 4.6 Actuator Faults

**EPS-C (Steering):**
| Fault | Detection | Response |
|-------|-----------|----------|
| Motor failure | Following error >5° for >300ms | ESTOP |
| Encoder failure | `SES_INF_Angle_Status != 1` | STEER_FAULT, stop transmitting |
| Comm timeout | No `0x169` for >20ms | EPS-C internal fault lock |
| Boot sync timeout | No `0x201` within 5s | STEER_FAULT; rider retry via START short-press |

**SEB (Brake):**
| Fault | Detection | Response |
|-------|-----------|----------|
| Comm timeout | No `0x7B9` for >20ms | SEB internal fault lock |
| Boot sync timeout | No `0x721` within 2s | **BRAKE_DEGRADED** — transmits 0x7B9 with lever-based stroke defaults (0mm released, 15mm pressed) without sync. Recovers to BRAKE_ACTIVE when first valid 0x721 arrives. Brake lever remains functional at all times. |
| Checksum failure | SEB rejects frame | Frame dropped; counter still increments |
| Physical brake lever | Always available — GPIO2 → SYS → CAN 0x7B9. The brake is **by-wire** (electro-hydraulic SEB, no mechanical master cylinder). Lever availability depends on SYS continuing to transmit 0x7B9. BRAKE_DEGRADED guarantees transmission continues even without boot sync. |

### 4.7 Electrical Faults

| Fault | Protection | Response |
|-------|-----------|----------|
| 72V short to chassis | TLP281 galvanic isolation (4kV), TVS diodes, 1A fuses | Fuse blows (<1ms), TVS clamps transient |
| 72V on CAN bus | SN65HVD230 ±25V common-mode rating exceeded | Transceiver destruction. Both CAN buses are physically separate — a fault on one bus cannot propagate. |
| 12V rail short | DC-DC overcurrent protection + 12V relay cut | DC-DC shuts down. 12V relay opens. |
| Load dump (motor regen) | SMCJ90CA TVS on all 72V lines | Clamps to 146V |
| Reverse polarity | Series Schottky on ESP32 12V input | Blocks reverse current |
| ESD strike | TVS diodes + TLP281 isolation | Clamped to safe voltage |

### 4.8 Firmware Faults

| Fault | Detection | Response |
|-------|-----------|----------|
| Stack overflow | FreeRTOS stack canary | Task self-deletes, log error |
| Priority inversion | Preemptive scheduler + priority inheritance | Handled by FreeRTOS |
| Deadlock | External watchdog timeout | Hardware reset within 100ms |
| Infinite ISR | External watchdog (independent oscillator) | Hardware reset — internal WDT would also be stuck |
| Flash corruption | Bootloader checksum | Boot into safe recovery mode |

---

## 5. EGAS 3-Level Motor Safety Architecture

The separation of MTR (STM32) from SYS (ESP32-S3) follows the EGAS 3-level electronic throttle monitoring concept (ISO 26262 ASIL-C):

```
Level 3: Hardware — ESTOP button wired direct to both MTR and SYS
         TPS3850 external watchdog on each MCU. No software, no CAN.
         ESTOP press → MTR cuts throttle + gear instantly (local).

Level 2: Function Monitor — SYS ESP32-S3
         Monitors MTR via CAN: compares 0x204 setpoint vs 0x206 feedback.
         Mismatch > threshold → CAN 0x001 ESTOP.
         Also handles QM body functions (lights, DCDC, indicators).

Level 1: Function Controller — MTR STM32
         Normal actuation: reads sensors, drives MCP4725 DAC + gear relays.
         MANUAL: pass-through from grip/gear. AUTO: follows CAN 0x204.
         No wireless, no OS, minimal attack surface.
```

| Principle | Implementation |
|-----------|---------------|
| **Freedom from interference** | Separate MCUs — a SYS crash/hang cannot block motor kill |
| **ASIL decomposition** | MTR (QM level sensor reads) + SYS monitor (ASIL-B comparison) → combined ASIL-C |
| **Independent safe state path** | ESTOP button wired to both MCUs — MTR cuts throttle locally, zero CAN delay |
| **Diverse monitoring** | SYS compares commanded speed vs actual from 0x206 — mismatch → ESTOP |

---

## 6. Emergency Response Matrix

| Trigger | Speed | Gear | Brake | Steering | DCDC | 12V | Lights | Exit |
|---------|-------|------|-------|----------|------|-----|--------|------|
| **ESTOP button** | 0V | N | Max | Obstacle: hold (dyn-clamped)→silent. Non-obstacle: ramp→0°. | ON (MCU power) | OFF (accessories) | Brake ON | START btn or MODE long-press → MANUAL |
| **CAN 0x001** | 0V | N | Max | Same as button | ON | OFF | Brake ON | START btn or MODE long-press → MANUAL |
| **RT heartbeat lost** | 0V | N | Max | Same as button | ON | OFF | Brake ON | START btn or MODE long-press → MANUAL |
| **SYS heartbeat lost** | 0V (MTR kills locally) | N (MTR cuts locally) | Max (RT takes over 0x7B9) | EPS-C timeout* (RT still alive in AUTO) | ON (RT alive) | OFF | Brake ON | SYS reboot → re-sync; MANUAL mode |
| **Steering follow err** | 0V | N | Max | Obstacle: hold (dyn-clamped)→silent. Non-obstacle: ramp→0°. | ON | OFF | Brake ON | START btn or MODE long-press → MANUAL |
| **Jetson heartbeat lost** | 0 mm/s | N | Moderate brake (2 MPa via 0x205) | Stop 0x169 | ON | ON | Brake ON | Auto-recover on resume; MANUAL mode |
| **0x300 stale (500ms)** | 0 mm/s (coast) | N | Lever only | Stop 0x169 | ON | ON | Normal | Auto-recover on resume |
| **0x204 stale (200ms)** | 0 mm/s | N | Lever only | No change | ON | ON | Normal | Auto-recover on resume |
| **Watchdog reset** | 0V (MTR kills locally) | N (MTR cuts locally) | SEB timeout* | EPS-C timeout* | OFF | OFF | All OFF | Reboot → MANUAL |
| **CAN bus-off** | Depends | Depends | Depends | Depends | Depends | Depends | Depends | Auto-recover or ESTOP if persistent |

> \* **Watchdog reset brake window (critical):** When SYS resets, MTR maintains motor kill + gear neutral independently (separate MCU). However, SEB is commanded exclusively by SYS via CAN 0x7B9. After 20ms without a command frame, SEB enters internal comm-fault — behavior is **empirically unverified** (hold or release). If SEB holds pressure: ~20ms window, safe. If SEB releases: ~2.5s window until SYS reboots and brake LBS completes. **Must be tested before road operation.** A hardware brake-hold relay gated by the TPS3850 RST line is recommended regardless, to make brake behavior during MCU reset deterministic. See [[emergency-safety-analysis]] §3 for full analysis.

---

## 7. Rider's Guide to Emergency Situations

### 7.1 Understanding the dashboard

| Indicators | Meaning |
|------------|---------|
| AUTO bulb ON, MANUAL bulb OFF | AUTO mode — Jetson is driving |
| MANUAL bulb ON, AUTO bulb OFF | MANUAL mode — rider is driving |
| Both bulbs OFF + **brake light ON** | ESTOP — vehicle is emergency-stopped, full brake engaged. Brake light is powered from the always-on DC-DC rail and works even though the accessory relay is cut. |
| Everything OFF (brake light OFF, mode bulbs OFF) | Vehicle is **powered off** — normal state when key is off |
| AUTO bulb blinking 2 Hz | Degraded steering — MANUAL mode only, AUTO locked out |

**How to tell ESTOP from power-off:** During ESTOP, the brake light is **ON** (powered independently). When the vehicle is powered off, all lights including the brake light are OFF. This is the primary visual distinction.

### 7.2 If the vehicle enters ESTOP

1. **Stay seated and hold the handlebars.** The steering will either center itself or hold position.
2. **The brake will engage automatically** — the vehicle will decelerate.
3. **Do NOT press the MODE button** — it is ignored in ESTOP.
4. **To recover:** Press the **green START button** (GPIO41). The vehicle will enter MANUAL mode.
5. **Check surroundings** before releasing the brake lever and riding.
6. **If START button doesn't work:** Power-cycle the vehicle (key switch). This always starts in MANUAL mode.

### 7.3 If the vehicle behaves unexpectedly in AUTO

1. **Press the ESTOP button** (big red mushroom). This is always the fastest path to stop.
2. **Or squeeze the brake lever.** This engages the brake directly (GPIO2 → SYS → SEB).
3. **Or switch to MANUAL** via the MODE button (if system is still responsive).
4. The steering has multiple safety clamps — it cannot command dangerous angles even if Jetson malfunctions.

### 7.4 After a watchdog reset

If the system reboots unexpectedly (watchdog fired):
1. The vehicle starts in **MANUAL mode**.
2. Steering and brake will re-sync via Listen Before Speaking.
3. The reset reason is logged (`esp_reset_reason()`).
4. If resets recur, stop riding and investigate.

---

## 8. Testing the Emergency System

### 8.1 ESTOP Button Test

1. Vehicle stationary, MANUAL mode.
2. Press ESTOP button.
3. Verify: brake light ON, mode indicators OFF, throttle grip produces no response, gear selector produces no response.
4. Press START button → verify transition to MANUAL mode.
5. Verify: throttle and gear respond again.

### 8.2 CAN ESTOP Test

1. Vehicle stationary, on stands (wheels off ground).
2. Trigger CAN `0x001` from a CAN analyzer tool.
3. Verify same ESTOP behavior as physical button.

### 8.3 Heartbeat Timeout Test

1. Vehicle stationary, on stands, AUTO mode.
2. Disconnect SYS from CAN bus (unplug SN65HVD230).
3. Verify: within 1000ms, RT detects SYS heartbeat loss and broadcasts CAN `0x001`.
4. Verify: ESTOP behavior activates.
5. Reconnect and restart.

### 8.4 Steering Following Error Test

1. Vehicle stationary, on stands, AUTO mode.
2. Mechanically block the steering linkage.
3. Command a steering angle change via Jetson (or simulated CAN).
4. Verify: within 300ms of exceeding 5° error, ESTOP triggers.

### 8.5 Command Staleness Test

1. Vehicle stationary, on stands, AUTO mode.
2. Stop Jetson ROS 2 bridge (or disconnect high CAN).
3. Verify: within 500ms, RT zeros `0x204` speed and stops `0x169`.
4. Verify: SYS detects `0x204` staleness within 200ms, forces speed=0 and gear=N.

### 8.6 External Watchdog Test

1. Comment out the WDT toggle in `safety_task` (SYS) or `control_task` (RT).
2. Flash and run.
3. Verify: MCU resets within ~100ms.
4. Verify: during reset window, motor controller sees 0V throttle.
5. Restore toggle and re-flash.

### 8.7 Dynamic Angle Clamp Test

1. Vehicle stationary, on stands, AUTO mode.
2. Send a CAN `0x300` command with speed=25 km/h and yaw_rate requesting 40° steering.
3. Verify: RT clamps steering to ~5° (not 40°).
4. Verify: `0x169` transmitted angle is ≤5°.

---

## 9. Design Principles

1. **ESTOP bypasses queues.** The safety task preempts and writes directly to actuators — no queue delay.
2. **ESTOP is an absorbing state.** Once entered, only deliberate human action (START button or power-cycle) exits.
3. **ESTOP exit goes to MANUAL, never AUTO.** The rider resumes direct control after any emergency.
4. **NC wiring for all safety inputs.** Cut wires and disconnected plugs read as ESTOP, not "everything fine."
5. **Threshold + duration for all fault checks.** Single-sample glitches don't trigger ESTOP — faults must persist.
6. **Fail-safe by default.** De-energized relays = Neutral gear. Zero throttle voltage = motor stop. NC ESTOP button = pressed when disconnected.
7. **Fail-visible where possible.** Brake light always illuminates when any braking source is active. Both mode bulbs OFF = ESTOP.
8. **Independent safety layers.** Hardware (ESTOP button, watchdog IC) → CAN (0x001, heartbeats) → software (staleness, following error, clamps). No single failure defeats them all.

---

## 10. Related Documents

| Document | Content |
|----------|---------|
| [[defense-in-depth-safety]] | Detailed breakdown of each safety layer |
| [[external-watchdog]] | TPS3850 watchdog IC — timeout, safe state, testing |
| [[high-voltage-isolation]] | 72V galvanic isolation — TLP281, relays, fuses, TVS |
| [[distributed-architecture]] | Three-node rationale — Jetson/RT/SYS/MTR split |
| [[listen-before-speaking]] | CAN actuator safe bootstrapping after ESTOP/watchdog reset |
| [[steer-by-wire-security-protocol]] | Rolling counter + XOR checksum for steer-by-wire actuators |
| [[wiring]] | Pin-to-pin wiring including ESTOP button, brake lever, START button |
| [architecture.md](../architecture.md) §3 | Mode state machine (MANUAL → AUTO → ESTOP) |
| [architecture.md](../architecture.md) §6.1 | EGAS 3-level motor safety architecture |
| [architecture.md](../architecture.md) §7.6 | Steering safety mechanisms and ESTOP behavior |
| [architecture.md](../architecture.md) §8.6 | Brake control, heartbeats, external watchdog, physical controls |
| [can-dictionary.md](../can-dictionary.md) §0x001 | SAFETY_ESTOP frame definition |

---

*See also: [[defense-in-depth-safety]] for layered safety approach, [[external-watchdog]] for hardware watchdog, [[high-voltage-isolation]] for electrical fault protection, `architecture.md` §6 for EGAS 3-level safety, §3 for mode state machine.*
