# Defense-in-Depth Safety Patterns

The E-Trike safety architecture doesn't rely on any single mechanism. Multiple independent layers check for faults — if one misses, the next catches it. This is **defense in depth**: overlapping, independent safety barriers so that no single-point failure causes a hazardous event.

---

## Layer 1: Physical ESTOP button

A physical NC (normally-closed) pushbutton wired to SYS ESP32-S3 GPIO1.

- Pressed → GPIO LOW → `safety_task` (Core 0, priority 5) detects it within one scheduler tick (1 ms) → `mode_set(Estop)`.
- The button is wired fail-safe: a broken wire or disconnected plug reads as LOW = ESTOP.
- This is the **fastest** path to ESTOP and works regardless of CAN bus state or firmware health (as long as the safety task runs).

**Checked in:** SYS `safety_task` (direct GPIO read, 20 Hz poll).

---

## Layer 2: CAN `0x001` SAFETY_ESTOP

Any node (Jetson, RT, SYS) can broadcast `0x001` on either CAN bus.

- An empty frame (DLC=0) with the highest-priority CAN ID (`0x001` wins every arbitration).
- RT forwards it between buses (transparently, bypassing the gateway queue).
- Each node's dispatch task processes it immediately.

**Why Jetson might trigger ESTOP:** Perception stack detects an imminent collision that the obstacle sensor missed. **Why RT might trigger:** Steering following error exceeds threshold. **Why SYS might trigger:** Physical ESTOP button (redundant CAN path).

**Checked in:** Every node's CAN dispatch task.

---

## Layer 3: Heartbeat timeout

Each node sends its own heartbeat (0x7FC/0x7FD/0x7FE) at 2 Hz on its bus. The safety monitor checks:

- On RT: if Jetson heartbeat (`0x7FC` on high CAN) stops for >1500 ms → ESTOP in AUTO.
- On SYS: if RT heartbeat (`0x7FD` on low CAN) stops for >1500 ms → ESTOP in AUTO.

A missing heartbeat means the controller node has crashed, rebooted, or lost CAN connectivity. The vehicle must stop.

**Checked in:** RT watchdog task, SYS safety task.

---

## Layer 4: Command staleness (watchdog)

RT's watchdog task runs at 10 Hz and checks the timestamp of the last received `0x300 HOST_DRIVE_CMD`:

```
if (time_since_last_drive_cmd > 500 ms) {
    zero 0x204 RT_DRIVE_CMD       →  MTR drives motor at 0 V
    stop 0x169 VCU_SES_REQ        →  EPS-C timeout-faults
}
```

If Jetson's planning stack hangs or the ROS→CAN bridge crashes, the vehicle coasts to a stop (motor zero) and steering goes to its comm-fault safe state. This is a **passive safety** response — no active braking, but acceleration stops and steering becomes inert.

**Checked in:** RT `watchdog` task (10 Hz).

---

## Layer 5: Steering following error

While in AUTO mode, RT compares the commanded steering angle (`0x169`) against the actual angle reported by EPS-C (`0x201 SES_StrAngle`):

```
error = abs(cmd_angle_mdeg - actual_angle_mdeg);
if (error > 5000_mdeg && error_duration_ms > 300) {
    mode_set(Estop);
}
```

This catches:
- **Mechanical faults:** stuck linkage, rock jam, bent tie rod.
- **Actuator faults:** EPS-C motor failure, encoder failure, internal control loop fault.
- **CAN faults:** corrupted `0x169` frames (caught because checksum failure → EPS-C rejects → angle doesn't move).

The 300 ms persistence requirement prevents false triggers from momentary CAN glitches or sensor noise.

**Checked in:** RT `control` task (100 Hz), using `SES_StrAngle` from `0x201`.

---

## Layer 6: Dynamic angle clamp (speed-dependent steering limit)

The maximum allowable steering angle is inversely proportional to vehicle speed:

| Speed | Max angle | Rationale |
|-------|-----------|-----------|
| 2 km/h | ~40° | Low-speed maneuvering needs full range |
| 10 km/h | ~18° | Moderate cornering |
| 25 km/h | ~5° | High speed — tight turns cause rollover |

The clamp is applied in RT's `physics_resolve()` function, **after** the kinematic solve but **before** the steering angle is sent to EPS-C. This means even if Jetson commands a dangerous turn (bug, bad planning, or attack), RT clamps it to the safe envelope.

The formula:

```
max_angle = lerp(kSteerMaxAngleAtHighSpeed, kSteerMaxAngleAtLowSpeed,
                 clamp((speed - kLowSpeedThresh) / (kHighSpeedThresh - kLowSpeedThresh), 0, 1))
```

This is distinct from the physics model's rollover threshold (§8 of physics-model.md). The rollover threshold is a *warning boundary* for the planner. The dynamic clamp is an *enforced hard limit* applied at the actuator level.

**Checked in:** RT `control` task (100 Hz).

---

## Layer 7: Software hard-stops

EPS-C mechanically accepts ±78° (its internal limit). The physical steering rack on the tricycle has end-stops at approximately ±40°. If the firmware commands beyond ±40°, the motor pushes against an immovable mechanical stop — this can strip gears, burn out the motor, or bend the linkage.

RT clamps ALL commanded angles to ±40° in firmware, regardless of source:

```
cmd_angle = clamp(cmd_angle, -40000_mdeg, +40000_mdeg);
```

This is the innermost safety layer for steering — it runs on every control cycle, before any other clamp. Even if dynamic angle clamp misbehaves, hard-stops prevent mechanical damage.

**Checked in:** RT `control` task (100 Hz).

---

## Layer 8: Brake light OR logic

The brake light illuminates if ANY braking source is active:

```
brake_light_on = brake_lever_pressed()      // GPIO2 — physical lever
              OR (mode == Estop)            // ESTOP — full brake
              OR g_light_state.brake_light; // Jetson CAN 0x302 — predictive/hazard
```

All sources are **local to SYS** — no CAN round-trip needed for the physical lever or ESTOP. The Jetson CAN bit is a *supplemental* trigger (predictive illumination) but can never be the *only* trigger. The physical braking state always wins.

This is a **fail-visible** pattern: if any brake reason exists, the light is on. There's no way to be braking without the light illuminating.

**Checked in:** SYS `lights` task (20 Hz).

---

## The threshold + duration pattern

Several safety checks use the same pattern:

| Check | Threshold | Duration | Action |
|-------|-----------|----------|--------|
| Steering following error | >5° | >300 ms | ESTOP |
| Command staleness | no `0x300` | >500 ms | Zero outputs |
| Heartbeat timeout | no peer heartbeat | >1500 ms | ESTOP in AUTO |

**Why duration matters:** CAN frames can be delayed by arbitration (low-priority messages wait for high-priority ones) or by bus errors. A single missed frame should not trigger ESTOP. The duration acts as a debounce — it confirms the condition is persistent, not transient.

**Why different durations:** Following error (300 ms) is the fastest — a stuck linkage is immediately dangerous. Command staleness (500 ms) allows for brief Jetson hiccups. Heartbeat timeout (1500 ms) is conservative — a node crash takes longer to confirm than a CAN glitch.

---

## ESTOP as the convergence point

All safety layers ultimately call `mode_set(Estop)`. The ESTOP handler:

1. Sets `g_mode = Estop` (atomic, visible to all tasks).
2. Motor task: MCP4725 → 0 V, all gear relays → OFF.
3. Brake task: `0x7B9` stroke = max (full brake, ~27 mm).
4. Steering: `0x169` stops transmitting → EPS-C timeout-faults.
5. DC-DC: `0x012` enable = 0 → 12V rail off.
6. Lights: brake ON, all others OFF.
7. Exit requires power-cycle (cannot leave ESTOP via mode switch).

ESTOP is an **absorbing state** — once entered, the only way out is a full power-cycle. This prevents accidental re-engagement while a fault condition persists.

---

## What defense-in-depth protects against

| Failure mode | Caught by layer |
|-------------|-----------------|
| Jetson crashes | Layer 4 (staleness) → zero outputs |
| Jetson sends dangerous steering | Layer 6 (dynamic clamp) → capped to safe envelope |
| Steering linkage jams | Layer 5 (following error) → ESTOP |
| EPS-C motor fails | Layer 5 (following error) → ESTOP |
| CAN bus dies | Layer 3 (heartbeat) + Layer 4 (staleness) |
| Rider hits ESTOP button | Layer 1 (GPIO) + Layer 2 (CAN redundancy) |
| Firmware hangs | External watchdog IC (see [[external-watchdog]]) |
| 72V shorts to chassis | [[high-voltage-isolation]] |

---

*Primary reference: [[emergency-system]] for the complete ESTOP system, trigger paths, emergency response matrix, rider's guide, and testing procedures.*

*See also: [[listen-before-speaking]] for actuator boot safety, [[external-watchdog]] for hardware watchdog, [[high-voltage-isolation]] for galvanic isolation, [[physics-model]] §8 for rollover threshold, [[architecture]] §7.6 for safety mechanisms, §3 for mode state machine.*
