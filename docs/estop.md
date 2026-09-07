# ESTOP: distributed emergency-stop design, signals & recovery

**Scope.** How emergency stop actually works across the etrike distributed
controllers, grounded in the current firmware (post safety-issue #2..#8 fixes).
Every ESTOP path, the CAN signals that carry it, the assert/latch/clear
semantics per node, and the reset sequences an operator must perform.

Firmware nodes referenced:

| Firmware | Board / MCU | Node | Role |
|----------|-------------|------|------|
| `sys-esp32` | ESP32-S3 | **SYS** | System safety authority; owns the ESTOP latch & `0x011` |
| `rt-esp32`  | ESP32-S3 | **RT**  | Realtime motion master; ESTOP latch + steering ramp + brake fallback |
| `mtr-stm32` | STM32G431 | **MTR** | Motor actuator; relays + DAC; ESTOP latch + REARM |
| `rm-esp32`  | ESP32 (classic) | **RM** | Operator RC / bench controller; link-loss ESTOP, isolated deployment |

See also: `docs/working-architecture.md` §6 (ESTOP) and §8 (safety-issue status),
`docs/can-architecture.md`.

---

## 1. Stop flavours and fault classes

There is **one** universal stop primitive — the `0x001` broadcast — but three
distinct stop *semantics* that controllers distinguish (see
`docs/working-architecture.md` §8 "Fault-class semantics"):

| Class | Meaning | Clears how | Examples |
|-------|---------|------------|----------|
| **Latched ESTOP** | vehicle must be explicitly re-armed | SYS reset path (START / MODE long-press) then the two-frame `0x011` clear + MTR REARM | hardware ESTOP, CAN `0x001`, SEB L3 (`0x721`/`0x731`), EGAS L2, bus-off, MTR-reported ESTOP, RT internal fault |
| **Fail-safe disable** (not latched) | auto-clears on next valid frame / confirmed recovery | N consecutive valid frames at cadence | MTR 500 ms any-frame deadman, MTR `0x204` drive-command watchdog (issue #2), RT `0x300` host-cmd stale |
| **B-class traction inhibit** (recoverable with confirmation) | clears after N healthy observations | confirmed recovery, own bit | SYS `kInhibitMtrFbkLoss` (issue #7), `kInhibitSebCommsLoss`, transient brake following-error (issue #5); RT MTR-health (issue #8) |

The latched-vs-recoverable split lives in SYS as two reason masks
(`sys-esp32/src/inhibit_state.h`):

- `g_inhibit_reasons` — transient / B-class (each detector owns its own bit)
- `g_latched_fault_reasons` — cleared **only** by the explicit reset path and
  only while the underlying cause is healthy

```
traction_inhibited = transient_inhibited() || latched_fault_present()
sys_estop_latched   = (ModeManager.mode() == Estop) || hardware_button
```

---

## 2. CAN signals that carry ESTOP

CAN ID / constants defined in `protocol/compat/can_protocol.hpp` (ids),
`protocol/generated/cpp/etrike_protocol.hpp` (payloads), and the SEB/SES
codecs in `protocol/codecs/`.

| Frame | CAN ID | DLC | Direction | Signal(s) | Meaning |
|-------|--------|-----|-----------|-----------|---------|
| `SAFETY_ESTOP` | `0x001` | 0 | **broadcast** (any node → all) | *(none)* | Immediate emergency stop trigger. Any node may originate; every receiver latches. |
| `SYS_SAFETY_STS` | `0x011` | 5 | SYS → RT, MTR (Low); SYS → Host (High) | byte0 `estop_active` (8-bit, `SysSafetySts::EstopActiveMeta`), byte1 `heartbeat_ok`, byte2 lights, byte3 `rolling_counter`, byte4 `e2e_crc` | **Persistent ESTOP authority.** `estop_active` reflects the *system* latch (`mode==Estop \|\| hw_button`, issue #4), CRC-8 (poly 0x2F, Data-ID 0x3C11) over bytes [0..3]. 5 Hz / 200 ms. |
| `SYS_MODE_CMD` | `0x110` | 2 | SYS → RT, MTR | `mode` (0=MANUAL,1=AUTO), `rolling_counter` | Mode authority. ESTOP is *not* encoded here — `0x110` is clamped to MANUAL during ESTOP. |
| `SYS_PWR_CMD` | `0x113` | 2 | SYS → MTR | `power_state` (0=OFF,1=ON), `rolling_counter` | Power authority. OFF during ESTOP and while any traction inhibit is active. |
| `SYS_HEARTBEAT` | `0x7FE` | 2 | SYS → RT | byte0 `alive_ctr`, byte1 bit1 `estop_active` (`SysHeartbeat::EstopActiveMeta`), bit2 `mode_auto`, bit3 `can_ok`, bits4-7 task-health | Redundant ESTOP latch echo + RT liveness watch. |
| `MTR_MOTOR_FBK` | `0x206` | 4 | MTR → RT, SYS, Host | `fault_flags` byte3: bit0 `kMtrFaultEstopActive` (0x01), bit1 `kMtrFaultCmdTimeout` (0x02), bit4 ready | MTR ESTOP **ack** (redundant path if SYS missed the `0x001`), plus command-timeout flag (issues #2/#7). `actual_speed_mmps` is the *echoed* command (not a measurement). |
| `RT_STATE_RPT` | `0x210` | — | RT → SYS/Host | `estop_reason` (0=normal, 1..10 reasons), `safety_state`, `steer_state`, task-health | Reports *why* RT stopped and whether it considers itself in an internal ESTOP. |
| `RT_DRIVE_CMD` | `0x204` | 5 | RT (or RM) → MTR | `motor_speed_mmps`, `gear` | The propulsion command. Zeroed by every stop flavour; MTR watchdogs it separately (issue #2). |
| `VCU_SEB_REQ` | `0x7B9` | 8 | SYS → SEB (sole normal producer) | stroke / pressure command, `rolling_counter`, XOR8 | Brake command. RT is **only** an emergency fallback writer (issue #3). |
| `RT_BRAKE_CMD` | `0x205` | — | RT → SYS | `brake_pressure_kpa` | Brake *intent*; SYS converts to the final `0x7B9`. |
| `SYS_DIAG_RPT` | `0x600` | 8 | SYS → RT, Host | `mode`, `brake_fault`, `heartbeat_ok`, `estop_active` (mode-based) | Diagnostic view of SYS stop state. |

`0x001` rate limiting — `shared/shared_config.h:26-33`:
`kEstopBroadcastMinIntervalUs = 250 ms` between `0x001` broadcasts **per ECU**
(`shared::should_send_estop_now`). SYS additionally rate-limits inbound `0x001`
to max 2 per 500 ms window (`sys-esp32/src/config.h:102-103`,
`kEstopRateLimitWindowMs`/`kEstopRateLimitMax`).

---

## 3. SYS — the ESTOP authority (owns the latch)

Firmware: `sys-esp32` (ESP32-S3). Key files: `src/main.cpp`, `src/mode_manager.{h,cpp}`,
`src/safety_monitor.h`, `src/brake_control.h`, `src/inhibit_state.h`.

### 3.1 ESTOP entry paths → `ModeManager::force_estop()`

All of these latch `ModeManager` into `Mode::Estop`
(`sys-esp32/src/mode_manager.cpp:70`). File:line of each trigger:

| # | Trigger | Source | main.cpp ref |
|---|---------|--------|--------------|
| 1 | Hardware ESTOP button (GPIO1, active-high NC) | `task_safety` reads GPIO → `g_safety.set_estop()` → `estop_triggered` | `:551-577` (trigger gate `:565-569`) |
| 2 | CAN `0x001` received (rate-limited RX) | dispatch `kIdSafetyEstop` | `:332-353` (force at `:348`) |
| 3 | SEB `0x731` ErrInfo — any of 16 L3 bits | dispatch `kIdSebErrInfo` | `:486-513` (force at `:507`) |
| 4 | SEB `0x721` `error_status==3` (L3) — latched `kLatchedSebL3` (issue #5) | dispatch `kIdSebStatus` | `:355-380` (force at `:373`) |
| 5 | Persistent brake following-error → `kLatchedBrakeFollowing` (issue #5) | dispatch following-error monitor | `:410-456` (force at `:449`) |
| 6 | RT heartbeat `0x7FD` lost (>1000 ms, after 3 s startup grace) | `task_safety` (`estop_triggered`) | `:565-577` (force at `:569`) |
| 7 | MTR reports ESTOP (0x206 `fault_flags` bit0) — redundant propagation | dispatch `kIdMtrMotorFbk` | `:301-319` (force at `:315`) |
| 8 | MTR ESTOP-ACK timeout (ESTOP sent, no ack bit in 100 ms) | `task_safety` | `:618-631` (force at `:624`) |
| 9 | EGAS L2: `|0x204 setpoint − 0x206 actual| > 500 mm/s` > 500 ms (AUTO) | `task_safety` | `:585-615` (force at `:601`) |
| 10 | CAN bus-off persistent (≥ 5 counts) | `task_can_control` | `:1073-1083` (force at `:1078`) |

On each entry SYS **broadcasts `0x001`** (`send_estop_frame`, rate-limited via
`can_send_estop()`), records `g_last_estop_trigger_tick`, and latches the mode.

### 3.2 Publish — the system latch on `0x011` and `0x7FE`

```
// src/main.cpp:203-204  (issue #4)
sys_estop_latched() = ModeManager::estop_latched(g_mode_mgr.mode(),
                                                 g_safety.estop_active())
                 ==   (mode == Mode::Estop) || hardware_button_active
```

- `task_can_tx` (5 Hz) builds `0x011`: `message.estop_active = sys_estop_latched()`
  then fills the E2E CRC over bytes [0..3] before encoding — `main.cpp:994-1105`
  (`message.estop_active = sys_estop_latched()` at `:996`).
- `task_hb` (10 Hz) builds `0x7FE`: `message.estop_active = sys_estop_latched()`
  — `main.cpp:1100`.

Because `estop_active` now tracks the **mode latch**, a software ESTOP
(`0x001`, SEB L3, EGAS, bus-off, MTR-reported-ESTOP) keeps `estop_active == 1`
even with the physical button released — so MTR/RT cannot two-frame-clear into
a false all-clear while SYS is still latched.

### 3.3 Authority clamp while stopped

`task_mode` (10 Hz) publishes `0x110`/`0x113` through
`sys::resolve_authority(mode_is_estop, resolved_auto, power_requested)`
(`sys-esp32/src/inhibit_state.h:77-84`):

```
stop = mode_is_estop || any_inhibit()          // any transient/latched inhibit
0x110.mode    = stop ? MANUAL : (AUTO if resolved_auto)
0x113.power   = stop ? OFF    : power_requested
```

So during ESTOP (or any traction inhibit) MTR sees MANUAL + power OFF. The
brake task (`task_brake`, 50 Hz) always drives the final `0x7B9` (sole normal
producer, issue #3): ESTOP ⇒ max stroke 27 mm, lever ⇒ 15 mm, else `0x205` kPa
intent or released (`src/brake_control.h` `build_command` priority order,
`brake_control.h:85-136`).

### 3.4 Reset / recovery

```
// operator actions only — src/mode_manager.cpp:15-68, config.h:87-103
exit ESTOP -> MANUAL:
  1. START button (GPIO41) falling edge          // mode_manager.cpp:39-48
  OR MODE button (GPIO11) held 3 s long-press    // mode_manager.cpp:19-35 (Gap #11)
CAN 0x111/0x110 NEVER clear a latched ESTOP.     // mode_manager.cpp:78 (set_from_can guard)

after exit, task_mode clears each latched fault whose cause is healthy:
  kLatchedSebL3           -> cleared when SEB error_status < 3
  kLatchedBrakeFollowing  -> cleared when a fresh 0x721 is present
  // main.cpp task_mode reset block (:720-746)
Only after SYS mode leaves ESTOP does 0x011/0x7FE publish estop_active == 0,
which is what lets RT/MTR run their confirmed two-frame clears below.
```

---

## 4. RT — ESTOP latch + steering ramp + brake fallback

Firmware: `rt-esp32` (ESP32-S3). Key files: `src/can_dispatch.h`,
`src/safety_monitor.h`, `src/safety_stream_loss.h`, `src/can_health.h`,
`src/main.cpp`, `src/brake_fallback.h`.

### 4.1 Assert (event-driven latch)

`can_dispatch.h` enqueues `rt::SafetyEvent::ESTOP` for:

- inbound `0x001` (`can_dispatch.h:139-154`) — forwarded cross-bus, reason
  `kEstopReasonCanEstop` (0x5, `rt-esp32/src/config.h:20`);
- `0x011.estop_active == 1` (`can_dispatch.h:182-190`) — authoritative, reason
  CAN-ESTOP; validated by E2E CRC + `StreamValidity` (700 ms);
- SES `0x202` L3 angle/torque fault (`:275-290`), SEB `0x721` `error_status==3`
  (`:343-349`) — reason `kEstopReasonInternal`;
- Low CAN bus-off (`can_health.h:38-62`, reason `kEstopReasonBusOff`) and High
  CAN bus-off ≥ 5 (`can_health.h:101-103`).

`t_control` drains the event queue into `m_estop_pending`/`m_estop_reason`
(`main.cpp:359-402`), and a **fail-safe**: loss of the `0x011` stream itself
keeps/sets the latch — never silently clears it (`safety_stream_loss.h`,
`main.cpp:398-402`; `last_rx_us == 0` at startup is NOT a loss).

### 4.2 Reaction (per control cycle, 100 Hz)

```
// run_safety_checks (src/safety_monitor.h) returns SafetyResult
if estop_pending or mode==Estop:
    zero_setpoints = true      // 0x204 -> {0, N}
    brake_kpa     = MAX (5000)
    disable_steering = true    // steering ESTOP ramp to 0 deg

// t_control (main.cpp:513-533): on zero_setpoints it overwrites g_cmd_q with
// zero, and rate-limited broadcasts 0x001 on BOTH buses; steering ramps via
// g_steering.start_estop().
```

`SafetyResult.estop_reason` is published in `0x210 RT_STATE_RPT`
(`main.cpp:728`). Reasons (`rt-esp32/src/config.h:15-25`): 1 Button, 2 Heartbeat,
3 FollowingError, 4 Obstacle, 5 CanEstop, 6 BusOff, 7 Internal, 8 EgasMismatch,
9 StaleCmd, 10 Watchdog.

### 4.3 Clear — asymmetric two-frame via `0x011`

```
// can_dispatch.h:192-223  (ssts_latched already true)
// An authorized clear requires TWO consecutive zero frames whose rolling
// counters advance by exactly +1 (mod 256). A duplicate / gap / jump restarts
// the sequence from that frame as the new baseline. Continuous zeros while NOT
// latched never accumulate clear credit (normal running never clears/REARM).
fresh 0x011 estop_active == 0:
    first_zero (not latched-just-before)  -> baseline (clear_confirm = 1)
    advancing (counter == last_zero + 1)  -> ++clear_confirm
    duplicate / gap / jump               -> baseline again
    clear_confirm >= 2                    -> ssts_latched = false
                                            enqueue SAFETY_CLEAR
                                            g_steering_exit_request = true  // :217-222
```

SAFETY_CLEAR clears `m_estop_pending`/`m_estop_reason` (`main.cpp:391-392`).
Steering/internal ESTOP is additionally released by a fresh Host drive command
(`g_steering_exit_request`, `can_dispatch.h:452`). **Because of issue #4, SYS only
sends those two advancing zero frames after it has itself been reset out of ESTOP.**

### 4.4 MTR-health watchdog (issue #8)

`MtrHealthSupervisor` (`src/safety_monitor.h`) — in AUTO past the AUTO-entry
grace, `0x206` stale > 200 ms ⇒ MTR unavailable ⇒ `zero_setpoints` even at
standstill; max brake only if a non-zero drive was recently commanded; confirmed
3-frame recovery; disabled by `g_bypass_mtr_absent`.

### 4.5 Emergency brake fallback (issue #3)

RT is **not** a normal `0x7B9` producer. `SebBrakeFallback`
(`src/brake_fallback.h`) only makes RT the emergency `0x7B9` writer when SYS
heartbeat is lost **and** SYS `0x7B9` has actually disappeared from the Low bus
for a guard interval:

```
NORMAL
  | SYS 0x7FE lost (motion already zeroed)  + fallback armed (startup acquisition)
  v
SYS_DEGRADED   (RT does NOT write 0x7B9; SYS brake task may still be alive)
  | SYS 0x7B9 absent > kSebFallbackGuardMs
  v
EMERGENCY_FALLBACK  -> RT asserts 0x001 (rate-limited) + transmits max-brake 0x7B9
  | SYS 0x7FE healthy again
  v  (handback is latched + epoch-guarded: stop RT 0x7B9, open epoch,
     count kSebHandbackVerifyFrames fresh POST-epoch SYS 0x7B9 frames)
NORMAL   // vehicle still ESTOP-latched until SYS 0x011 two-frame clear
```

---

## 5. MTR — motor actuator latch + REARM

Firmware: `mtr-stm32` (STM32G431, superloop @ 5 ms). Key file:
`src/motor_manager.h`, `src/config.h`, `src/can_driver.h`.

### 5.1 Assert

```
// handle_frame (motor_manager.h:56-155)
0x001 received                       -> trigger_estop()          // A1 (:72-73)
0x011.estop_active == 1 (valid CRC)  -> trigger_estop()          // A2 (:182-185)

trigger_estop():  estop_active_ = true; speed=0; gear=N;
                  relays Off; DAC force_zero()                   // :391-402
```

### 5.2 Fail-safe gate in tick (5 ms)

```
// tick() (motor_manager.h:318-327)
if (estop_active_ || comms_timed_out_ || drive_cmd_timed_out_ ||
    !power_valid_ || !safety_state_valid_ || !rearm_ok):
    target=0; relays Off; DAC force_zero(); return
```

Non-ESTOP, recoverable disables also feed this gate:
- any-frame deadman 500 ms (`config.h:41` `kWatchdogTimeoutMs`)
- `0x011` stream stale > 700 ms (`motor_manager.h:504` `kSafetyFreshMs`; expiry
  at `:256-265`) / CRC error / counter fault (`:161-179`)
- `0x110`/`0x113` stale (500 ms, `StreamValidity`)
- dedicated `0x204` drive-command watchdog, 150 ms while `drive_expected`
  (issue #2, `:271-315`) — releases only after 3 valid frames at cadence

`0x206.fault_flags` reflects ESTOP (bit0) and any command timeout (bit1):
`motor_manager.h:420-432` (flag at `:426-427`).

### 5.3 Clear — two-frame `0x011` then REARM

```
// handle_safety_status (motor_manager.h:158-227)
fresh advancing 0x011 estop_active == 0:
    frame 1 -> baseline (clear_confirm=1)
    frame 2 -> if clear_confirm>=2 && estop_active_: authorized_clear()

authorized_clear():  estop_active_=false; invalidate 0x110/0x113 authority;
                     rearm_required_ = true                         // :229-245

// REARM — motion may NOT resume until (0x113 OFF->ON edge) with a fresh
// 0x110 seen and 0x011 still valid (motor_manager.h:134-146, :260-267)
0x113 OFF       -> rearm_off_seen_ = true
0x113 ON (after) + mode_valid_ + safety valid -> rearm_observed_ = true
ignition_on_ = power_valid_ && power_state_on_ && safety_state_valid_
               && (!rearm_required_ || rearm_observed_)
```

So after a latched MTR ESTOP the operator must: clear SYS ESTOP (START /
long-press), let SYS publish two `estop_active==0` frames, then MTR needs a
power OFF→ON cycle on `0x113` with valid `0x110` (RM/SYS re-arms it) before the
relays/DAC may energise again.

---

## 6. RM — bench / isolated controller (link-loss deadman)

Firmware: `rm-esp32` (ESP32 classic, FlySky RC). Key file: `src/main.cpp`,
`src/config.h`. RM is connected **only** when SYS/RT/Host are absent — it is the
sole authority in bench/manual and authors `0x110`/`0x113`/`0x011` itself
(`main.cpp:195-241`).

```
// 50 Hz RC loop (main.cpp:78-130), config.h:59 kSignalLossTimeoutMs = 100
estop_latched   = g_can_estop_latched            // external 0x001 latch (:37)
signal_lost     = no fresh RC edge on CH0/1/2/4/5 within 100 ms
estop_or_signal_loss = signal_lost || estop_latched                       // :97

if signal_lost (rising edge):
    broadcast 0x001 once                          // main.cpp:100-114
    (loopback consumed via 50 ms credit window, :252-276)

outputs while stopped:
    0x204   gated off  -> MTR 500 ms watchdog trips          // main.cpp:171-193
    brake   0x7B9 max stroke 27 mm                             // :148-166
    0x110 mode  MANUAL; 0x113 power OFF                        // :195-225
    0x011 estop_active = estop_or_signal_loss  (+E2E CRC)      // :227-241

latch clear (main.cpp:88-95):
    valid link + Ignition OFF + Gear Neutral  -> g_can_estop_latched = false
external 0x001 (not own loopback) -> g_can_estop_latched = true  // :273-276
```

---

## 7. End-to-end sequences

### Assert — hardware button
```
operator presses ESTOP (SYS GPIO1)
  -> SYS task_safety: force_estop(), broadcast 0x001      (main.cpp:565-577)
  -> 0x001 latches: MTR (A1), RT (ESTOP event), RM (if present)
  -> SYS publishes 0x110=MANUAL, 0x113=OFF, 0x011.estop_active=1
  -> MTR relays/DAC off; RT zeros 0x204 + steering ramp + max brake 0x7B9 via SYS
```

### Assert — software (e.g. SEB L3 via `0x731`)
```
SEB L3  -> SYS dispatch force_estop() + 0x001 broadcast (main.cpp:507-513)
  same downstream effects as above;
  + kLatchedSebL3 bit set so a reset is refused until SEB is healthy
```

### Clear / re-arm — full chain
```
1. SYS: START button or MODE 3 s long-press  -> mode MANUAL
   (task_mode clears kLatchedSebL3 / kLatchedBrakeFollowing if healthy)
2. SYS now publishes 0x011/0x7FE estop_active == 0 (consecutive frames)
3. RT: two fresh 0x011 zero frames with advancing counters -> SAFETY_CLEAR (latch released)
4. MTR: two fresh 0x011 zero frames (StreamValidity-valid) -> authorized_clear(); requires REARM
5. SYS/RM sends 0x113 OFF->ON (+ fresh 0x110) -> MTR rearm_observed_
6. drive may resume when 0x204 is fresh and AUTO + steering active
```

---

## 8. How to reset an ESTOP (operator / bench playbook)

> **Golden rule.** ESTOP is a *distributed latch*. It is cleared per-node, in a
> fixed order, and **only by the operator**, never by a CAN mode/power command.
> If the underlying cause is still present, the node will re-latch immediately.
> Do the physical-cause check FIRST (button released? SEB healthy? MTR alive?
> RC link restored?), then clear in this order.

### 8.1 The fixed order (all nodes)

```
  ┌─ 0. Underlying causes clear (see 8.2 per-cause) ──────────────┐
  │                                                              │
  │  SYS ESTOP latch        -> START button, or MODE hold 3 s     │
  │    (mode_manager.cpp:15-68; clears latched faults at :720+)   │
  │        │  SYS publishes 0x011/0x7FE estop_active == 0         │
  │        ▼                                                      │
  │  RT latch                -> two fresh 0x011 zero frames,      │
  │                            counters advance +1 each          │
  │    (can_dispatch.h:192-223 -> SAFETY_CLEAR, main.cpp:391-392) │
  │        │                                                     │
  │        ▼                                                      │
  │  MTR latch               -> two fresh 0x011 zero frames       │
  │    (motor_manager.h:182-199 -> authorized_clear :229)         │
  │        ▼                                                      │
  │  MTR REARM               -> 0x113 OFF->ON edge + valid 0x110  │
  │    (motor_manager.h:134-146 rearm_off_seen_/rearm_observed_)  │
  │        ▼                                                      │
  │  resume                  -> fresh 0x204 + AUTO + steer active │
  └───────────────────────────────────────────────────────────────┘
```

### 8.2 Per-node reset procedures

**SYS (the authority)** — `sys-esp32`, `src/mode_manager.cpp:15-68`.
Clearing SYS ESTOP also clears each latched fault whose cause is now healthy
(`main.cpp:720-746`):

| Cause | How to clear at SYS |
|-------|---------------------|
| Hardware ESTOP button | Release the button (NC: press = ESTOP). Then **START** button, or **MODE** hold **3 s**. |
| CAN `0x001` from a peer | Remove the source (RT/MTR/RM still ESTOP-ing? clear them first). Then **START** / MODE 3 s. |
| SEB L3 (`0x721`/`0x731`) | Repair SEB so `0x721 error_status < 3`; reset is **refused while L3 is active** (`main.cpp:729-733`). Then START / MODE 3 s. |
| Persistent brake following-error | Restore SEB stroke tracking (fresh `0x721` in Stroke mode). Reset refused while cause active. Then START / MODE 3 s. |
| RT heartbeat `0x7FD` lost | RT must be powered/healthy again. Then START / MODE 3 s. |
| MTR ESTOP-ack timeout | MTR must ACK `0x206` bit0. Then START / MODE 3 s. |
| EGAS L2 / bus-off | Clear the speed mismatch / restore the bus. Then START / MODE 3 s. |

After START/long-press, `task_mode` clears `kLatchedSebL3` /
`kLatchedBrakeFollowing` only if healthy, and `0x011`/`0x7FE` begin publishing
`estop_active == 0`.

**RT** — `rt-esp32`. RT clears its latch automatically once SYS publishes two
consecutive fresh `0x011` `estop_active == 0` frames whose counters advance
(+1 each, `can_dispatch.h:192-223`).
If the cause was a steering/internal fault, it also needs a fresh Host drive
command (`g_steering_exit_request`). RT is never cleared "by itself" first —
always clear SYS first.

**MTR** — `mtr-stm32`. MTR needs (a) two consecutive fresh `0x011`
`estop_active == 0` frames (validated by `StreamValidity`) →
`authorized_clear()` (`motor_manager.h:182-199`, `authorized_clear` at `:229`),
and (b) a **REARM**: a `0x113` power OFF→ON edge observed after the clear, with
valid `0x110` and `0x011` (`motor_manager.h:134-146, :260-267`). In production
SYS/RM performs the OFF→ON when it re-powers; in bench, RM does it when you move
the ignition switch OFF then ON. Until REARM completes, `ignition_on_` stays
false and relays/DAC stay off even though the ESTOP latch is gone.

**RM (bench)** — `rm-esp32`. RM's own latch (`g_can_estop_latched`) clears only
via the **RC reset sequence**: valid RC link **+ Ignition OFF + Gear Neutral**
(`main.cpp:88-95`). If RM broadcast `0x001` due to RC signal loss, restoring the
RC link ends the deadman automatically; the latched-stop flag still needs the
Ignition-OFF + Gear-N sequence. External `0x001` (not RM's own loopback) also
sets the latch and needs the same reset.

### 8.3 Symptom → action quick table

| Symptom (what you see) | What is latched | Action |
|------------------------|-----------------|--------|
| Green "ready" bulb off, red ESTOP bulb on (SYS) | SYS mode ESTOP | Release button / fix cause → **START** or **MODE 3 s** |
| MTR relays/DAC never re-energise after SYS cleared | MTR ESTOP + not REARMed | Verify two `0x011` zero frames arrived, then **power cycle `0x113` OFF→ON** (ignition switch) |
| RT keeps zeroing `0x204` after SYS cleared | RT latch not cleared | Confirm SYS is publishing two consecutive advancing `0x011` zero frames (issue #4 keeps them 1 until SYS reset) |
| Vehicle won't move, no ESTOP bulb | Traction inhibit (MTR-fbk / SEB-comms / following) | Fix the recoverable cause (MTR feedback, SEB `0x721`) → auto-clears after N healthy frames |
| RM won't drive after an ESTOP | RM `g_can_estop_latched` | Valid RC link + **Ignition OFF + Gear Neutral** |
| Latch returns immediately after reset | Underlying cause still present | Do the cause check first; a latched fault refuses reset while active (`main.cpp:720-746`) |

### 8.4 What does NOT clear an ESTOP

- `0x110`/`0x111` CAN mode commands — `ModeManager::set_from_can` refuses to
  leave ESTOP (`mode_manager.cpp:78`; CAN never clears a latched ESTOP).
- `0x113` power ON alone — power is gated behind the ESTOP/inhibit clamp
  (`resolve_authority`, `inhibit_state.h:77-84`).
- A single `0x011` `estop_active == 0` frame — the asymmetric clear needs two
  consecutive fresh frames, counters advancing (+1) at RT
  (`can_dispatch.h:192-223`) and `StreamValidity`-valid at MTR
  (`motor_manager.h:182-199`).
- Power-cycling a **consumer** (RT/MTR/RM) does not clear the **SYS** latch —
  SYS is the authority and must be reset by button.
- Bench "solo mode" (`g_bench_solo_mode`) suppresses missing-peer faults but
  **never** the physical ESTOP button.

---

## 9. Verification

- MTR native suite (host, `mtr-stm32/test/test_mtr_full_suite.cpp`): 0x011
  two-frame asymmetric clear, REARM-across-clear, CRC/counter fail-safes,
  0x204 watchdog, SYS→MTR estop cut.
- Native-test harness (`native-test/`): `estop_latch`, `remediation_fixes`,
  `sys_inhibit_state`, `rt_safety_checks`, `rt_safety_monitor`,
  `rt_brake_fallback`, `rt_safety_stream_loss` equivalents.
- Cross-node ESTOP consistency is pinned by `sys-esp32/test/test_rt_sys_integration`
  and the RT-consuming half (native) — `0x011`/`0x7FE` `estop_active` must track
  the SYS latch (issue #4).

---

## 10. Known limits (still open)

- **No hardware IWDG/WWDG** on MTR/SYS — a firmware hang can leave outputs
  energized (`mtr-stm32/Core/Inc/stm32g4xx_hal_conf.h:49,68`).
- **`0x001` carries no sender/reason/epoch** — persistent truth is
  reconstructed from `0x011`/`0x210`/`0x206`.
- **SEB command-loss behaviour** is not proven (brake hold/release on `0x7B9`
  silence) — relevant to the emergency-fallback handback gap (issue #3) and the
  deferred single-source-ID (option 3) target.
- SYS `0x011.estop_active` reflects mode+button; the physical ESTOP button is
  never bypassable even in bench/solo mode (`g_bench_solo_mode` only suppresses
  missing-dependency faults).
