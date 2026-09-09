# Verified Pipeline Bugs

Scope: Host → RT → SYS → MTR/SES/SEB control and ESTOP lifecycle.

## Severity 1 — functional safety or operator-visible failure

### BUG-01: RT rejects valid high-level commands after SYS authority reacquisition [RESOLVED]

- **Files**
  - `rt-esp32/src/safety_stream_loss.h`
  - `rt-esp32/src/rt_state.h`
  - `rt-esp32/src/can_dispatch.h`
  - `rt-esp32/src/main.cpp`
- **Symptom**
  - High-level drive/mode commands are accepted on the RT high bus but produce no motion, or only the *next* command works, after SYS 0x011 stream loss and recovery.
- **Bug**
  - `SafetyStreamSupervisor` returns to `ACQUIRED` after one new 0x011 frame, even when it is only a reacquisition baseline. `StreamValidity::observe()` remains invalid until a subsequent advancing frame.
  - RT then clears `g_no_sys_authority`, but mode/Host command stream validators still reject the first post-recovery frame. This creates an inconsistent, one-frame authority handoff.
- **Resolution**
  - Implemented multi-stream readiness bitmask (`g_ready_mask` with `READY_BIT_SAFETY`, `READY_BIT_MODE`, `READY_BIT_HOST`).
  - Cleared on stream loss, fault, or ESTOP to prevent pre-fault data reuse.
  - Set independently as each stream's validator confirms validity (parallel recovery without artificial sequencing).
  - Single barrier release: `g_no_sys_authority` is cleared only when `is_sys_authority_ready()` (`SAFETY` & `MODE`) is satisfied. Motion setpoints are gated until `is_motion_ready()` (`SAFETY`, `MODE`, and fresh `HOST`) is satisfied.


### BUG-02: [FIXED] RT suppresses motion silently when the low bus has no ACK-capable peer

- **Files**
  - `rt-esp32/src/main.cpp` lines 197–217
  - `rt-esp32/src/main.cpp` lines 289–314
  - `rt-esp32/src/can_driver_twai.cpp` lines 377–385
- **Symptom**
  - High-level commands reach RT, but no downstream command reaches SYS/MTR/SES/SEB and the Host is not told immediately why.
- **Bug**
  - RT blocks all low-bus traffic except 0x001 until it receives a known low-bus frame. At power-up with SYS, MTR, SES, or SEB absent, the blocking itself prevents the initial RT frames from reaching SYS. This can deadlock before SYS ever transmits.
  - The condition is only reported as a diagnostic, but no immediate Host-visible failure state or operator stop is raised.
- **Fix direction**
  - Decoupled CAN transport readiness from actuator readiness: removed global `peer_seen` low-bus TX suppression so discovery and heartbeat frames stream freely upon boot ($t=0$).
  - Actuator output commands (`0x204` drive commands, steering, brakes) are independently gated using node-specific readiness status (`MTR_READY`, `SES_READY`, `SEB_READY`).
  - Low-bus peer timeout remains monitored and reported via `RtLowCanPeerTimeout` over High CAN to the Host.

### BUG-03: [FIXED] SYS MTR ESTOP ACK timeout runs only once, even while the acknowledgment is still missing

- **Files**
  - `sys-esp32/src/main.cpp` lines 645–664
  - `sys-esp32/src/config.h` lines 100–101
  - `sys-esp32/src/mtr_estop_ack.h`
  - `sys-esp32/test/test_sys_qa/test_sys_qa.cpp`
- **Symptom**
  - MTR never acknowledges `ESTOP_ACTIVE`, but SYS samples 0x206 only once, then stops retrying or reporting the failed acknowledgment.
- **Bug**
  - SYS updates `g_motor_fault_flags` from every 0x206 frame, but the F3 check clears `g_last_estop_trigger_tick` unconditionally after one timeout period, regardless of whether `ESTOP_ACTIVE` was observed.
  - The check therefore has a single 100ms sampling opportunity. If the first 0x206 feedback frame after SYS triggers estop is delayed, dropped, or arrives before the MTR latch is set, SYS never retries the missing-ack diagnostic or retry action.
- **Fix direction**
  - Implemented `MtrEstopAckWatchdog` state machine tracking `pending`, `deadline`, and `retries_left` (`kMtrEstopAckMaxRetries = 3`).
  - Clears `pending = false` immediately on receipt of `0x206` with `ESTOP_ACTIVE` (sub-millisecond early clearance).
  - Performs bounded periodic re-transmissions on deadline expiration, and escalates to latched fault (`g_brake_fault_active`) only when retries are exhausted.

### BUG-04: MTR post-authority reacquisition timing is undefined (RESOLVED)

- **Files**
  - `rt-esp32/src/main.cpp` lines 345–500
  - `mtr-stm32/src/motor_manager.h` lines 84–105, 281–310, 501–504
  - `rt-esp32/src/safety_stream_loss.h`
- **Symptom**
  - Reset or command path works in one test but not in the full pipeline.
- **Bug**
  - RT’s 100 Hz control loop can resynchronize safety authority, and MTR requires a valid mode/power stream plus a fresh drive command watchdog state. However, the exact timing order is not atomic:
    - RT can clear `g_no_sys_authority` before SYS authority is fully acquired downstream.
    - MTR can invalidate mode/power independently.
    - RT’s `0x204` watchdog and mode/power checks have separate timeouts and state transitions.
  - These interdependent validation threads are not synchronized, leading to nondeterministic post-reset behavior.
- **Fix direction / Solution implemented**
  - Implemented multi-stream authority readiness latch in RT (`READY_BIT_SAFETY | READY_BIT_MODE | READY_BIT_HOST`).
  - Implemented multi-stream authority readiness bitmask latch in MTR (`MTR_READY_MODE | MTR_READY_POWER | MTR_READY_DRIVE`):
    - All 3 streams (`0x110` mode valid, `0x113` power valid and ON with REARM satisfied, `0x204` fresh drive setpoint) must be active and valid before propulsion is uninhibited.
    - Zeroes `mtr_ready_mask_` on full authority loss (ESTOP assert, safety stream timeout/CRC corruption, CAN link timeout, authorized clear).
    - Clears individual stream bits upon independent stream timeout/invalidation (`MTR_READY_DRIVE` cleared on 0x204 timeout, `MTR_READY_POWER` on power OFF/fault, `MTR_READY_MODE` on mode stream fault).
  - Gated motion setpoints and TX on complete readiness; RT sends `{0, N}` keep-alive frames on `0x204` during reacquisition so MTR's drive watchdog stays fed without triggering active torque before MTR finishes REARM.
  - Exposed `recovery_pending` in `RT_NODE_STATUS` (0x501) when SYS authority has recovered but fresh Host command is awaited.

## Severity 2 — command path and observability issues

### BUG-05: RT does not require a fresh Host command after an ESTOP clear (RESOLVED)

- **Files**
  - `rt-esp32/src/main.cpp`
  - `rt-esp32/src/can_dispatch.h`
  - `rt-esp32/src/safety_stream_loss.h`
- **Symptom**
  - RT may resume using the pre-ESTOP command expressed in its local `cmd` variable, when the Host has not yet sent a new command after the clear.
- **Bug**
  - During a safety trip, RT does overwrite `g_cmd_q` with `{0,0}`, which correctly forces a stop.
  - However, every control-tick iteration peeks that queue into the persistent local `cmd` variable, and `g_cmd_q` remains full with `{0,0}`. If the safety clear occurs immediately after the queue overwrite, RT continues to resolve `{0,0}` until the Host sends a new command, which is safe but lacks an explicit “fresh command required” state. It is therefore impossible to distinguish intentional zero motion from an idle post-clear state on the Host interface without knowing the safety-state transition.
  - This creates poor diagnosability and makes pipeline continuation depend on Host production timing.
- **Fix direction / Solution implemented**
  - On ESTOP assertion, stream loss, watchdog stale, and on `SAFETY_CLEAR`, `READY_BIT_HOST` is cleared in `g_ready_mask`, and `g_cmd_q` + local `cmd` are zeroed out.
  - Motion is gated until a fresh `0x300 HOST_DRIVE_CMD` frame arrives post-clear, setting `READY_BIT_HOST`.
  - Exposed in `RtNodeStatus` (0x501) with `recovery_pending = true` while waiting for the fresh Host drive command.

### BUG-06: missing SEB status at boot is treated differently from SEB loss while running [RESOLVED]

- **Files**
  - `sys-esp32/src/config.h`
  - `sys-esp32/src/inhibit_state.h`
  - `sys-esp32/src/inhibit_state.cpp`
  - `sys-esp32/src/main.cpp`
- **Symptom**
  - Vehicle is stopped until SEB recovers, but no immediate estop/reset cause is surfaced.
- **Bug**
  - SYS used `g_last_seb_status_tick = 0` as “never received,” overloading timestamp 0 with state meaning.
  - At startup with missing SEB, diagnostic staleness warnings were skipped, creating an observability asymmetry between boot-time missing SEB and runtime stream loss.
- **Fix direction / Solution implemented**
  - Decoupled SEB presence state from timestamps: added explicit `g_seb_seen` atomic boolean and `kSebStartupAcquireMs` (1000 ms) acquisition deadline.
  - During boot, traction remains inhibited (`kInhibitSebCommsLoss`) while waiting for SEB to boot.
  - If `kSebStartupAcquireMs` expires without receiving 0x721 (`!g_seb_seen`), traction remains inhibited and a startup acquisition failure warning is logged periodically.
  - At runtime once `g_seb_seen` is true, 0x721 gap exceeding `kSebStatusTimeoutMs` triggers the runtime staleness warning and sets `kInhibitSebCommsLoss`.
  - In both cases, receiving 3 consecutive valid frames clears `kInhibitSebCommsLoss` (hysteresis recovery).

### BUG-07: Full SES 0x202 L3 bit coverage is incomplete

- **Files**
  - `rt-esp32/src/can_dispatch.h` lines 281–305
- **Symptom**
  - Certain steering actuator L3 faults fail to trigger ESTOP.
- **Bug**
  - Code only checks angle and torque fault bits from `0x202`. The documented fault table includes additional L3 fault bits in bytes 0–3, including actuator/domain faults.
  - These unchecked L3 bits can be present without triggering the safety event.
- **Fix direction**
  - Implement a complete L3 fault mask derived directly from the protocol/DBC and add vectors proving every L3 bit trips ESTOP.

### BUG-08: MTR drive-command watchdog uses two different recovery criteria

- **Files**
  - `mtr-stm32/src/motor_manager.h` lines 84–105, 281–310
- **Symptom**
  - After temporary 0x204 loss, drive authority recovers at an unpredictable time.
- **Bug**
  - The watchdog uses `kDriveCmdRecoverMaxGapMs` to define “consecutive valid command” but applies it only after the first recovery frame is observed. The first frame can be stale, and the logic only checks the inter-frame gap, not actual stream freshness at the time of release.
  - This does not directly create unsafe motion because mode/power authority are still checked, but it makes back-to-back drive command time-sensitive and hard to reproduce.
- **Fix direction**
  - Reset both the recovery count and gap validity on every confirmed watchdog trip and require a fresh, contiguous sequence from the first frame after the trip.

### BUG-09: RT lacks a distinct Host-visible estop reason for every stop-producing state

- **Files**
  - `rt-esp32/src/main.cpp` lines 500–573
- **Symptom**
  - Host receives a stop but no detailed cause.
- **Bug**
  - RT resets `g_estop_reason` to `none` when the current mode is neither ESTOP nor a hard safety trip and steering is not disabled.
  - Several stop causes remain undefined or collapsed:
    - Host timeout and no SYS authority are zero-setpoint gates rather than explicit estop-latch reasons.
    - MTR feedback loss is reported through the diagnostic channel but not as a dedicated RT ESTOP reason.
    - ESTOP reasons reuse `CanEstop` and `Internal` for unrelated failure classes.
  - The Host therefore cannot always distinguish the reason it is stopped or whether an explicit operator reset is required.
- **Fix direction**
  - Assign every stop-producing state a distinct RT state-report reason, or add a dedicated stop-cause report rather than overloading `estop_reason`.

### BUG-10: [FIXED] estop reset is not observable through a dedicated Host command

- **Files**
  - `protocol/contracts/hmi.yaml` (0x114 `HOST_ESTOP_RESET_REQ`)
  - `protocol/contracts/sys.yaml` (0x115 `SYS_ESTOP_RESET_RSP`)
  - `protocol/contracts/network.yaml`
  - `protocol/contracts/baseline-manifest.json`
  - `rt-esp32/src/can_rx_router.h`
  - `sys-esp32/src/config.h`
  - `sys-esp32/src/inhibit_state.h`
  - `sys-esp32/src/mode_manager.h`
  - `sys-esp32/src/mode_manager.cpp`
  - `sys-esp32/src/main.cpp`
  - `sys-esp32/test/test_mode_manager/test_mode_manager.cpp`
- **Symptom**
  - Host cannot reliably clear an estop after diagnosing the root cause or inspect why a reset attempt is refused.
- **Bug**
  - Reset was only supported through physical switches (START button or 3s MODE long-press on SYS).
  - The Host had no dedicated, sequence-protected, authenticated reset command or pre-flight blocker visibility.
- **Resolution**
  - Implemented 1-Req / 1-Rsp architecture with blocker introspection:
    - Host sends `0x114 HOST_ESTOP_RESET_REQ` (`request_seq`, `reset_token = 0x5253`, `rolling_counter`).
    - RT acts as Route Only, forwarding `0x114` High $\to$ Low.
    - SYS acts as Sole Reset Authority via `get_estop_reset_blockers()`:
      - Rejects if hardware ESTOP button pressed (`kResetBlockPhysicalEstop`), active latched faults (`kResetBlockLatchedFault`), vehicle moving $> 50\,\text{mm/s}$ (`kResetBlockMoving`), heartbeat lost (`kResetBlockHeartbeatLoss`), MTR active estop (`kResetBlockMtrEstopActive`), or bad token/stale stream (`kResetBlockInvalidToken`).
    - If blockers == 0: executes `try_exit_estop_remote()` transitioning `ESTOP -> MANUAL` (power OFF), opens reset grace window (`sys::mark_estop_reset`), and replies `0x115 SYS_ESTOP_RESET_RSP` with `result = ACCEPTED (0)` and `blocker_mask = 0`.
    - If blockers != 0: remains in ESTOP and replies `0x115 SYS_ESTOP_RESET_RSP` with `result = REJECTED (1)` and active `blocker_mask`.
  - Preserved downstream safety invariants: RT and MTR clear only upon observing fresh advancing `0x011` clear frames, and MTR requires an explicit subsequent `0x113` power command (`OFF -> ON`) before rearming.
  - Exposed continuous pre-flight visibility via `0x500 SYS_NODE_STATUS` `block_mask`.


## Severity 3 — protocol and tooling issues

### BUG-11: Protocol baseline validation fails

- **Files**
  - `protocol/contracts/baseline-manifest.json`
  - `protocol/contracts/rt.yaml`
- **Symptom**
  - `python -m protocol.tools.protocol validate` fails.
- **Bug**
  - The frozen baseline does not include the current high-bus `RT_DIAG_EVENT_RPT` instance at ID 0x621, so validation reports:
    `normalized contract differs from frozen baseline; missing=[], extra=[('high', 1568)]`
- **Fix direction**
  - Re-freeze the baseline after review or revert the new protocol instance.

### BUG-12: Protocol C++ tests expect a C++17-capable compiler, but the machine default compiler is GCC 6.3

- **Files**
  - `protocol/tests/python/test_cpp_generated.py`
  - `protocol/tests/python/test_diagnostics_manager.py`
- **Symptom**
  - C++ protocol tests fail with `#include <string_view>` missing.
- **Bug**
  - Test selection assumes `g++` on PATH implements the C++17 standard library. The machine default points to GCC 6.3, which lacks `std::string_view`.
- **Fix direction**
  - Adjust tests to require C++17 capability and report a clear skip message, or prepend a known-good C++17 toolchain to `PATH`.
