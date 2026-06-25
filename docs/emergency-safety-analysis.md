# Emergency System Safety Analysis — Three Critical Issues

This document analyzes three safety issues discovered during review of the ESTOP and emergency handling design. Each issue is traced through the architecture with its causal chain, quantified for risk, and assigned a recommended fix.

---

## Issue 1: ESTOP Exit Race Condition — Steering Ramp Interrupted by START Button

### 1.1 The scenario

1. Non-obstacle ESTOP is triggered (button press, heartbeat loss, following error, CAN `0x001`).
2. RT steer state machine begins centering ramp: transmits `0x169 VCU_SES_REQ` at 50 Hz with decreasing angle targets at 20°/s.
3. **Rider presses START button before the ramp completes.**
4. SYS `mode_task` detects GPIO32 falling edge → `mode_set(Manual)`.
5. SYS transmits CAN `0x110 SYS_MODE_CMD` with `mode=0` (MANUAL).
6. RT `dispatch` receives `0x110` → calls `mode_set(Manual)`.
7. RT steer SM: **MANUAL mode → RT does NOT send `0x169`.** Per architecture §7.6 mode behavior table: "MANUAL: RT does NOT send 0x169. EPS-C standalone."
8. `0x169` transmission stops **immediately** at whatever angle the ramp had reached.
9. EPS-C: 20 ms without `0x169` → internal comm-fault timeout.
10. EPS-C comm-fault behavior: unit-specific and **unknown**. May lock at current angle, go limp, or freewheel.

### 1.2 Causal trace

```
START btn press
  │
  ├── SYS mode_task @ 10 Hz
  │     └── g_mode = Manual
  │     └── CAN 0x110 {mode=MANUAL}
  │
  ├── RT dispatch receives 0x110
  │     └── mode_set(Manual)
  │           └── steer SM: ESTOP+STEER_ACTIVE → MANUAL
  │                 └── Stop transmitting 0x169   ← RACE: ramp may be mid-flight
  │
  └── EPS-C: 0x169 stops
        └── T+20ms: comm-fault timeout
              └── Behavior: UNKNOWN (lock / limp / freewheel at current angle)
```

**Key architectural detail:** The START button's `mode_set(Manual)` is unconditional. There is no check on whether the steering ramp is complete. The mode transition is instant. The steer SM obediently stops transmitting `0x169` per MANUAL-mode rules — it has no awareness that a ramp was in progress.

### 1.3 Quantified risk

| Parameter | Value |
|-----------|-------|
| Worst-case starting angle | ±40° (software hard-stop limit) |
| Ramp rate | 20°/s |
| Maximum ramp duration | 2.0 seconds (40° ÷ 20°/s) |
| Window of vulnerability | 0 to 2.0 seconds after ESTOP entry |
| Angle if interrupted at t=0.5s | 40° − (0.5 × 20) = **30° off-center** |
| Angle if interrupted at t=1.0s | **20° off-center** |
| EPS-C behavior after 0x169 stops | Unknown — may **lock at that angle** or go limp |

At 30° steering lock in MANUAL mode, the vehicle cannot be ridden straight. The rider must either:
- Power-cycle and hope EPS-C releases on power-loss (unknown behavior).
- Wrestle the locked steering mechanically (risk of damaging EPS-C gear train).
- Press ESTOP again (which won't help — already in a bad state).

**Risk assessment:** High severity, medium probability. The START button is the only ESTOP exit path — a rider who entered ESTOP on a curved road will naturally want to exit as soon as they feel safe, likely before the 2-second ramp completes if they're not aware of the ramp duration. Combined with unknown EPS-C timeout behavior, this creates a non-deterministic safety outcome.

### 1.4 Why it wasn't caught earlier

The two-tier ESTOP steering design (gap #3 resolution in architecture §12) focused on *entering* ESTOP correctly — hold vs ramp based on trigger — but never considered the *exit* transition. The ramp was designed as an ESTOP-mode behavior, and the MANUAL-mode behavior ("RT does NOT send 0x169") was designed for normal MANUAL operation. The boundary between them — ESTOP→MANUAL transition mid-ramp — fell through the gap.

### 1.5 Recommended fix: Deferred mode transition with ramp completion

**Approach:** RT defers the ESTOP→MANUAL steering mode transition until the centering ramp completes. No new CAN messages needed — this is a local change to RT's steer state machine.

**Implementation:**

In RT's `dispatch` task, when processing `0x110 SYS_MODE_CMD` with `mode=MANUAL` and the current mode is ESTOP:

```cpp
// In dispatch task, on receiving 0x110 mode=MANUAL while in ESTOP:
if (g_steer_ramp_in_progress) {
    // Defer the mode transition for steering purposes.
    // SYS has already transitioned to MANUAL (brake, motor, lights all MANUAL).
    // RT continues the steering centering ramp.
    // Once ramp completes, steer SM transitions to MANUAL (stops 0x169).
    g_steer_deferred_exit_to_manual = true;
    // Do NOT call mode_set(Manual) yet — keep RT in ESTOP mode locally.
    // All other subsystems (motor setpoint, brake) follow SYS immediately.
} else {
    mode_set(Manual);  // ramp already complete — safe to transition
}
```

In the steer state machine's `STEER_ACTIVE` state:

```cpp
case STEER_ACTIVE:
    if (g_mode == Mode::Estop && g_steer_ramp_in_progress) {
        // Continue centering ramp
        ramp_step();
        send_0x169(ramp_target);
        if (ramp_complete()) {
            g_steer_ramp_in_progress = false;
            if (g_steer_deferred_exit_to_manual) {
                // Ramp completed — safe to go silent
                stop_0x169();
                steer_state = STEER_IDLE_MANUAL;  // or equivalent
            }
        }
    } else if (g_mode == Mode::Manual) {
        // Normal MANUAL: silent
        stop_0x169();
    }
    break;
```

**Key properties of this fix:**
- No new CAN messages. No protocol change.
- No mode mismatch between SYS and RT: SYS transitions to MANUAL immediately (brake lever responsive, motor pass-through, lights MANUAL behavior). RT defers only the *steering* aspect.
- The brake transitions from ESTOP (max stroke) to MANUAL (lever-based) immediately on START — **this is correct** because the rider pressed START specifically to regain control.
- The steering ramp continues uninterrupted. Once centered, RT goes silent per MANUAL rules.
- If the ramp cannot complete (mechanical jam → following error persists), the existing fallback applies: silent-stop after >5° error for >1s. The START button press doesn't change this.
- Timeout: if the ramp doesn't complete within 5 seconds (e.g., EPS-C not responding), RT transitions to STEER_FAULT. SYS is already in MANUAL, so rider has motor and brake control but no steering assist.

**Edge case — rider pressing START during obstacle-triggered ESTOP:** In obstacle-triggered ESTOP, the steering holds current angle then silent-stops after 500ms. During the hold phase, `g_steer_ramp_in_progress = false` (it's a hold, not a ramp). So START button exits immediately — which is correct: the steering is already at a fixed angle, and EPS-C will timeout-fault on 0x169 stop as it would in any MANUAL transition. During the silent-stop phase (after 500ms), there's no 0x169 transmission at all — START exits normally.

---

## Issue 2: SEB Boot Sync Timeout Contradiction — "Brake Lever Always Works" is False

### 2.1 The contradiction

Three statements in the architecture contradict each other:

**Statement A** (architecture §8.10, error handling table):
> SEB sync timeout: No 0x721 within 2s of boot → `BRAKE_FAULT`, lever inop

**Statement B** (architecture §4.3, SYS failure section):
> Physical fallbacks during SYS failure: Brake lever (GPIO2) still works — physical hydraulic brake independent of CAN.

**Statement C** (architecture §8.6, BRAKE_FAULT state):
> BRAKE_FAULT: Stop transmitting

Statements B and C cannot both be true. If BRAKE_FAULT means "stop transmitting 0x7B9", then the brake lever — which is just a GPIO2 input switch read by SYS firmware — cannot result in braking because the command never reaches SEB.

Statement B is **factually incorrect**. The E-Trike has a **brake-by-wire** system: the brake lever is an electrical switch, not a hydraulic master cylinder. The actuation path is always:

```
Lever (GPIO2) → SYS firmware → CAN 0x7B9 → SEB → hydraulic pressure to calipers
```

There is no mechanical or hydraulic bypass. "Independent of CAN" is false — the brake always depends on CAN 0x7B9 reaching SEB.

### 2.2 How SEB boot sync failure occurs

The Listen Before Speaking (LBS) sequence for the brake requires:

1. **BRAKE_BOOT_WAIT** (500ms): Power-on delay. Do not transmit.
2. **BRAKE_LISTEN_SYNC**: Wait for `0x721 SEB_STATUS`. Extract current stroke. Wait for alignment.
3. **BRAKE_ACTIVE**: Transmit `0x7B9` at 50 Hz continuously.

If step 2 times out (no `0x721` within 2 seconds), the SM transitions to BRAKE_FAULT and **stops transmitting permanently**. There is no recovery path defined.

This can happen from:
- SEB not powered (separate power supply issue)
- SEB CAN interface not initialized yet (boots slower than SYS)
- CAN bus wiring fault between SYS and SEB
- SEB internal fault preventing status transmission

### 2.3 Causal trace — worst case

```
Power-on
  │
  ├── SYS boots in MANUAL mode (safe default)
  ├── brake_init(): BRAKE_BOOT_WAIT (500ms) → BRAKE_LISTEN_SYNC
  ├── SEB is powered but slower to boot (internal self-test)
  │
  ├── T+2500ms (500ms wait + 2000ms listen): no 0x721 received
  │     └── BRAKE_FAULT
  │           └── Stop transmitting 0x7B9
  │
  ├── Rider squeezes brake lever
  │     └── GPIO2 reads LOW
  │     └── brake_task: state == BRAKE_FAULT → does NOT transmit 0x7B9
  │     └── SEB receives no command
  │     └── **No braking occurs**
  │
  └── Rider has NO brake. Vehicle is in MANUAL mode with throttle and steering functional.
      The only way to stop: ESTOP button (but ESTOP also sends brake via 0x7B9 — same dead path).
```

**Wait — does ESTOP also fail?** Yes. In ESTOP, SYS sends `0x7B9` with stroke=max from the `brake_task`. But if the brake SM is in BRAKE_FAULT, it's not transmitting. The ESTOP brake command also goes through the same dead CAN path.

However, the ESTOP button also:
- Kills motor (MCP4725 = 0V via `motor_task` — different task, still works)
- Kills gear (relays OFF — different task, still works)
- Turns off DC-DC (CAN 0x012 via `dcdc_task` — different task, still works)

So ESTOP stops the motor but doesn't engage the brake in this scenario. The vehicle coasts to a stop rather than braking actively.

### 2.4 Quantified risk

| Parameter | Value |
|-----------|-------|
| Trigger condition | SEB CAN status not received within 2s of boot |
| Probability | Low (requires SEB power/CAN fault) but non-zero — any CAN wiring issue, SEB slow boot, or SEB internal fault |
| Consequence | **Complete brake loss** — lever inoperative, ESTOP braking inoperative |
| Mitigation in current design | None. BRAKE_FAULT is terminal with no recovery. |
| Vehicle state | MANUAL mode, motor and steering functional, no brake |
| Stop distance without brake | At 25 km/h on flat ground, coast-to-stop is >50m |

This is a **single-point failure** that takes out the brake: one CAN ID not arriving at boot time disables the entire braking system for the remainder of the power cycle.

### 2.5 Why the architecture makes this mistake

The brake LBS was modeled on the steering LBS, where FAULT is a recoverable state because EPS-C goes **standalone** in MANUAL mode — the steering wheel mechanically turns the rack regardless of CAN. The brake has no equivalent "standalone" mode. SEB requires continuous CAN commands to maintain any brake pressure. The LBS pattern doesn't transfer cleanly to a by-wire actuator with no mechanical fallback.

The statement "physical brake lever independent of CAN" appears to assume a traditional hydraulic brake where the lever directly pushes fluid. But the SEB is electro-hydraulic — the lever is just a switch input to the MCU. This is a domain confusion between traditional and by-wire braking.

### 2.6 Recommended fix: Degraded transmit mode instead of terminal FAULT

**Approach:** Replace BRAKE_FAULT (terminal stop) with BRAKE_DEGRADED (transmit with safe defaults despite no sync). The brake is too critical to go silent.

**Implementation:**

Replace the BRAKE_FAULT state with BRAKE_DEGRADED:

```
BRAKE_DEGRADED:
  - Transmit 0x7B9 at 50 Hz with conservative defaults
  - Stroke = 0 (no brake) when lever released
  - Stroke = kBrakeManualStroke (~15 mm) when lever pressed (GPIO2 LOW)
  - Rolling counter still increments
  - Checksum still computed
  - CAN 0x600 SYS_DIAG_RPT includes brake_degraded flag
  - Recovery: when first valid 0x721 arrives, sync current stroke
    and transition to BRAKE_ACTIVE
```

This means:
- The brake lever **always works** — even without boot sync. The statement "brake lever always works" becomes true.
- The only difference vs normal operation: without sync, SYS doesn't know the exact current stroke, so it commands absolute positions rather than relative changes.
- SEB may reject some frames if the initial stroke value doesn't match its internal state. But a frame with valid checksum + rolling counter at a plausible stroke value (0–27 mm) is likely to be accepted.
- Once the first `0x721` arrives, normal synced operation resumes.

**Why this is safe without sync:**
- Stroke=0mm (released) and stroke=15mm (pressed) are always within SEB's valid range (0–27mm).
- The SEB's internal PID will move to the commanded stroke regardless of starting position.
- The rolling counter increments → SEB sees liveness.
- The checksum is correct → SEB accepts the frame.
- Worst case: SEB was at 27mm and receives stroke=0 → brake releases. This is the correct MANUAL behavior (lever released = no brake). SEB was at 0mm and receives stroke=15mm → brake engages. Also correct.

**Alternative consideration — hardware fix:** If SYNTREE's protocol requires exact sync (rejects frames when stroke doesn't match internal state), a hardware solution is needed: wire the brake lever as a **redundant direct input to SEB** (if SEB has a discrete brake input pin). This bypasses the MCU entirely. But this depends on SEB hardware capability and adds wiring complexity.

---

## Issue 3: Watchdog Reset Unbraked Window — No Deterministic Brake During MCU Reset

### 3.1 The scenario

1. SYS ESP32-S3 hangs (stack overflow, deadlock, crystal failure, latch-up).
2. `safety_task` stops toggling GPIO23.
3. **T+100ms:** TPS3850 external watchdog IC asserts RST → ESP32 EN pin pulled LOW.
4. **All SYS GPIOs** go to reset state (high-impedance input).
5. Motor controller: MCP4725 loses I2C communication → output defaults to 0V (or last held value, depending on MCP4725 configuration). **Likely 0V** (MCP4725 has power-on reset to 0V, and I2C loss may not trigger a reset — it holds last value until VCC drops).
6. Gear relays: GPIOs float → NPN transistor base goes LOW → relay coil de-energizes → contacts open → all gear lines = Neutral. **Confirmed safe.**
7. **T+120ms:** SEB has received no `0x7B9` for 20ms → internal comm-fault timeout.
8. SEB comm-fault behavior: **Unknown / TBD.** Options:
   - **Hold** last commanded pressure/stroke → brake maintained. **Safe.**
   - **Release** pressure → brake lost. **Unsafe.**
   - **Ramp release** over N seconds → partial brake during window. **Partially safe.**
9. **T+~200ms:** ESP32 bootloader starts. SYS begins firmware init.
10. **T+~250ms:** FreeRTOS scheduler starts. `safety_task` begins WDT toggle (re-arms watchdog).
11. **T+~700ms:** `brake_init()` completes BOOT_WAIT (500ms). Enters BRAKE_LISTEN_SYNC.
12. **T+~2700ms:** If SEB responds to `0x721`, brake enters ACTIVE. First `0x7B9` command transmitted.
13. **Total unbraked window if SEB releases on comm-fault:** T+120ms to T+2700ms = **~2.58 seconds.**

### 3.2 The MCP4725 ambiguity

The MCP4725 DAC is powered from the 5V rail (12V→5V LDO), not directly from the ESP32's 3.3V. When the ESP32 resets:

- The I2C bus (SDA=GPIO15, SCL=GPIO16) goes high-impedance.
- The MCP4725 retains its last programmed output voltage **as long as VCC (5V) is maintained.**
**Motor kill during SYS reset — covered by MTR.** MTR STM32 is the EGAS Level 1 Function Controller and runs on a separate MCU. When SYS watchdog resets, MTR continues running independently. MTR drives its own MCP4725 DAC and gear relays. MTR has a direct ESTOP GPIO input shared with SYS GPIO1. MTR also monitors SYS heartbeat (0x7FE) and broadcasts CAN 0x001 on timeout. During SYS reset, MTR kills motor (MCP4725=0V) and cuts gear (relays OFF) locally — zero CAN dependency.

**Brake during SYS reset — NOT covered.** SEB is commanded exclusively by SYS via CAN 0x7B9. MTR has no SEB command path. RT could command SEB (it already sends 0x7B9 in AUTO mode) but does not take over on SYS heartbeat loss in the current design — see Issue 7 for the fix. During the SYS reboot window, SEB enters comm-fault after 20ms. Behavior is unverified (hold or release).

### 3.3 Quantified risk (revised)

| Parameter | Value |
|-----------|-------|
| Trigger | SYS firmware hang → external WDT fires |
| Motor response | Safe — MTR kills motor locally (0V throttle, N gear) |
| Brake response | **Non-deterministic** — depends on SEB comm-fault behavior |
| Unbraked window (if SEB releases) | ~2.58 seconds (20ms timeout + ~2.5s SYS reboot + brake LBS) |
| Distance at 25 km/h | ~18 meters |
| Probability | Low (requires firmware hang + SEB releases on comm-fault) |

### 3.4 Why the SEB comm-fault behavior matters critically

The entire safety of the watchdog reset window hinges on one unknown: **does SEB hold or release on CAN timeout?**

- If SEB **holds**: the brake stays engaged during the reset window. The vehicle decelerates under existing brake pressure. Safe.
- If SEB **releases**: the vehicle coasts with no brake for ~2.5s. At 25 km/h downhill, this is dangerous.

This is an **uncontrolled assumption** in the safety architecture. We are depending on a behavior we haven't verified and can't control.

### 3.5 Recommended fix: Hardware brake-hold on watchdog reset

**Approach:** Add a dedicated hardware signal from the TPS3850 watchdog IC's RST output to a relay that controls SEB power or a discrete brake-engage input on SEB (if available).

**Option A — SEB power-cut relay (preferred):**
```
TPS3850 RST (active-low) ──► P-channel MOSFET gate
                                    │
                           SEB 12V power ──► MOSFET (normally ON)
                                    │
                           RST asserted → MOSFET OFF → SEB loses power
```

If SEB loses power:
- Its internal failsafe should engage (electro-hydraulic units typically default to pressure-hold or full-release on power loss — need to verify with SYNTREE spec).
- If SEB defaults to **hold** on power loss: this is ideal. Power-cycle SEB, brake holds.
- If SEB defaults to **release** on power loss: this makes things worse.

**Option B — Dedicated brake relay (more robust):**
```
TPS3850 RST ──► NC relay coil (energized during normal operation)
                      │
                Relay contacts: SEB enable line or brake pressure dump valve
                RST asserted → relay de-energizes → contacts close → brake engage
```

This uses the same NC wiring principle as the ESTOP button. A watchdog reset (or any power loss) de-energizes the relay, which mechanically engages a brake-hold signal. The relay can be wired to:
- A discrete "emergency brake" input on SEB (if it has one — check SYNTREE pinout)
- A solenoid valve that locks hydraulic pressure in the brake line

**Option C — MTR as brake proxy (software, less robust):**

Add SEB command capability to MTR. When MTR detects SYS heartbeat loss (`0x7FE` timeout), MTR starts transmitting `0x7B9` with stroke=max. This requires:
- MTR to have its own CAN ID for SEB command (can't use 0x7B9 — already used by RT/SYS in mode-gated scheme)
- Or MTR to use a new CAN ID that SEB accepts as a secondary command
- SYNTREE SEB must support multiple command sources (unlikely — it's preprogrammed for specific IDs)

This is architecturally complex and depends on SEB capabilities we may not have.

**Option D — Conservative: verify SEB comm-fault behavior and document the assumption**

If SEB is verified to **hold pressure on CAN timeout**, the current design is acceptable — the unbraked window is only 20ms (time to comm-fault detection), not 2.5s. This should be tested empirically. If SEB holds, document it as a verified safety property. If SEB releases, Options A or B become mandatory.

### 3.6 Immediate action

**Test SEB comm-fault behavior:**
1. Command SEB to stroke=27mm (max brake) via CAN 0x7B9 at 50 Hz.
2. After 1 second of sustained pressure, stop CAN transmission.
3. Measure hydraulic pressure over the next 5 seconds.
4. If pressure holds (>80% after 1s): comm-fault = hold → acceptable.
5. If pressure drops to zero within 500ms: comm-fault = release → **hardware fix required before road testing.**

---

## Summary

| Issue | Severity | Probability | Fix complexity | Recommended action |
|-------|----------|-------------|----------------|--------------------|
| **1. ESTOP exit race** | High | Medium | Low (software only, RT-local) | Defer mode transition until ramp complete |
| **2. SEB brake lever contradiction** | High | Low | Low (software only, SYS-local) | **Implemented** — BRAKE_DEGRADED state added to architecture §8.6. |
| **3. Watchdog unbraked window** | High | Low (if SEB holds) / High (if SEB releases) | Medium (hardware) | **Test SEB comm-fault behavior first.** If release: add NC brake relay on WDT RST line. |

**Issue 1 and 2 are software-only fixes** that can be implemented immediately in the RT and SYS firmware respectively. **Issue 3 requires empirical testing** of SEB behavior before the fix path is known — and may require a hardware change.

---

*See also: [[emergency-system]] §1-4 for the documented emergency behaviors that these issues affect, [[external-watchdog]] for the watchdog reset sequence, [architecture.md](../architecture.md) §3 for mode state machine, §8.6 for brake LBS, §8.10 for error handling table.*

---

## Issue 4: Obstacle-Triggered ESTOP "Hold Angle" Creates Rollover Risk While Cornering

### 4.1 The scenario

1. Vehicle is cornering at 25 km/h with steering at 15° (within the 18° dynamic clamp limit at 10 km/h — but at 25 km/h the dynamic clamp would limit to ~5°. So for this scenario, let's assume the vehicle is at 15 km/h, steering at 12°, which is within the dynamic clamp limit at that speed).
2. Jetson perception detects an obstacle → broadcasts CAN `0x001` on high bus.
3. RT receives `0x001`, classifies it as **obstacle-triggered**.
4. RT steer SM: "Obstacle-triggered: hold current angle, silent-stop after 500ms."
5. Steering is held at 12°.
6. Brake engages at max stroke (~27 mm) — full emergency braking.
7. **Combined cornering + hard braking on a three-wheeled vehicle** creates a lateral load transfer that approaches the rollover threshold.

### 4.2 Causal trace

The physics model defines the rollover threshold as:

$$a_y = \frac{v^2}{L}\tan(\delta) > \frac{g w}{2h}$$

The obstacle-triggered ESTOP design rationale is: "don't steer — you might steer into the obstacle or adjacent lane." This is correct for straight-line travel (δ ≈ 0°). It is **incorrect** when the vehicle is already cornering, because:

1. Holding a non-zero steering angle while emergency braking sustains lateral acceleration.
2. Hard braking transfers weight forward, reducing rear tire lateral grip.
3. The tricycle's single front wheel carries both steering and braking forces — combined loading reduces the cornering force available before slip.
4. The dynamic angle clamp (architecture §7.6, Layer 6) is only applied to **commanded angles** in `physics_resolve()`. During ESTOP, the hold behavior bypasses the dynamic clamp entirely — there is no envelope check on the held angle.

**The dynamic angle clamp says 12° is safe at 15 km/h during normal driving.** But during emergency braking, the safe envelope shrinks because braking reduces lateral grip margins. The hold behavior applies the normal-driving envelope to an emergency-braking scenario — these are different physical conditions.

### 4.3 Why the classification is too coarse

The current design classifies ESTOP triggers into exactly two categories:

| Trigger | Classification | Steering behavior |
|---------|---------------|-------------------|
| Jetson perception, RT obstacle sensor | Obstacle | Hold angle, silent-stop after 500ms |
| Button, heartbeat, following error, stale command | Non-obstacle | Ramp to 0° at 20°/s |

This classification assumes:
- Obstacle = going straight, don't veer.
- Non-obstacle = any direction is acceptable to center.

But obstacle detection can occur while cornering. The "hold angle" response to a cornering obstacle does not account for whether the held angle is safe during emergency braking. The classification should consider vehicle state, not just trigger source.

### 4.4 Quantified risk

| Parameter | Value |
|-----------|-------|
| Scenario | Cornering at 15 km/h, δ = 12°, obstacle detected |
| Rollover threshold | Depends on CG height and track width — not yet measured for the physical trike |
| Braking deceleration | ~0.6–0.8g (emergency brake on dry pavement) |
| Weight transfer forward | ~30–40% of rear load shifted to front |
| Lateral grip remaining | Significantly reduced during combined braking+cornering |
| Severity | **High** — rollover is a catastrophic failure mode |
| Probability | **Low** — requires obstacle detection while cornering at speed, which is a narrow window in urban operation |

### 4.5 Recommended fix: Dynamic-clamp the hold angle during obstacle ESTOP

**Approach:** During obstacle-triggered ESTOP, the held angle is clamped to the **dynamic angle clamp limit for the current speed**, using the SAME envelope as normal AUTO steering commands. If the current angle exceeds the limit, the steering ramps down to the limit (not to 0° — to the speed-safe limit).

**Behavior by scenario:**

| Speed | Current δ | Dynamic limit | ESTOP hold behavior |
|-------|-----------|---------------|---------------------|
| 25 km/h | 2° (straight) | 5° | Hold at 2° (within limit, safe) |
| 25 km/h | 8° (cornering, already past limit) | 5° | Ramp from 8° → 5° at 20°/s, then hold at 5° |
| 10 km/h | 12° (cornering) | ~18° | Hold at 12° (within limit, safe) |
| 2 km/h | 40° (U-turn) | 40° | Hold at 40° (within limit, safe) |

**Key properties:**
- Straight-line obstacle detection: behavior is unchanged (δ ≈ 0° is always within the dynamic limit).
- Cornering at speed: steering is reduced to the speed-safe envelope, preventing rollover.
- The vehicle still doesn't suddenly center (would risk lane departure) — it ramps to the safe limit, which at high speed is already near-straight (~5°).
- After 500ms, silent-stop as before — EPS-C handles the timeout.

**Implementation:** In RT's obstacle-triggered ESTOP path, before entering the hold:

```cpp
if (estop_trigger == ESTOP_TRIGGER_OBSTACLE) {
    // Clamp the hold angle to the dynamic limit for current speed
    float dynamic_limit = compute_dynamic_angle_limit(current_speed_mmps);
    float hold_angle = current_steering_angle_deg;
    if (fabs(hold_angle) > dynamic_limit) {
        // Ramp toward the safe limit
        hold_angle = ramp_toward(hold_angle, 
                      copysign(dynamic_limit, hold_angle), 
                      kSteerRampRateDegPerSec);
    }
    // Hold at the clamped angle, silent-stop after 500ms
}
```

This reuses the existing dynamic angle clamp function that already runs in `physics_resolve()` — no new math, just applying it in one more place.

---

## Issue 5: Jetson Heartbeat Loss → Controlled Stop Is Insufficient for Perception Failure

### 5.1 The asymmetry

| Heartbeat lost | Watcher | Timeout | Action |
|---------------|---------|---------|--------|
| SYS (0x7FE) | RT | 1000ms | CAN 0x001 → **ESTOP** (full brake, motor kill, DCDC off) |
| Jetson (0x7FC) | RT | 1500ms | Zero setpoints → **controlled stop** (motor 0V, gear N, no active brake, steering silent) |

The justification given (architecture §8.6): "Jetson is QM, not safety-critical. Its death triggers a controlled stop, not ESTOP. Three missed frames protect against false triggers."

But Jetson runs the **perception stack** — obstacle detection, path planning, collision avoidance. If Jetson dies while navigating traffic, the vehicle continues coasting with:
- No obstacle detection
- No path planning
- No active braking (coast only)
- No active steering (EPS-C timeout-faults)

A "controlled stop" in traffic is just coasting — at 25 km/h, >50m to stop on flat ground with no perception. The rider must recognize the failure and manually brake.

### 5.2 What the architecture actually requires

The architecture's §6.2 Option D comparison table claims: "SYS failure → brake lost? No (RT takes over)." This states RT should take over brake on any MCU failure. But the heartbeat action table only triggers CAN 0x001 — which SEB ignores. RT does NOT take over 0x7B9 transmission on SYS heartbeat loss.

The option D architecture promises RT brake takeover on SYS failure but doesn't implement it. And for Jetson failure, there's no brake takeover at all — just a coast.

### 5.3 False-positive risk assessment

The reason Jetson death isn't ESTOP is concern about false triggers: Jetson is a Linux machine with non-realtime CAN stack. A momentary CAN buffer overflow or scheduling delay could miss heartbeat frames.

But at 1500ms (3 missed frames at 2 Hz), a Linux machine that can't send a single CAN frame in 1.5 seconds is genuinely dead — not "jittery." This is a robust detection threshold. The false-positive risk from non-realtime Linux CAN jitter at 1500ms is very low.

### 5.4 Recommended fix: "Assisted stop" — moderate brake on Jetson loss

**Approach:** When RT detects Jetson heartbeat loss, instead of pure coast, RT commands a moderate brake pressure via 0x205 (RT→SYS) and transitions SYS to MANUAL mode. This is an intermediate response between "controlled stop" (coast) and "ESTOP" (full emergency brake with DCDC off).

**Behavior:**

| Condition | Current | Proposed |
|-----------|---------|----------|
| Jetson heartbeat lost (1500ms) | Zero 0x204 + stop 0x169 → coast | Zero 0x204 + stop 0x169 + **0x205 brake_pressure_kpa = 2000 (2 MPa, ~40% of max)** + **SYS → MANUAL mode** |

**Why 2000 kPa (2 MPa):**
- SYNTREE SEB max pressure is 5 MPa (5000 kPa).
- 2 MPa provides noticeable deceleration (~0.3–0.4g) without wheel lockup.
- This is roughly equivalent to moderate engine braking in a car — the vehicle slows decisively but not violently.
- The rider can always override by pressing the brake lever harder (lever pressure wins in the max-select brake arbitration).

**Implementation path:**
1. RT detects Jetson heartbeat timeout.
2. RT sets `0x205 RT_BRAKE_CMD` = `kBrakeAssistedStopKpa` (2000 kPa).
3. RT sends `0x110` with mode=MANUAL → SYS transitions to MANUAL.
4. SYS receives `0x205 > 0` → SEB Pressure Mode, target 2 MPa.
5. Brake light ON (OR logic: mode change or 0x302 bit).
6. Motor throttle = 0V (from 0x204 speed=0).
7. Gear = N (from 0x204 gear=N).
8. Rider takes over — lever overrides, MODE button available to re-enter AUTO after Jetson recovers.

**Why this is better than full ESTOP:**
- DC-DC stays on → 12V rail stays up → signal lights, brake light, indicators all functional.
- Rider maintains steering (EPS-C standalone in MANUAL) and can override brake.
- No power-cycle needed to recover — just wait for Jetson to reboot and press MODE.
- Safer in traffic than a dark, unbraked vehicle (ESTOP kills all lights).

**Why this is better than pure coast:**
- Active deceleration from the moment of detection.
- Brake light warns following vehicles.
- Transition to MANUAL gives the rider immediate control.
- The 1500ms detection window + ~50ms to first brake pressure = ~1.55s from Jetson death to active braking. At 25 km/h, 10.8m of coast before brake engages — much better than >50m of pure coast.

---

## Issue 6: START Button Failure — No Monitored Fallback for the Only ESTOP Exit

### 6.1 The single point of failure

The START button (GPIO32) is the **only** exit from ESTOP. The architecture states:

> "ESTOP exit: START button → MANUAL mode. (NOT MODE button, NOT CAN command, NOT timeout)"

There is no monitoring of GPIO32 health. Failure modes:
- **Stuck HIGH:** Button disconnected or internal pull-up failed → can never exit ESTOP.
- **Stuck LOW:** Button shorted to ground → could accidentally exit ESTOP (though a falling edge transition is required, so a persistent LOW at power-on wouldn't trigger exit).
- **Debounce failure:** Noisy contact → multiple rapid mode transitions → undefined behavior.

The only backup is power-cycle, which:
1. Restarts all four nodes (Jetson, RT, SYS, MTR).
2. Requires SEB LBS boot sync (~2.5s until brake available).
3. Requires EPS-C LBS boot sync (~2.5s until steering sync).
4. May fail SEB sync (→ BRAKE_DEGRADED, brake lever still works with the fix from Issue 2).
5. Is impractical at roadside in traffic.

### 6.2 Causal trace — stuck START button

```
Rider presses ESTOP (legitimate, e.g., obstacle)
  → Vehicle enters ESTOP: motor kill, full brake, DCDC off
  → Rider assesses: obstacle cleared, safe to proceed
  → Rider presses START button
  → GPIO32 reads HIGH (stuck — broken wire, bad solder joint, corroded contact)
  → mode_task: no falling edge detected
  → Mode remains ESTOP
  → Rider presses START repeatedly — no response
  → Rider's only option: power-cycle
  → All nodes reboot → 3+ seconds before drivable
  → If SEB sync fails during boot → BRAKE_DEGRADED
  → Roadside recovery takes 10+ seconds
```

### 6.3 Recommended fix: MODE button long-press as secondary ESTOP exit

**Approach:** Add a secondary ESTOP exit path via the MODE button (GPIO11). A long-press (3 seconds) of the MODE button exits ESTOP to MANUAL mode, identical to a START button press. The MODE button is on a different GPIO — two independent physical buttons must fail for the rider to be completely locked out.

**Implementation in SYS `mode_task`:**

```cpp
// Existing: START button (GPIO32) — exit ESTOP
if (falling_edge(gpio32) && g_mode == Mode::Estop) {
    mode_set(MANUAL);  // immediate exit
}

// NEW: MODE button (GPIO11) long-press — secondary ESTOP exit
if (gpio11_is_low) {
    gpio11_hold_ms += 10;  // mode_task runs at 10 Hz = 100ms per tick
    if (gpio11_hold_ms >= 3000 && g_mode == Mode::Estop) {
        mode_set(MANUAL);  // long-press exit
        gpio11_hold_ms = 0;
    }
} else {
    gpio11_hold_ms = 0;
}
```

**START button health monitoring (additional):**

```cpp
// In diag_task @ 1 Hz:
if (g_mode == Mode::Estop && estop_duration_ms > 30000) {
    // Been in ESTOP for >30s — check if START button is responsive
    // Log diagnostic. If button is stuck HIGH for the entire duration,
    // set a flag in 0x600 SYS_DIAG_RPT.
}
```

**Why MODE button long-press, not short-press:**
- In MANUAL/AUTO, MODE short-press toggles modes — muscle memory.
- In ESTOP, a long-press prevents accidental exit from bumping the MODE button.
- A 3-second hold is a deliberate action — the rider is consciously choosing to exit ESTOP.
- Different duration from steering retry (START short-press for STEER_FAULT vs START long-press 3s for force-activate).

---

## Issue 7: SYS Crash Has a 1000ms Brake Gap — RT Should Take Over 0x7B9 Immediately

### 7.1 The gap

When SYS crashes:

| Time | Event | Brake state |
|------|-------|-------------|
| T+0 | SYS hangs. Last 0x7B9 frame sent at T−20ms (50 Hz). | Last commanded stroke active |
| T+20ms | SEB: no 0x7B9 for 20ms → **comm-fault timeout.** Behavior unknown. | **Brake lost** (or held, unverified) |
| T+1000ms | RT: SYS heartbeat (0x7FE) frozen for 2 missed frames → timeout. RT broadcasts CAN 0x001. | SEB ignores CAN 0x001 (only responds to 0x7B9) |
| T+1000ms+ | MTR receives CAN 0x001 → kills motor + gear. | Motor safe, brake still lost |

**Brake gap: 20ms to 1000ms = ~980ms.** At 25 km/h, this is ~6.8m of travel with no brake.

### 7.2 The architecture already promises this fix

Architecture §6.2 Option D explicitly claims:

> "SYS failure → brake lost? **No (RT takes over)**"

And:

> "D preserves independent failure modes — RT failure loses AUTO steering/brake but SYS can still brake in MANUAL; **SYS failure loses MANUAL brake but RT can still brake in AUTO.**"

But the current heartbeat action table does NOT implement RT brake takeover on SYS heartbeat loss. It only broadcasts CAN 0x001. RT already knows how to send 0x7B9 (it does so in AUTO mode at 50 Hz with pressure control). It simply doesn't do so on SYS failure.

### 7.3 Why the 1000ms timeout is wrong for the brake path

Architecture §8.6 justifies the 1000ms inter-MCU heartbeat timeout:

> "The fast detection path for actuator faults is the steering following-error check (300ms → ESTOP) and 0x204 staleness (200ms, see below). The heartbeat catches failures those checks miss."

This lists TWO fast detection paths:
1. Steering following error (300ms) — covers EPS-C
2. 0x204 staleness (200ms) — covers motor

There is **no fast detection path for the SEB brake actuator.** The brake relies entirely on the 1000ms heartbeat timeout. But the brake is the most critical actuator — motor kill without brake just means coasting. Brake failure means no way to stop.

The FTTI (Fault Tolerant Time Interval) for brake loss is shorter than for motor or steering loss. The vehicle must be able to stop within a bounded distance. At 25 km/h:
- 200ms → 1.4m (acceptable)
- 1000ms → 6.9m (marginal)
- 2500ms → 17.4m (unacceptable — Issue 3's watchdog window)

### 7.4 Recommended fix: RT brake takeover on SYS heartbeat loss, with shortened timeout

**Approach:** Two changes:

1. **Shorten RT's SYS heartbeat timeout from 1000ms to 200ms.** This is the FTTI-bound value (1.4m at 25 km/h). At 2 Hz heartbeat, this means detecting after 1 missed frame (200ms is >500ms period — wait, at 2 Hz period=500ms, 1 missed frame = 500ms worst case, 2 missed = 1000ms). We need a faster heartbeat or a different detection mechanism.

**Revised heartbeat rate for SYS:** Increase SYS heartbeat from 2 Hz to **10 Hz** (100ms period). Then a timeout of 200ms = 2 missed frames. This requires:
- SYS `hb` task: change from 2 Hz to 10 Hz (trivial, 10 Hz is well within capability).
- Bandwidth: 1 byte DLC at 10 Hz = negligible (<0.02% of 500 kbit/s bus).

2. **On SYS heartbeat timeout, RT immediately takes over 0x7B9 transmission with full brake.** RT already sends 0x7B9 in AUTO mode. On SYS heartbeat loss, RT sends 0x7B9 with stroke=max regardless of mode (mode gate opens on emergency). This is the dual-sender exception documented in §6.2 — both RT and SYS may briefly transmit if SYS isn't truly dead, but SEB accepts whichever has valid checksum + rolling counter.

**New behavior timeline:**

| Time | Event | Brake state |
|------|-------|-------------|
| T+0 | SYS hangs. Last 0x7B9 sent at T−20ms. | Last commanded stroke active |
| T+20ms | SEB comm-fault timeout (if SYS was the last sender). | Behavior unknown |
| T+200ms | RT: SYS heartbeat (0x7FE, now at 10 Hz) frozen for 2 missed frames → timeout. | — |
| T+200ms | RT immediately starts sending 0x7B9 @ 50 Hz with stroke=max. | — |
| T+220ms | SEB receives first RT 0x7B9 frame. | **Brake engages.** |
| **Brake gap** | **20ms to 220ms = 200ms worst case** | **1.4m at 25 km/h** |

**Why RT can safely take over 0x7B9:**

RT already sends 0x7B9 in AUTO mode with the full SYNTREE protocol (rolling counter, checksum, stroke/pressure control). Taking over in MANUAL/ESTOP on emergency is just continuing to send with different parameters (stroke=max instead of computed pressure). No new protocol logic needed. The SYNTREE security bytes (rolling counter + checksum) prove liveness — SEB accepts the frame regardless of which node sent it.

**Collision risk during takeover (both RT and SYS sending 0x7B9):**

If SYS isn't fully dead (heartbeat was delayed but SYS is still transmitting 0x7B9), both RT and SYS temporarily send 0x7B9 on the low bus. Each sends at 50 Hz (20ms period). Both have valid checksums and rolling counters. SEB receives two alternating streams with different counters — this is within the CAN spec (arbitration is per-frame) and SEB's acceptance criteria (valid checksum + counter). SYS's frames have lever-based stroke (0–15mm); RT's frames have max stroke (27mm). The max-select arbitration at the SEB level (it follows whichever command has the higher stroke) would pick RT's 27mm — which is the safe choice (full brake).

**Return to normal operation:** When SYS heartbeat resumes (SYS reboots, sends 0x7FE), RT stops sending 0x7B9 and SYS resumes normal mode-gated operation. The transition is: RT stops → brief gap (≤20ms) → SYS starts. SEB sees a 20ms gap from RT's last frame before SYS's first frame, which is within the 20ms comm-fault threshold (marginal — may need a brief overlap where both send to avoid SEB timeout).

**Interaction with Issue 3 (watchdog brake window):**

With this fix, the watchdog brake window during SYS reset shrinks from ~2.5s to ~200ms: RT detects SYS heartbeat loss within 200ms and fills the brake gap with 0x7B9. SEB receives RT's brake command while SYS reboots. When SYS comes back online and resumes 0x7B9 transmission, RT detects SYS heartbeat return and yields. Total unbraked window: 200ms heartbeat detection + 20ms first frame = 220ms worst case.

The SEB comm-fault behavior test (Issue 3.6) is still needed to confirm the 20ms gap behavior, but the 2.5s window is eliminated.

---

## Updated Summary

| Issue | Severity | Probability | Fix complexity | Recommended action |
|-------|----------|-------------|----------------|--------------------|
| **1. ESTOP exit race** | High | Medium | Low (software, RT-local) | Defer mode transition until ramp complete |
| **2. SEB brake lever contradiction** | High | Low | Low (software, SYS-local) | Replace BRAKE_FAULT with BRAKE_DEGRADED |
| **3. Watchdog unbraked window** | High | Low→Very Low (with Issue 7 fix) | Low (Issue 7 fix covers this) | Issue 7's RT brake takeover closes most of this gap. Verify SEB comm-fault behavior for the 20ms edge. |
| **4. Obstacle ESTOP cornering rollover** | High | Low | Low (software, RT-local) | Dynamic-clamp the hold angle during obstacle ESTOP |
| **5. Jetson death → coast is insufficient** | Medium | Low | Low (software, RT-local) | Assisted stop: 2 MPa brake + MANUAL transition on Jetson heartbeat loss |
| **6. START button failure — no fallback** | Medium | Low | Low (software, SYS-local) | MODE button long-press (3s) as secondary ESTOP exit |
| **7. SYS crash → 1000ms brake gap** | High | Low | Medium (SYS heartbeat rate change + RT takeover logic) | Shorten SYS heartbeat to 10 Hz/200ms timeout; RT takes over 0x7B9 on loss |

**Issues 4, 5, and 6 are software-only.** Issue 7 requires a heartbeat rate change on SYS and brake takeover logic on RT — still software-only, but touches both MCUs. **Issue 7 also significantly mitigates Issue 3** by reducing the watchdog brake window from ~2.5s to ~200ms.

---

## Issue 8: No Independent Brake Monitor — ESTOP Could Silently Have No Brakes

### 8.1 The gap

All 8 safety layers protect motor speed (MCP4725=0V, gear=N, 0x204 staleness check) and steering (following error, dynamic clamp, hard-stops), but **none independently verify that the SEB actually applied braking force.** During ESTOP, SYS (or RT, with the Issue 7 fix) transmits `0x7B9` with stroke=max at 50 Hz. If SEB's CAN receiver is faulted, the system has no way to know the brake command was never received or acted upon.

A stale `0x7B9` TX with no acknowledgment monitoring means ESTOP could silently fail to brake — the motor stops, but the vehicle coasts rather than decelerating under brake force.

### 8.2 What the SEB already provides (no new sensors needed)

Per `docs/by-wire - brake.csv`, the SEB_STATUS frame (`0x721`, 100 Hz = 10ms cycle) already contains:

| Signal | Field | Type | Range | Description |
|--------|-------|------|-------|-------------|
| `SEB_Stroke_Value` | Bytes 2-3 | uint16, scale 0.05, offset -30 | -5 to 27 mm | **Actual stroke position** |
| `SEB_Pressure_Value` | Byte 4 | uint8, scale 0.05 | 0 to 5 MPa | **Actual hydraulic pressure** |
| `SEB_Error_Status` | Byte 0, bits 6-7 | uint2 | 0-3 | 0=no fault, 1=minor, 2=general, 3=**severe (request shutdown)** |
| `SEB_RollCnt_Status` | Byte 6, bits 4-7 | uint4 | 0-15 | Rolling counter — proves SEB is alive |

Additionally, `0x731 SEB_ErrInfo` (10 Hz) provides detailed fault flags including `SEB_CanCom_Err` (CAN Communication Fault, Level 3) — the SEB itself reports when it has lost CAN communication.

**This is sufficient to implement a brake following-error monitor entirely in software, with zero new sensors.**

### 8.3 Causal trace — what a brake monitor catches

```
ESTOP triggered (any source)
  → SYS/RT commands 0x7B9 {Stroke_Value_Req = 1140 (27mm max)}
  → SEB receives frame, validates checksum + rolling counter
     Case A: SEB healthy → stroke actuates to 27mm → 0x721 {Stroke_Value ≈ 1140}
             Monitor: |1140 - 1140| < threshold → brake OK
     Case B: SEB CAN receiver faulted → never receives 0x7B9
             → 0x721 {Stroke_Value = 0 (or last held value)}
             Monitor: |0 - 1140| >> threshold → BRAKE FAULT DETECTED
     Case C: SEB internal fault (motor stall, oil pressure loss)
             → 0x721 {SEB_Error_Status = 3 (severe), Stroke_Value stuck}
             → or 0x731 {SEB_Mtr_Stall_Err = 1, SEB_Oil_Err = 1}
             Monitor: error status or stroke mismatch → FAULT DETECTED
     Case D: SEB mechanically jammed (caliper stuck, hydraulic blockage)
             → 0x721 {Stroke_Value frozen, Pressure_Value rising abnormally}
             Monitor: stroke not tracking command → FAULT DETECTED
```

### 8.4 Recommended fix: Brake following-error monitor in SYS

**Approach:** Add a brake following-error check to SYS's `dispatch` task (processing `0x721`) or `safety` task, analogous to the steering following-error check on RT. Since we cannot add physical sensors, this uses the existing SEB CAN feedback — which is already being received by SYS.

**Implementation:**

```cpp
// In SYS dispatch task, on receiving 0x721 SEB_STATUS:
void check_brake_following_error() {
    float cmd_stroke = last_commanded_stroke_mm;  // from brake_task's 0x7B9 TX
    float actual_stroke = seb_status.stroke_value_mm();  // from 0x721
    float stroke_error = fabs(cmd_stroke - actual_stroke);
    uint8_t seb_error_level = seb_status.error_status();  // bits 6-7

    // Check 1: Severe SEB internal fault
    if (seb_error_level >= 3) {
        log_brake_fault("SEB Level 3 fault — request shutdown");
        g_brake_monitor_fault = true;
    }

    // Check 2: Stroke following error (debounced)
    if (stroke_error > kBrakeStrokeErrorThresholdMm && cmd_stroke > kBrakeMinCmdForCheck) {
        brake_error_duration_ms += 10;  // 0x721 arrives at 100 Hz = 10ms period
        if (brake_error_duration_ms > kBrakeErrorDebounceMs) {
            log_brake_fault("Brake stroke following error: cmd=%.1f act=%.1f",
                            cmd_stroke, actual_stroke);
            g_brake_monitor_fault = true;
        }
    } else {
        brake_error_duration_ms = 0;
    }

    // Check 3: 0x721 staleness (no feedback at all)
    // If no 0x721 received for >100ms (10 missed frames at 100 Hz):
    // SEB is not communicating → brake may be dead
}
```

**Constants:**

| Constant | Value | Rationale |
|----------|-------|-----------|
| `kBrakeStrokeErrorThresholdMm` | 3.0 mm | ~11% of max stroke (27mm). Allows for mechanical compliance but catches gross failures. |
| `kBrakeMinCmdForCheck` | 1.0 mm | Don't check when brake is released (stroke=0) — noise dominates. |
| `kBrakeErrorDebounceMs` | 100 ms | 10 consecutive frames at 100 Hz. Prevents false triggers from hydraulic transients. |
| `0x721 staleness timeout` | 100 ms | 10 missed frames at 100 Hz = SEB CAN dead. |

**What the monitor DOES when a fault is detected:**

Since ESTOP is already the maximum response (motor killed, brake commanded to max), the brake monitor cannot trigger "more ESTOP." But it CAN:

1. **Set a persistent diagnostic flag** in CAN `0x600 SYS_DIAG_RPT` — records that ESTOP braking failed for post-incident analysis.
2. **Log the fault** to persistent storage (if available).
3. **Flash the brake light** in a distinct pattern (if 12V is available — see Issue 12) to alert the rider that braking may be compromised.
4. **If the fault occurs during non-ESTOP operation** (lever braking in MANUAL): log the fault so it's caught early.

**Why this is not identical to the steering following-error check:**

The steering following error triggers ESTOP — it escalates the response. The brake following error CANNOT escalate (ESTOP is already max brake). It serves a different purpose: **fault recording and rider alerting.** Knowing that the brake failed during an incident is critical for accident investigation and system improvement.

### 8.5 SEB_Error_Status as a primary signal

The `SEB_Error_Status` field in `0x721` is particularly valuable because the SEB itself reports when it cannot function:

| Value | Meaning | Action |
|-------|---------|--------|
| 0 | No fault | Normal operation |
| 1 | Level 1 (minor, warning) | Log, continue |
| 2 | Level 2 (general, warning) | Log, set diag flag |
| 3 | **Level 3 (severe, request shutdown)** | **Immediate: set brake fault flag, log, alert rider** |

A Level 3 fault from SEB means the SEB itself has determined it cannot safely operate. This is a manufacturer-defined condition and carries more weight than a stroke comparison.

---

## Issue 9: CAN 0x001 Is Spoofable — No Authentication, DoS-Vulnerable

### 9.1 The gap

CAN `0x001 SAFETY_ESTOP` is an empty frame (DLC=0) with the highest arbitration priority. Any node on either bus can broadcast it. There is no authentication — no checksum, no sender identifier, no rolling counter. A corrupted node that enters an error loop could flood `0x001`, causing:

1. **Persistent ESTOP** — every `0x001` frame re-triggers ESTOP on all receivers.
2. **CAN bus saturation** — at 500 kbit/s, a minimum CAN frame is ~47 bits (standard ID + DLC=0 + stuff bits + EOF). A babbling node could transmit thousands per second, consuming all bus bandwidth.
3. **No recovery except power-cycle** — the flooding node must be physically removed from the bus.

### 9.2 CAN error containment analysis

CAN controllers have built-in error containment: a node that fails repeatedly increments its Transmit Error Counter (TEC). At TEC > 255, the node enters **bus-off** state and disconnects from the bus. This means a flooding node WILL eventually be isolated — but:

- The error recovery mechanism requires the node to successfully transmit before TEC decreases. A genuinely corrupted node in a tight loop may never recover.
- During the flood period (before bus-off), all other traffic is blocked by `0x001` winning every arbitration.
- If the flooding node is on the high bus (Jetson), RT bridges `0x001` to the low bus — doubling the disruption.

### 9.3 Recommended fix: Rate-limit ESTOP processing + sender identification

**Approach:** Two independent protections:

**Protection 1 — Rate limit on ESTOP processing (software, all nodes):**

```cpp
// In each node's CAN dispatch, on receiving 0x001:
static int64_t last_estop_frame_us = 0;
static int estop_count_in_window = 0;
constexpr int64_t kEstopRateWindowUs = 500'000;  // 500ms window
constexpr int kMaxEstopPerWindow = 2;             // max 2 per window

int64_t now = esp_timer_get_time();
if (now - last_estop_frame_us > kEstopRateWindowUs) {
    estop_count_in_window = 0;
    last_estop_frame_us = now;
}
estop_count_in_window++;

if (estop_count_in_window <= kMaxEstopPerWindow) {
    mode_set(Estop);  // process normally
} else {
    // Rate limit exceeded — ignore this frame
    // The first frame already triggered ESTOP; subsequent floods are noise
    log_warning("ESTOP flood detected (%d frames in %lld us)",
                estop_count_in_window, now - last_estop_frame_us);
}
```

This means: the FIRST `0x001` frame triggers ESTOP (as intended). Subsequent floods within the same 500ms window are ignored. A genuine second ESTOP (e.g., physical button pressed after recovering from the first) would arrive after the window expires.

**Protection 2 — Add sender identification to ESTOP frames (protocol change):**

Change `0x001 SAFETY_ESTOP` from DLC=0 to DLC=1 with a single byte identifying the sender:

| Byte | Field | Values |
|------|-------|--------|
| 0 | Sender ID | 0x00 = SYS, 0x01 = RT, 0x02 = Jetson, 0x03 = MTR |

This allows receivers to:
- Log which node triggered ESTOP (valuable for diagnostics).
- Detect duplicate frames from the same sender (ignore if the same sender floods).
- Apply per-sender rate limits (one node flooding doesn't block other nodes' legitimate ESTOP).

The DLC=1 frame is 55 bits (vs 47 for DLC=0) — the arbitration time difference is negligible at 500 kbit/s (~16 µs). The safety benefit of knowing who triggered ESTOP justifies the 8 extra bits.

**Recovery from ESTOP flood:** The rate limiter means the rider can press START to exit ESTOP even during a flood — because `0x001` frames arriving during the rate-limit window are ignored. The flood source must still be resolved (the corrupted node eventually bus-offs or is physically disconnected), but the vehicle can be recovered to MANUAL mode.

---

## Issue 10: No ESTOP Acknowledgment from MTR STM32

### 10.1 The gap

When ESTOP is triggered (button, CAN 0x001, or heartbeat), SYS and RT have no confirmation that MTR actually received and acted on it. MTR cuts throttle and gear locally — but this action is invisible to SYS/RT. The SYS function monitor (EGAS Level 2) compares `0x204` setpoint vs `0x206` feedback. But during ESTOP, `0x204` is forced to zero, so:

- If MTR was already at zero speed (vehicle stopped), a frozen MTR outputting zero would not generate a mismatch — the monitor sees `0x204=0, 0x206=0` and assumes MTR is responding correctly.
- If MTR freezes entirely (no `0x206` transmission), SYS has no `0x206` staleness check — the absence of feedback is not monitored.
- MTR has no dedicated heartbeat CAN ID. It monitors RT heartbeat (`0x7FD`) but doesn't send one of its own.

### 10.2 What MTR already sends

Per MTR config (`mtr-stm32/src/config.h`):
```
kIdSysThrottleSts = 0x120;  // MTR→RT/SYS, 100 Hz — throttle speed
kIdMotorFbk       = 0x206;  // MTR→SYS/RT, 50 Hz  — speed, gear state, fault flags
```

`0x206 MTR_MOTOR_FBK` at 50 Hz contains `i16 actual_speed, u8 gear_state, u8 fault_flags`. The `fault_flags` field is the mechanism for MTR to report its internal state.

### 10.3 Recommended fix: ESTOP acknowledgment via 0x206 fault_flags

**Approach:** MTR sets a dedicated `ESTOP_ACTIVE` bit in the `0x206 fault_flags` byte when it has locally acted on ESTOP (throttle DAC=0, all gear relays OFF). This is a software change on MTR.

**Implementation:**

```cpp
// In MTR fault_flags bit definitions:
constexpr uint8_t kMtrFaultEstopActive = 0x01;  // bit 0: ESTOP active locally
constexpr uint8_t kMtrFaultDacFault    = 0x02;  // bit 1: MCP4725 I2C fault
constexpr uint8_t kMtrFaultGearFault   = 0x04;  // bit 2: gear relay fault
// ... other fault bits ...

// In MTR main loop, after ESTOP action:
if (estop_asserted) {
    dac_write(0);           // MCP4725 = 0V
    gear_all_off();         // all relays OFF
    fault_flags |= kMtrFaultEstopActive;  // ACKNOWLEDGE
} else {
    fault_flags &= ~kMtrFaultEstopActive;
}
// fault_flags is transmitted in 0x206 at 50 Hz
```

**On SYS side (EGAS Level 2 monitor):**

```cpp
// In SYS dispatch, processing 0x206:
if (g_mode == Mode::Estop) {
    if (!(mtr_fbk.fault_flags & kMtrFaultEstopActive)) {
        // MTR has NOT acknowledged ESTOP
        // This could mean: MTR didn't receive ESTOP, MTR CAN is dead, or MTR is frozen
        mtr_estop_ack_timeout_ms += 20;  // 0x206 at 50 Hz = 20ms period
        if (mtr_estop_ack_timeout_ms > 100) {  // 5 missed frames
            log_error("MTR failed to acknowledge ESTOP");
            g_mtr_estop_fault = true;
            // MTR is already supposed to kill motor — if it hasn't acknowledged,
            // something is wrong. Set diag flag for post-incident analysis.
        }
    } else {
        mtr_estop_ack_timeout_ms = 0;
        g_mtr_estop_fault = false;
    }
}

// Also: 0x206 staleness check (new)
if (time_since_last_0x206 > 200) {  // 10 missed frames at 50 Hz
    log_error("MTR feedback lost — 0x206 absent for >200ms");
    g_mtr_comms_lost = true;
}
```

**What this gives us:**
1. Positive confirmation that MTR received and acted on ESTOP (via `ESTOP_ACTIVE` bit).
2. Detection of MTR communication loss (via `0x206` staleness check).
3. The EGAS Level 2 monitor can now distinguish: "MTR is healthy and braking" vs "MTR is silent and possibly still powering the motor."

---

## Issue 11: Startup Grace Period Masks Heartbeat but Not 0x204 Staleness

### 11.1 The gap

The startup grace period (3 seconds) masks heartbeat checks to prevent false ESTOP at boot. But the `0x204` staleness check on SYS is NOT masked. On cold boot:

1. SYS boots first (fastest boot time).
2. RT is still booting — `0x204` is absent.
3. At T+200ms after SYS `motor_task` starts: no `0x204` received → staleness triggers: `setpoint.speed = 0, setpoint.gear = N`.

This is a false trigger — the vehicle isn't even running, so forcing speed=0 and gear=N is harmless (already the default). But it's logically inconsistent: if the system is in a startup grace period where "things aren't ready yet," ALL safety checks that depend on external data should be masked, not just heartbeats.

The risk is low (the false trigger produces a safe outcome), but the design principle is wrong — it indicates the startup grace concept was applied inconsistently.

### 11.2 Recommended fix: Gate 0x204 staleness with the same startup grace period

**Approach:** Apply the same 3-second startup grace to the `0x204` staleness check:

```cpp
// In SYS motor_task:
bool startup_grace_active = (esp_timer_get_time() < kStartupGracePeriodUs);  // 3s

if (!startup_grace_active && time_since_last_0x204 > kSetpointStaleMs) {
    setpoint.speed = 0;
    setpoint.gear = N;
}
```

This aligns the staleness check with the heartbeat check — both are masked during the 3-second startup window. After 3 seconds, the vehicle should have all nodes online; if `0x204` is still absent, it's a genuine fault.

---

## Issue 12: "Both Bulbs OFF = ESTOP" Is Ambiguous at Power-Off

### 12.1 The gap

The rider's guide (§7.1 of emergency-system.md) states:

| Indicators | Meaning |
|------------|---------|
| Both bulbs OFF | ESTOP — vehicle is emergency-stopped |

But a powered-down vehicle also shows both bulbs OFF. The rider cannot distinguish ESTOP-persisted from normal power-off without pressing START. Pressing START on a powered-off vehicle does nothing (SYS isn't running to detect the button). The rider might mistake power-off for ESTOP and try to power-cycle — which would actually start the vehicle in MANUAL mode, potentially while they think they're troubleshooting an ESTOP condition.

Furthermore, during ESTOP the DC-DC converter is OFF (CAN `0x012 enable=0`), which means the **12V rail is dead.** Both the AUTO bulb (GPIO25) and MANUAL bulb (GPIO26) are powered from the 12V accessory rail via relay. If 12V is dead, the bulbs physically cannot illuminate — even if SYS drives the GPIOs. The brake light (GPIO21) is also on 12V and also cannot illuminate.

**This means the OR logic "brake light ON during ESTOP" is physically impossible with the current power architecture.** The 12V rail must be alive for any lamp to work.

### 12.2 Recommended fix: Keep DC-DC enabled during ESTOP; cut only the accessory relay

**Approach:** During ESTOP, keep the DC-DC converter ON (`0x012 enable=1`) to maintain 12V for CAN transceivers, MCU power, and the brake light. Cut only the 12V accessory relay (GPIO27) to kill non-safety lights (headlight, turn signals, mode indicators).

**Revised ESTOP power behavior:**

| Component | Current ESTOP | Revised ESTOP |
|-----------|--------------|---------------|
| DC-DC converter (0x012) | OFF | **ON** (maintains 12V for safety-critical loads) |
| 12V accessory relay (GPIO27) | OFF | OFF (cuts headlight, turn signals, mode bulbs) |
| Brake light (GPIO21) | DEAD (no 12V) | **ON** (powered from DC-DC, not through accessory relay) |
| Mode bulbs (GPIO25/26) | DEAD | OFF (cut by accessory relay — intentional ambiguity eliminated by brake light) |
| CAN transceivers | DEAD (no 5V) | **ALIVE** (5V from DC-DC→LDO) |
| MCUs | Variable | **ALIVE** (3.3V from DC-DC→LDO) |

**This requires one wiring change:** The brake light (GPIO21 relay) must be powered from the always-on DC-DC output, not from the accessory relay output. This is a wiring change, not a new sensor. The MCUs are already on the always-on rail (otherwise SYS couldn't run during ESTOP).

**With this change, ESTOP is visually distinct:**
- Brake light: **ON** (always, during ESTOP — powered from DC-DC directly)
- Mode bulbs: **OFF** (cut by accessory relay)
- Headlight, turn signals: **OFF** (cut by accessory relay)

A powered-off vehicle: **everything OFF including brake light.** The rider can now distinguish.

**Without the wiring change (immediate documentation fix):** Document that DC-DC stays ON during ESTOP (to keep MCUs and CAN alive — it always did, the architecture was ambiguous). The brake light illuminates if it's on the always-on rail; if it's not, document it as a known wiring limitation and recommend the rewire.

---

## Issue 13: EPS-C Mechanical Jam Silent-Stop Recovery Path Unclear

### 13.1 The gap

Emergency-system.md §3.3 describes the mechanical jam fallback: if following error persists during ESTOP centering ramp (>5° for >1s), fall back to silent-stop (stop transmitting `0x169`). EPS-C enters internal fault-lock.

§4.6 says the boot sync timeout for EPS-C requires "rider retry via START short-press" to reset to STEER_LISTEN_SYNC.

The question: does the mechanical-jam silent-stop during ESTOP enter the same STEER_FAULT state, making it recoverable via START short-press? Or does it require a full power-cycle?

### 13.2 State machine analysis

During ESTOP centering ramp:
- Steer SM is in `STEER_ACTIVE` (transmitting `0x169` with ramping targets).
- Mechanical jam triggers: following error >5° for >1s.
- RT stops transmitting `0x169` → EPS-C enters comm-fault.

At this point, the steer SM state should transition from `STEER_ACTIVE` to `STEER_FAULT`. But the architecture doesn't specify this transition — it says "fall back to silent-stop" without naming the state.

If the steer SM remains in `STEER_ACTIVE` (just silently stopped), then when the rider exits ESTOP to MANUAL, the SM would still be `STEER_ACTIVE` but in MANUAL mode, which says "do NOT send 0x169." This works — no transmission. But the EPS-C is still fault-locked from the silent-stop, so the rider has no steering assist in MANUAL.

If the steer SM transitions to `STEER_FAULT`, the documented recovery path applies:
- Short-press START → reset to `STEER_LISTEN_SYNC` (EPS-C must respond to `0x201`).
- Long-press START (3s) + throttle at zero → force-activate with target=0° (MANUAL only, AUTO locked out).

### 13.3 Recommended fix: Explicit transition to STEER_FAULT on mechanical jam

**Approach:** When the mechanical jam fallback triggers during ESTOP, explicitly transition the steer SM to `STEER_FAULT`. The existing STEER_FAULT recovery paths then apply:

```cpp
// In steer SM, during ESTOP centering ramp:
if (following_error_deg > 5.0f && error_duration_ms > 1000) {
    // Mechanical jam — stop transmitting
    stop_0x169();
    steer_state = STEER_FAULT;  // explicit transition
    g_steer_ramp_in_progress = false;
    log_error("Steering mechanical jam during ESTOP centering");
}
```

**Recovery sequence for the rider:**
1. Mechanical jam occurs during ESTOP → steering silent-stops.
2. Rider presses START → vehicle enters MANUAL mode.
3. Steer SM is in `STEER_FAULT` → no 0x169 transmission.
4. Rider short-presses START → steer SM resets to `STEER_LISTEN_SYNC`.
5. If EPS-C responds to `0x201` with `SES_INF_Angle_Status == 1`: → `STEER_ACTIVE` in MANUAL (EPS-C standalone — steering wheel works normally).
6. If EPS-C doesn't respond (still fault-locked): remains in `STEER_FAULT`. Rider long-presses START (3s) + throttle at zero → force-activate with target=0°.
7. If force-activate also fails: power-cycle is the final recovery.

---

## Issue 14: Fixed 5° Following Error Threshold at High Speed

### 14.1 The gap

The steering following error threshold is a fixed 5° regardless of vehicle speed. But at high speed, the dynamic clamp limits commanded steering to ~5°, making the 5° threshold represent a **100% error** (commanded 5°, actual 0°). At low speed, the dynamic clamp allows ~40°, making 5° only a **12.5% error.**

The sensitivity is inverted: at high speed where precise steering matters most, the threshold is coarsest relative to the command range. A 4° error at 25 km/h (80% of the allowed steering range) would NOT trigger ESTOP — the vehicle continues at speed with severely degraded steering authority.

### 14.2 Quantified

| Speed | Dynamic limit | 5° fixed threshold as % of range |
|-------|--------------|----------------------------------|
| 2 km/h | 40° | 12.5% — appropriate |
| 10 km/h | 18° | 27.8% — loose |
| 25 km/h | 5° | **100%** — must have ZERO response to trigger |

At 25 km/h, a 4° error (commanded 5°, actual 1°) represents the EPS-C delivering only 20% of the commanded steering angle. This is a degraded steering condition at speed that the fixed 5° threshold would miss entirely.

### 14.3 Recommended fix: Speed-scaled following error threshold

**Approach:** Make the following error threshold proportional to the dynamic angle clamp limit for the current speed, with a floor:

```
threshold = max(kMinFollowingErrorDeg, kFollowingErrorRatio * dynamic_limit)
```

| Constant | Value | Rationale |
|----------|-------|-----------|
| `kMinFollowingErrorDeg` | 2.0° | Floor — prevents noise triggers at very low speed |
| `kFollowingErrorRatio` | 0.25 | 25% of the speed-safe range |

**Behavior:**

| Speed | Dynamic limit | New threshold | Old threshold |
|-------|--------------|---------------|---------------|
| 2 km/h | 40° | max(2°, 10°) = **10°** | 5° (was too sensitive for parking) |
| 10 km/h | 18° | max(2°, 4.5°) = **4.5°** | 5° (similar) |
| 25 km/h | 5° | max(2°, 1.25°) = **2°** | 5° (was 2.5× too loose) |

At low speed: the threshold loosens (10° vs 5°), appropriate for parking maneuvers where mechanical compliance and tire scrub produce larger following errors that aren't safety-relevant.

At high speed: the threshold tightens (2° vs 5°), catching degraded steering authority before it becomes dangerous. A 4° error at 25 km/h would now trigger ESTOP — which it should, because the EPS-C has lost ~80% of its commanded authority.

**Implementation:** This is a one-line change in RT's following error check — replace the hardcoded `5.0f` with a call to compute the speed-scaled threshold using the existing dynamic clamp value.

---

## Updated Summary (Issues 8-14)

| Issue | Severity | Fix complexity | New hardware? | Recommended action |
|-------|----------|----------------|---------------|--------------------|
| **8. No brake monitor** | High | Low (software, SYS-local) | **None** — SEB provides 0x721 stroke/pressure/error at 100 Hz | Add brake following-error check comparing 0x7B9 cmd vs 0x721 feedback. Monitor SEB_Error_Status for Level 3 faults. Add 0x721 staleness check. |
| **9. 0x001 spoofable** | Medium | Low (software, all nodes) | None | Rate-limit ESTOP processing (max 2 frames per 500ms window). Add DLC=1 sender-ID byte to 0x001. |
| **10. No MTR ESTOP ACK** | Medium | Low (software, MTR+SYS) | None | MTR sets ESTOP_ACTIVE bit in 0x206 fault_flags. SYS checks ACK within 100ms of ESTOP. Add 0x206 staleness check. |
| **11. Startup grace inconsistent** | Low | Trivial (software, SYS) | None | Gate 0x204 staleness check with same 3s startup grace as heartbeat. |
| **12. ESTOP HMI ambiguous** | Medium | Low (software + wiring) | **Rewire only** — brake light to always-on DC-DC rail, not accessory relay | Keep DC-DC ON during ESTOP; cut only accessory relay (GPIO27). Brake light illuminates during ESTOP. Mode bulbs OFF during ESTOP (distinct from power-off where all lights OFF). |
| **13. EPS-C jam recovery unclear** | Low | Trivial (documentation + software) | None | Explicitly transition to STEER_FAULT on mechanical jam. Document recovery via START short-press → LISTEN_SYNC retry. |
| **14. Fixed 5° following error** | Medium | Trivial (software, RT-local) | None | Speed-scaled threshold: `max(2°, 0.25 × dynamic_limit)`. 2° at 25 km/h, 10° at 2 km/h. |

**All seven issues are software-fixable.** Issue 12 requires a brake light wiring change (to the always-on DC-DC rail) but no new sensors or components.
