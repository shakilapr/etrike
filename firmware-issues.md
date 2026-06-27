# RT ESP32-S3 Firmware — Over-Complication & Issue Analysis

Deep analysis of `rt-esp32/src/` against `sys-esp32/src/`, `shared/`, and `architecture.md`.
Focus: things that make debugging difficult, cause bugs, or deviate from the architecture.

---

## Issue 1 — `g_ses_angle_raw` is offset-free but named "raw"

### Evidence

**`can_dispatch.h:83`** — offset is subtracted on input:
```cpp
g_ses_angle_raw.store(ctx.steer_feedback_angle - rt::kSyntreeAngleOffset);
// steer_feedback_angle = raw u16 from 0x201 (0–60000, 0.1°/bit, offset -3000)
// kSyntreeAngleOffset  = 30000
// stored value ranges -30000..+30000 in 0.1° units, offset-free
```

**`steering_control.h:199`** — offset is added back for 0x169 output:
```cpp
out.target_angle = m_active_angle + kSyntreeAngleOffset;
```

**`main.cpp:457`** — offset is added back for 0x310 STEER_DIAG:
```cpp
int16_t angle = g_ses_angle_raw.load() + rt::kSyntreeAngleOffset;
```

**`safety_monitor.h:119-122`** — used WITHOUT offset for follow-error:
```cpp
int16_t cmd_raw    = g_last_cmd_angle_raw.load();   // offset-free
int16_t actual_raw = g_ses_angle_raw.load();          // offset-free → comparison works
```

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | No — the offset is applied correctly at every edge. The system is internally consistent. |
| **Debugging impact** | **High.** The name "raw" strongly implies CAN-bus raw (0–60000 range with offset). A developer seeing `g_ses_angle_raw = 0` expects that means "angle at minimum" but it actually means "wheels centered." If you're oscilloscope-probing the CAN bus and see value 30000 on 0x201, you'll look for where that becomes 0 — it happens silently at line 83, in a different file than where it's consumed. |
| **Safety impact** | **Low.** The offset convention is applied consistently. No safety-critical path gets the wrong value. |
| **Maintainability** | **Medium.** Adding a new consumer of `g_ses_angle_raw` requires knowing whether it's offset-free or not. Two consumers (STEER_DIAG, steering build) add the offset; one (follow-error) does not. A future developer adding a fourth consumer has a 50% chance of getting it wrong. |

### Recommendation

Rename to `g_ses_angle_0_1deg` (or `g_ses_angle_internal`). The name should communicate that the value is in internal convention (0.1°/bit, 0 = center), not CAN raw. Same for `g_last_cmd_angle_raw` → `g_last_cmd_angle_0_1deg`.

---

## Issue 2 — Steering follow-error: both sides multiplied to a common unit neither input uses

### Evidence

**`safety_monitor.h:119-127`:**
```cpp
int16_t cmd_raw    = g_last_cmd_angle_raw.load();     // 0.1° units, offset-free
int16_t actual_raw = g_ses_angle_raw.load();            // 0.1° units, offset-free
if (actual_raw != INT16_MIN) {
    int32_t diff      = int32_t(cmd_raw) - int32_t(actual_raw);
    int32_t err_mdeg  = (diff >= 0 ? diff : -diff) * 100;     // ×100 → millidegrees

    float threshold_deg = rt::compute_following_error_threshold(
        g_mtr_actual_speed_mmps.load());                       // returns degrees
    int32_t kThresholdMdeg = static_cast<int32_t>(
        threshold_deg * 1000.0f);                              // ×1000 → millidegrees

    if (err_mdeg > kThresholdMdeg) { ... }                     // millideg vs millideg
```

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **No.** The math is algebraically correct. `\|diff\| × 100 > threshold × 1000` simplifies to `\|diff\| > threshold × 10`. Both sides are scaled by the exact same ratio. |
| **Debugging impact** | **Medium.** If you're checking a follow-error violation at 25 km/h: the dynamic limit is ~5°, the threshold is max(2°, 0.25×5°) = 2°. The threshold in 0.1° units is 20. The diff between command and actual might be 25 (0.1° units). You'd see `err_mdeg = 2500` and `kThresholdMdeg = 2000` in the debugger — values in the thousands for what's conceptually a 2.5° vs 2.0° comparison. Mental overhead to convert back. |
| **Safety impact** | **None.** The comparison result is identical. |
| **Maintainability** | **Low.** If someone changes `err_mdeg` to use a different multiplier (e.g., ×10 thinking it's 1° units), the comparison breaks silently — ESTOP would trigger at 1/10th the intended threshold. The current pattern has no single source of truth for the unit. |

### Current effective formula

```
|cmd_0.1deg - actual_0.1deg| > max(2.0, 0.25 × dynamic_limit_deg) × 10
```

Where `dynamic_limit_deg = clamp(40.0 - (|speed_kmh| - 2.0) × 35.0/23.0, 5.0, 40.0)`.

### Recommendation

Do the comparison in 0.1° units directly — both inputs are already in that unit:
```cpp
int32_t err_0_1deg = std::abs(int32_t(cmd_raw) - int32_t(actual_raw));
int32_t threshold_0_1deg = static_cast<int32_t>(threshold_deg * 10.0f);
if (err_0_1deg > threshold_0_1deg) { ... }
```

---

## Issue 3 — `can_send_estop()` duplicated verbatim in RT and SYS

### Evidence

**`rt-esp32/src/safety_monitor.h:53-59`:**
```cpp
inline bool can_send_estop() {
    constexpr int64_t kMinIntervalUs = 250'000;
    int64_t now = esp_timer_get_time();
    int64_t last = g_last_estop_sent_us.load(std::memory_order_relaxed);
    if (now - last < kMinIntervalUs) return false;
    g_last_estop_sent_us.store(now, std::memory_order_relaxed);
    return true;
}
```

**`sys-esp32/src/main.cpp:64-71`:**
```cpp
static bool can_send_estop() {
    constexpr int64_t kMinIntervalUs = 250'000;
    int64_t now = esp_timer_get_time();
    int64_t last = g_last_estop_sent_us.load(std::memory_order_relaxed);
    if (now - last < kMinIntervalUs) return false;
    g_last_estop_sent_us.store(now, std::memory_order_relaxed);
    return true;
}
```

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **Not currently.** Both copies are identical. But if the rate limit is changed in one and not the other, RT and SYS would have different flooding protection — SYS could flood while RT rate-limits, or vice versa. |
| **Debugging impact** | **Low.** The function is small and self-contained. But if an ESTOP rate-limiting bug is reported, the developer must check two locations to confirm which ECU originated the flood. |
| **Safety impact** | **Medium if they diverge.** The 250ms interval prevents a stuck node from saturating the CAN bus with 0x001 frames (highest priority arbitration). If one ECU's rate limiter fails, it could block all other traffic. |
| **Maintainability** | **High.** Two copies = two places to update when the rate limit changes. The 250ms value is hardcoded as a literal, not a named constant — changing it requires finding every copy. |

### Recommendation

Move to `shared/shared_config.h` as a function template or inline that takes the timestamp atomic as a parameter. Each ECU keeps its own `g_last_estop_sent_us` (correct — per-ECU rate limiting), but the logic lives in one place.

---

## Issue 4 — Two near-identical 0x7B9 blocks in `t_can_tx_low`

### Evidence

**Block A — SEB takeover** (`main.cpp:370-384`, SYS heartbeat loss):
```cpp
static uint8_t seb_roll = 0;
if (g_seb_takeover.load(std::memory_order_relaxed)) {
    can::VcuSebReq seb;
    seb.control_enable = 1;
    seb.align_enable   = 1;
    seb.control_mode   = 0;       // Stroke mode
    seb.auto_brake     = 1;       // Emergency
    seb.stroke_req     = 1140;    // max stroke (27mm)
    seb.roll_cnt_enable = 1;
    seb.checksum_enable = 1;
    seb.rolling_counter = seb_roll;
    seb_roll = (seb_roll + 1) & 0x0F;
    seb.to_frame(fr); drv->send(fr);
}
```

**Block B — Option D AUTO** (`main.cpp:391-415`, normal operation):
```cpp
static uint8_t seb_auto_roll = 0;
if (!g_seb_takeover.load(std::memory_order_relaxed)
    && g_mode_current.load() == uint8_t(can::Mode::Auto)
    && g_steering.state() == rt::SteerState::STEER_ACTIVE) {
    int32_t kpa = g_brake_kpa_to_send.load();
    can::VcuSebReq seb;
    seb.control_enable = 1;
    if (kpa > 0) {
        seb.control_mode = 1;    // Pressure mode
        seb.pressure_req = ...;
        seb.stroke_req   = 600;  // 0mm baseline
        seb.auto_brake   = 1;
    } else {
        seb.control_mode = 0;    // Stroke mode
        seb.stroke_req   = 600;  // 0mm
    }
    seb.roll_cnt_enable = 1;
    seb.checksum_enable = 1;
    seb.rolling_counter = seb_auto_roll;
    seb_auto_roll = (seb_auto_roll + 1) & 0x0F;
    seb.to_frame(fr); drv->send(fr);
}
```

### Shared structure (both blocks)

1. Create `can::VcuSebReq seb`
2. Set `control_enable=1`, `roll_cnt_enable=1`, `checksum_enable=1`
3. Set mode-dependent stroke/pressure fields
4. Assign rolling counter from a `static` local
5. Increment rolling counter `= (x + 1) & 0x0F`
6. Call `seb.to_frame(fr)` then `drv->send(fr)`

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **No in normal operation** — the two blocks are mutually exclusive (takeover vs !takeover && AUTO). But the two separate rolling counters (`seb_roll`, `seb_auto_roll`) are a **latent bug risk**: they will produce non-monotonic counters if the system transitions between takeover and AUTO modes. The SEB validates rolling counters and may reject frames with non-sequential values. After a takeover→recovery transition, `seb_auto_roll` resumes from its last value (not from `seb_roll`'s last value), creating a gap the SEB may interpret as a fault. |
| **Rolling counter gap scenario** | 1. AUTO mode active, `seb_auto_roll` = 5. 2. SYS heartbeat lost, takeover activates, `seb_roll` = 0–3 sent. 3. SYS heartbeat recovers, takeover ends. 4. AUTO resumes, `seb_auto_roll` = 6. SEB sees counter jump from 3 (takeover's last) to 6 (AUTO's next). The SEB's rolling counter validation window (typically ±1) would reject frame 6. **This is a real bug.** |
| **Debugging impact** | **High.** When brake behavior is wrong, you must determine which of two identical-looking blocks executed. The conditions span three atomics + one state machine query. No log message distinguishes which path was taken. |
| **Safety impact** | **Medium (rolling counter gap).** If the SEB rejects frames due to counter discontinuity after a takeover→recovery transition, brake commands are silently lost for one or more 50 Hz cycles until the counter catches up. On a 20ms period, each lost frame is 20ms of no braking. |
| **Maintainability** | **High.** Adding a new 0x7B9 sender (e.g., a third mode) would require a third copy of the 12-line template with a third rolling counter. |

### Recommendation

Unify into a single function:
```cpp
static void send_seb_req(can::VcuSebReq& seb, uint8_t& roll,
                          int control_mode, int stroke, int pressure,
                          int auto_brake, CanDriver* drv, can::Frame& fr);
```
Called once from the takeover path and once from the AUTO path with different parameters. Single rolling counter ensures monotonic sequence regardless of mode transitions.

---

## Issue 5 — Monolithic `t_control`: 7 subsystems in 195 lines

### Evidence

`main.cpp:113-307`. The task body (one `while(1)` loop) contains:

| Lines | Subsystem | Rate |
|-------|-----------|------|
| 124-144 | Safety event queue drain | 100 Hz |
| 149-150 | Host command consume | 100 Hz |
| 152-163 | Kinematics + obstacle + angle clamp | 100 Hz |
| 166-177 | Obstacle→kPa + brake arbitration | 100 Hz |
| 179-210 | Safety checks + ESTOP origination + steering ESTOP | 100 Hz |
| 212-213 | Brake kPa publish + setpoint queue | 100 Hz |
| 215-248 | Shadow PID (18 lines of `#ifdef` comments) | 100 Hz |
| 250-257 | Telemetry capture + WDT toggle | 100 Hz |
| 259-302 | CAN bus-off monitoring (both buses) | **1 Hz** |

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **Not directly**, but the bus-off monitoring (44 lines, 1 Hz) has its own error counters, recovery logic, and ESTOP origination — all inlined into a 100 Hz task. If bus-off recovery fails, the control task's main loop is cluttered with logic that has nothing to do with 100 Hz vehicle control. |
| **Debugging impact** | **High.** To answer "why did the vehicle ESTOP?", you must read 195 lines covering 7 different failure modes (stale command, ESTOP event, mode=ESTOP, SYS heartbeat loss, Host heartbeat loss, steer follow-error, CAN bus-off). Each mode has its own trigger condition at a different location in the function. |
| **Safety impact** | **Low.** The subsystems are functionally independent and don't interfere. But the code density makes it harder to audit — a safety reviewer must verify that no line between 179 and 302 accidentally resets `sr.zero_setpoints` after it was set. |
| **Maintainability** | **High.** Compare with SYS: SYS has `task_safety` (safety checks), `task_motor` (motor control), `task_brake` (brake control), `task_diag` (diagnostics), each under 60 lines. RT crams equivalent logic into one task. Adding a new feature (e.g., IMU-based tilt detection) would add more lines to this already 195-line function. |

### Recommendation

Extract the bus-off monitor into a helper (or a dedicated low-priority task like SYS's `task_diag`). Extract the obstacle→kPa formula (duplicates `PhysicsModel::obstacle_limit` shape) into `PhysicsModel::obstacle_to_kpa()`. Move the shadow PID `#ifdef` block to `SpeedController` where it belongs.

---

## Issue 6 — Safety state split: events (queue) + data (atomics)

### Evidence

The safety evaluation in `run_safety_checks()` reads state from two different mechanisms:

**Via safety event queue** (drained at top of `t_control`):
- `ESTOP` event → `m_estop_pending = true`
- `MODE_CHANGE` event → `m_current_mode = evt.payload`
- `SEB_TAKEOVER` event → `m_seb_takeover = true` **(dead code — never enqueued, see Issue 9)**
- `SEB_RELEASE` event → `m_seb_takeover = false` **(dead code — never enqueued)**

**Via atomics** (read inside `run_safety_checks()`):
- `g_last_sys_hb_us` → SYS heartbeat timeout check
- `g_last_host_hb_us` → Host heartbeat timeout check
- `g_ses_angle_raw` + `g_last_cmd_angle_raw` → steer follow-error check
- `g_mtr_actual_speed_mmps` → dynamic limit for follow-error threshold

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **Design-level.** The architecture principle #1 states "Queues over shared state. No mutexes, no semaphores." The implementation uses queues for exactly 4 event types (2 of which are dead code) and atomics for everything else. The heartbeat timeout checks bypass the queue entirely — the dispatch task writes `g_last_sys_hb_us` via atomic, and `t_control` reads it 0–10ms later. This works because heartbeat timestamps have latest-value semantics (only the most recent matters), but it violates the stated architecture principle across 6 atomics. |
| **Debugging impact** | **Medium.** To understand the complete safety state at any moment, you must check: (a) the event queue drain result (local variables `m_estop_pending`, `m_current_mode`, `m_seb_takeover`), AND (b) the atomics (`g_last_sys_hb_us`, `g_last_host_hb_us`, `g_ses_angle_raw`, etc.). Two different update patterns, two different read patterns. |
| **Safety impact** | **Low.** The split is semantically correct — events for discrete transitions (ESTOP happened), atomics for continuous values (last heartbeat timestamp). But the architecture promised queues for everything, and the code doesn't deliver, creating a false sense of design consistency. |
| **Maintainability** | **Medium.** A new safety check (e.g., IMU tilt > 30° → ESTOP) requires the developer to decide: event or atomic? No clear guideline exists because the codebase does both. |

### Note

This is arguably the least severe issue — the atomics + queue hybrid is a pragmatic choice that works correctly. The architecture principle #1 is aspirational, not mandatory. But the mismatch between documented architecture and implementation creates confusion during code review and onboarding.

---

## Issue 7 — `SteeringControl` driven from 3 tasks at 2 priorities

### Evidence

Three different FreeRTOS tasks manipulate `g_steering`:

| Task | Priority | Rate | Operations |
|------|----------|------|-----------|
| `t_dispatch` | 4 | Event-driven | `exit_estop()` — line 177 of can_dispatch.h |
| `t_control` | 4 | 100 Hz | `set_target()` — line 321; `start_estop()` — line 206; `set_estop_hold_time()` — line 208 |
| `t_can_tx_low` | 3 | 50 Hz | `tick()` — line 363 |

### State transitions across tasks

```
t_dispatch (prio 4) ──exit_estop()──► m_estop_exit_pending = true
                                           │
t_control (prio 4)  ──set_target()──► clears m_estop_exit_pending (if ACTIVE)
                     ──start_estop()─► transitions to RAMP_TO_ZERO or HOLD_THEN_SILENT
                                           │
t_can_tx_low (prio 3) ──tick()──────► drives state machine forward each 50Hz tick
                                       consumes m_estop_exit_pending when ramp/hold completes
```

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **Potentially.** The `exit_estop()` sets `m_estop_exit_pending = true`. Then `set_target()` (called at 100 Hz from `t_control`) clears it: `m_estop_exit_pending = false` — but only if `m_state == STEER_ACTIVE`. If the state is still `ESTOP_RAMP_TO_ZERO`, the pending flag survives. This is correct but fragile: the interaction depends on `set_target()` being called while in ESTOP state to NOT clear the flag. If `set_target()` is ever modified to clear the flag unconditionally, the exit-ESTOP path breaks silently. |
| **Debugging impact** | **Very high.** "Why didn't the steering exit ESTOP?" requires tracing: (1) Was `exit_estop()` called? (dispatch, event-driven — was there a mode change?). (2) Was `set_target()` called while in ACTIVE state, clearing the flag prematurely? (3) Did `tick()` reach the ramp-complete/hold-complete state where the pending flag is consumed? Three tasks, two priorities, one state machine — you can't single-step through it. |
| **Safety impact** | **Low.** The ESTOP states (ramp-to-zero, hold-then-silent) are safety-critical — exiting them prematurely would be dangerous. The current code correctly defers exit until the ramp/hold completes. But the distributed state machine makes it hard to verify this property statically. |
| **Maintainability** | **High.** Adding a new steering state or transition requires understanding which task owns which part of the state machine. There's no single owner. Compare with SYS's `BrakeControl` — called exclusively from `task_brake` at 50 Hz. |

### Architectural comparison

SYS `BrakeControl`:
```
task_brake (prio 3, 50 Hz) ──► g_brake.tick(lever, estop, brake_kpa, mode, seb_status_raw, seb_cmd)
```
Single caller, single priority, single rate. All state transitions happen inside `tick()`. Adding a new brake mode requires changing one function in one file.

RT `SteeringControl`:
```
t_dispatch  ──► exit_estop()
t_control   ──► set_target(), start_estop(), set_estop_hold_time()
t_can_tx_low ─► tick()
```
Three callers, two priorities, two rates. Adding a new steering mode requires checking all three call sites for interactions.

---

## Issue 8 — `SteeringControl`: 225-line header-only class

### Evidence

`steering_control.h` — entire implementation in the header. Key sections:

- 6-state enum + full state machine in `tick()` (lines 36-131, 96 lines)
- Ramp-to-zero with following-error recovery (lines 69-106, 38 lines)
- Hold-then-silent with pending-exit handling (lines 108-125, 18 lines)
- `set_target()` with ESTOP guard and `m_estop_exit_pending` clear (lines 135-142)
- `start_estop()` with dynamic limit clamping for hold angle (lines 146-166)
- `exit_estop()` with deferred-exit pattern (lines 187-192)
- `build_command()` with speed→km/h conversion and SYNTREE offset application (lines 195-209)

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **No.** The code is functionally correct. |
| **Debugging impact** | **High.** A header-only class means every change recompiles every translation unit that includes it. More importantly, you cannot set a breakpoint on a specific state transition — the entire state machine is one inline method. Stepping through `tick()` in GDB means stepping through all 6 states' logic even when you only care about one transition. |
| **Safety impact** | **Low.** The state machine has clear entry/exit conditions for each state. The ramp rate (20°/s) is a `constexpr` with clear derivation. |
| **Maintainability** | **Medium.** The ramp-step calculation `kSteerEstopRampDegS * 10.0f / 50.0f` requires the reader to mentally convert: degrees/s → 0.1° units/s → per-tick at 50 Hz. A comment like `// 20 deg/s → 200 (0.1°)/s → 4 (0.1°)/tick at 50 Hz` would help. The following-error-during-ESTOP check (lines 88-102) could be a private method `check_estop_following_error()`. |

### Recommendation

Move the implementation to `steering_control.cpp`. Extract the ESTOP following-error check into a private method. Add derivation comments for the ramp-step constant.

---

## Issue 9 — Dead code: `SEB_TAKEOVER`/`SEB_RELEASE` event types never enqueued

### Evidence

**`safety_monitor.h:31-36`** defines the event types:
```cpp
struct SafetyEvent {
    enum Type : uint8_t {
        ESTOP = 0,
        MODE_CHANGE,
        SEB_TAKEOVER,       // ← defined but never enqueued
        SEB_RELEASE         // ← defined but never enqueued
    };
    Type    type;
    uint8_t payload;
};
```

**`t_control` drain code** (`main.cpp:137-143`) handles them:
```cpp
case rt::SafetyEvent::SEB_TAKEOVER:
    m_seb_takeover = true;
    break;
case rt::SafetyEvent::SEB_RELEASE:
    m_seb_takeover = false;
    break;
```

**Searching all `xQueueSend(g_safety_evt_q, ...)` calls in the codebase:**

| File | Event type enqueued |
|------|-------------------|
| `can_dispatch.h:79` | `ESTOP` |
| `can_dispatch.h:97` | `ESTOP` |
| `can_dispatch.h:173` | `MODE_CHANGE` |

No code anywhere enqueues `SEB_TAKEOVER` or `SEB_RELEASE`. These event types and their handlers are dead code.

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **No**, but only because SEB takeover is handled through a different mechanism. The SEB takeover state is managed via `run_safety_checks()`'s `seb_takeover` in/out parameter (set to `true` on SYS heartbeat timeout at `safety_monitor.h:102`, set to `false` on recovery at `safety_monitor.h:105`). The dead event queue path is never reached. |
| **Debugging impact** | **Medium.** A developer searching for "how does SEB takeover activate?" will find: (a) the event type definition, (b) the event handler in `t_control`, (c) no enqueue site. They'll waste time tracing the dead path before discovering the real mechanism in `run_safety_checks()`. |
| **Safety impact** | **None.** The dead path is harmless — it would set `m_seb_takeover` to the same value already set by `run_safety_checks()`. |
| **Maintainability** | **Medium.** Dead code is a maintenance hazard. If someone later adds an enqueue of `SEB_TAKEOVER`, the event handler would set `m_seb_takeover = true`, but then `run_safety_checks()` would overwrite it (since it takes `seb_takeover` by reference and may set it to `false` on heartbeat recovery). Two competing writers for the same state variable. |

### Recommendation

Either: (a) remove the dead event types and handlers, documenting that SEB takeover is managed directly in `run_safety_checks()`, or (b) add enqueue calls in the SYS heartbeat timeout/recovery paths and remove the in/out parameter from `run_safety_checks()`. Option (a) is simpler and doesn't change behavior.

---

## Issue 10 — 10ms publication delay on SEB takeover state

### Evidence

In `t_control` (`main.cpp:123-307`), the loop structure is:

```cpp
while (1) {
    // PHASE 1: Drain events + publish LAST iteration's derived state
    // ... drain g_safety_evt_q → m_seb_takeover ...
    g_mode_current.store(m_current_mode);      // line 146
    g_seb_takeover.store(m_seb_takeover);      // line 147 ← PUBLISH HERE

    // PHASE 2: Compute THIS iteration's state
    // ... kinematics, safety checks ...
    rt::SafetyResult sr = run_safety_checks(..., m_seb_takeover);  // line 183
    // run_safety_checks MAY MODIFY m_seb_takeover

    // PHASE 3: No re-publish of m_seb_takeover
    // ... PID, telemetry, WDT, bus-off ...

    vTaskDelayUntil(&last, per);  // block until next 10ms boundary
}
```

`run_safety_checks()` sets `seb_takeover = true` at line 102 when SYS heartbeat times out. But this change is NOT published via `g_seb_takeover.store()` until the NEXT iteration (line 147 of the next loop). The consumer `t_can_tx_low` reads `g_seb_takeover.load()` at 50 Hz.

### Analysis

| Aspect | Assessment |
|--------|-----------|
| **Causes bugs?** | **Not functionally.** The delay is 10ms (one control tick). SYS heartbeat timeout is 200ms, so the additional latency is 5%. The SEB takeover brake command arrives at the SEB at most 10ms later than it could. |
| **Debugging impact** | **Low.** The publication delay is invisible unless you're timestamp-comparing CAN traces. The SEB takeover frame (0x7B9) appears 10ms later than expected. |
| **Safety impact** | **Very low.** At 50 Hz SEB command rate, each frame is 20ms apart. A 10ms delay on the first takeover frame means the SEB starts braking at t=30ms instead of t=20ms after heartbeat loss detection — negligible for a 200ms timeout. |
| **Maintainability** | **Low.** The publish-before-compute pattern is confusing (state from iteration N-1 published at start of iteration N), but it's a common FreeRTOS task pattern to ensure consistent published state at the start of each period. |

### Note

This is the least severe issue. The 10ms delay is architecturally acceptable. The pattern (publish at top of loop, compute during loop, publish next iteration) is a standard technique for consistent periodic task state. However, the code could be restructured to publish after `run_safety_checks()` returns, eliminating the delay and making the data flow more obvious.

---

## Cross-Cutting Observation — Obstacle→kPa duplicates `obstacle_limit` shape

### Evidence

**`physics_model.cpp:84-90` — speed limiter:**
```cpp
int32_t PhysicsModel::obstacle_limit(int32_t target_mmps, unsigned obstacle_mm) {
    if (obstacle_mm <= shared::kObstacleStopMM)  return 0;
    if (obstacle_mm >= shared::kObstacleClearMM) return target_mmps;
    float t = static_cast<float>(obstacle_mm - shared::kObstacleStopMM)
            / static_cast<float>(shared::kObstacleClearMM - shared::kObstacleStopMM);
    return static_cast<int32_t>(target_mmps * t);  // 0→full (factor t)
}
```

**`main.cpp:167-176` — kPa computer:**
```cpp
int32_t obs_kpa;
if (obs <= shared::kObstacleStopMM) {
    obs_kpa = shared::kObstacleMaxKpa;
} else if (obs >= shared::kObstacleClearMM) {
    obs_kpa = 0;
} else {
    float t = static_cast<float>(obs - shared::kObstacleStopMM)
            / static_cast<float>(shared::kObstacleClearMM - shared::kObstacleStopMM);
    obs_kpa = static_cast<int32_t>(shared::kObstacleMaxKpa * (1.0f - t));  // max→0 (factor 1-t)
}
```

Same domain `[300, 3000]mm`, same domain points, same linear interpolation. One scales 0→max (speed), one scales max→0 (kPa). The kPa version is the complement of the speed version: `kPa = MaxKpa × (1 - speed_scale_factor)`.

Not a bug. But two copies of the same interpolation logic with inverted output. Could be `PhysicsModel::obstacle_to_kpa(obs)` calling `obstacle_limit` internally.

---

## Summary — Impact Matrix

| # | Issue | Bugs? | Debugging | Safety | Maintain |
|---|-------|-------|-----------|--------|----------|
| 1 | `g_ses_angle_raw` name lies | No | **High** | Low | Medium |
| 2 | Follow-error unit round-trip | No | Medium | None | Low |
| 3 | `can_send_estop()` duplicated | Risk if diverges | Low | **Medium** | **High** |
| 4 | Two 0x7B9 blocks + separate counters | **Yes (counter gap)** | **High** | **Medium** | **High** |
| 5 | Monolithic 195-line `t_control` | No | **High** | Low | **High** |
| 6 | Safety state: events + atomics | No (design) | Medium | Low | Medium |
| 7 | Steering from 3 tasks | Possible (fragile) | **V.High** | Low | **High** |
| 8 | 225-line header-only SteeringControl | No | **High** | Low | Medium |
| 9 | Dead SEB_TAKEOVER event code | No | Medium | None | Medium |
| 10 | 10ms SEB takeover publish delay | No | Low | V.Low | Low |

### Priority for fixes

1. **Fix Issue 4 (rolling counter gap)** — this is a real bug that causes lost brake frames after takeover→recovery
2. **Fix Issue 9 (dead code removal)** — clean up to prevent future confusion
3. **Address Issue 1 (naming)** — single biggest debugging friction
4. **Address Issue 5 (monolithic task)** — extraction of bus-off monitor and obstacle→kPa
5. **Address Issue 7 (steering from 3 tasks)** — consolidate steering calls to one task
