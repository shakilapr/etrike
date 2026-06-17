# Distributed Safety Patterns

Safety-critical embedded systems don't rely on a single mechanism to prevent hazards. They stack multiple independent layers so that if one fails, the next catches it. This is **defense in depth** — a pattern borrowed from nuclear, aviation, and automotive safety engineering.

The E-Trike has 8 independent safety layers (§6 of architecture.md). Each layer assumes the one above it might fail.

---

## 1. Defense in Depth — The Core Principle

```
┌──────────────────────────────────────┐
│  Layer 1: Physical ESTOP button      │  ← fastest, simplest, hardest to defeat
├──────────────────────────────────────┤
│  Layer 2: CAN 0x001 SAFETY_ESTOP     │  ← redundant comm path for ESTOP
├──────────────────────────────────────┤
│  Layer 3: Heartbeat timeout          │  ← detects crashed/frozen nodes
├──────────────────────────────────────┤
│  Layer 4: Command staleness          │  ← detects hung planning stack
├──────────────────────────────────────┤
│  Layer 5: Steering following error   │  ← detects stuck linkage / bad actuator
├──────────────────────────────────────┤
│  Layer 6: Dynamic steering clamp     │  ← prevents dangerous commands
├──────────────────────────────────────┤
│  Layer 7: Software hard-stops        │  ← prevents mechanical damage
├──────────────────────────────────────┤
│  Layer 8: Brake light OR logic       │  ← fail-visible: always shows braking
└──────────────────────────────────────┘
```

**The rule for layers:** each layer must be *independent* — a failure that defeats one layer must not defeat others. Independence comes from:
- Different hardware (GPIO vs CAN vs watchdog IC)
- Different software (separate tasks, separate nodes)
- Different physical principles (electrical vs optical vs mechanical)

---

## 2. ESTOP as an Absorbing State

An emergency stop isn't just "stop the motor." It must guarantee:

1. **All actuators return to safe state.** Motor → 0V. Gear → neutral. Brake → max. Steering → stop commanding (actuator timeout-faults). DC-DC → off. 12V relay → off.
2. **The state persists.** No automatic recovery. No "ESTOP cleared, resuming." The system stays stopped until a deliberate human action.
3. **The exit path is explicit and guarded.** The E-Trike requires pressing the START button (GPIO32) — not the MODE button, not a CAN command, not a timeout. And ESTOP exits to MANUAL (rider in direct control), never to AUTO.

```
ESTOP exit rule:
  START button → MANUAL mode
  (NOT: MODE button, CAN command, power-cycle timeout)
```

**Why this matters:** If ESTOP could be cleared by the same buggy software that triggered it, you'd have an oscillating system — ESTOP → clear → ESTOP → clear — with the vehicle lurching between stop and go.

---

## 3. Fail-Safe vs Fail-Operational

| Strategy | Behavior on failure | Example |
|----------|-------------------|---------|
| **Fail-safe** | System enters safe state, stops functioning | ESTOP: motor off, brake on, 12V off |
| **Fail-operational** | System continues with degraded capability | Jetson fails → RT zeros setpoints → vehicle coasts (motor off but steering still responds to rider) |

The E-Trike uses **fail-safe** for the safety-critical path (ESTOP kills everything) but **fail-operational** for non-critical failures (Jetson loss = coast, not ESTOP). The distinction matters: you don't want to ESTOP on every Jetson hiccup, but you do want to ESTOP on steering fault.

---

## 4. Normally-Closed (NC) Wiring

A **normally-closed** switch conducts current in its resting state. Pressing it *breaks* the circuit.

```
Normal operation:  GPIO reads HIGH (pull-up holds it) → system runs
Button pressed:    GPIO reads LOW (switch connects to GND) → ESTOP
Wire cut/broken:   GPIO reads LOW (pull-up defeated, floating→LOW) → ESTOP
Power loss:        GPIO reads LOW → ESTOP
```

An NC switch is **fail-safe by construction.** A broken wire, disconnected plug, or power loss all trigger ESTOP — they can't produce a "silent failure" where the button is broken but the system thinks it's fine.

The alternative — normally-open (NO) — is cheaper but unsafe for ESTOP: a cut wire looks exactly like "button not pressed."

---

## 5. Threshold + Duration Debounce

Safety checks don't trigger on a single out-of-bounds sample. They wait for the condition to *persist*.

```
if (abs(cmd - actual) > THRESHOLD) {
    error_duration_ms += dt;
    if (error_duration_ms > DURATION) {
        mode_set(Estop);  // confirmed persistent fault
    }
} else {
    error_duration_ms = 0;  // transient — reset counter
}
```

**Why duration matters:** CAN frames can be delayed by arbitration (a low-priority message waits for a high-priority one to finish). A single missed or delayed frame is a transient, not a failure. The duration confirms "this is real, not noise."

| Check | Threshold | Duration | Why this duration? |
|-------|-----------|----------|-------------------|
| Steering following error | >5° | 300 ms | Stuck linkage is immediately dangerous. Short debounce. |
| Command staleness | No `0x300` | 500 ms | Jetson hiccups are common. Longer tolerance. |
| Heartbeat timeout | No fresh counter | 200 ms (inter-MCU) | FTTI-bound. Must catch within 1.4 m of travel. |

---

## 6. Independent Safety Layers — Node Separation

A single MCU running all safety + actuation code can fail entirely from one bug (stack overflow, wild pointer, priority inversion). Physical separation means:

```
SYS ESP32-S3 (safety, motor, brake)          RT ESP32-S3 (physics, steering, gateway)
         │                                             │
         │  0x7FD/0x7FE heartbeats                     │
         │◄────────────────────────────────────────────►│
         │                                             │
         │  If RT crashes: SYS detects HB timeout       │
         │  → triggers CAN 0x001 ESTOP                  │
         │  → motor stops, brake engages                │
         │  → SYS continues running independently       │
```

SYS doesn't need RT to be alive to stop the vehicle. The ESTOP button, brake lever, and motor kill all work directly on SYS GPIOs. This is the architectural decoupling that makes defense in depth real — not just multiple `if` statements on the same chip.

---

## 7. Fail-Visible: The Brake Light OR Logic

Some safety states must be **visible** to the outside world, not just enforced internally:

```
brake_light_on = brake_lever_pressed()     // physical lever — local GPIO
              OR (mode == Estop)           // ESTOP — always brake
              OR g_light_state.brake;      // Jetson CAN 0x302 — predictive
```

All sources are OR'd together. The brake light illuminates if ANY braking reason exists. There's no code path that can produce "braking but light off." This is **fail-visible**: the safety state is externally observable, so a following vehicle or rider can see it even if other systems have failed.

---

## 8. Safety Pattern Checklist

When designing a safety-critical embedded system, verify:

- [ ] Every safety function has at least two independent implementations.
- [ ] No single point of failure defeats all safety layers.
- [ ] ESTOP is an absorbing state — deliberate exit only.
- [ ] Safety-critical inputs are NC (fail to safe state on disconnect).
- [ ] Threshold checks include a persistence duration (debounce).
- [ ] Safety-critical outputs are fail-visible where possible (lights, indicators).
- [ ] Nodes that must catch each other's failures exchange independent heartbeats.
- [ ] The external watchdog is on a different chip from the MCU.

---

*Primary reference: `docs/emergency-system.md` for the complete ESTOP system, all trigger paths, emergency response matrix, rider's guide, and testing procedures.*

*See also: [[heartbeat-monitoring]] for liveness detection, [[external-watchdog]] for hardware watchdog, [[state-machine-design]] for ESTOP FSM, `architecture.md` §6 for the E-Trike's safety layers.*
