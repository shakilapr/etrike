# Verified Pipeline Bugs

Scope: Host → RT → SYS → MTR/SES/SEB control and ESTOP lifecycle.

## Severity 1 — functional safety or operator-visible failure

### BUG-01: RT rejects valid high-level commands after SYS authority reacquisition

- **Files**
  - `rt-esp32/src/safety_stream_loss.h` lines 80–89
  - `rt-esp32/src/safety_monitor.h` lines 165–169
  - `rt-esp32/src/can_rx_router.h` lines 60–80
- **Symptom**
  - High-level drive/mode commands are accepted on the RT high bus but produce no motion, or only the *next* command works, after SYS 0x011 stream loss and recovery.
- **Bug**
  - `SafetyStreamSupervisor` returns to `ACQUIRED` after one new 0x011 frame, even when it is only a reacquisition baseline. `StreamValidity::observe()` remains invalid until a subsequent advancing frame.
  - RT then clears `g_no_sys_authority`, but mode/Host command stream validators still reject the first post-recovery frame. This creates an inconsistent, one-frame authority handoff.
- **Fix direction**
  - Make STREAM acquisition atomic across the 0x011 safety authority and downstream mode/command stream validators.
  - Resynchronize/reset the mode command validator and command watchdog state when safety authority transitions from `LOST` to `ACQUIRED`, or require the same reacquisition sequence everywhere.

### BUG-02: RT suppresses motion silently when the low bus has no ACK-capable peer

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
  - Add a bounded startup admission window before requiring ACK-positive peer discovery.
  - Surface low-bus transport unavailability as an immediate operator/Host fault state with a defined escalation path, not merely a diagnostic.

### BUG-03: SYS MTR ESTOP ACK timeout runs only once, even while the acknowledgment is still missing

- **Files**
  - `sys-esp32/src/main.cpp` lines 645–664
  - `sys-esp32/src/config.h` line 100
- **Symptom**
  - MTR never acknowledges `ESTOP_ACTIVE`, but SYS samples 0x206 only once, then stops retrying or reporting the failed acknowledgment.
- **Bug**
  - SYS updates `g_motor_fault_flags` from every 0x206 frame, but the F3 check clears `g_last_estop_trigger_tick` unconditionally after one timeout period, regardless of whether `ESTOP_ACTIVE` was observed.
  - The check therefore has a single 100ms sampling opportunity. If the first 0x206 feedback frame after SYS triggers estop is delayed, dropped, or arrives before the MTR latch is set, SYS never retries the missing-ack diagnostic or retry action.
- **Fix direction**
  - Keep the ack window open until either `ESTOP_ACTIVE` is observed or a bounded retry count expires, not until one time-based check has run.
  - Record acknowledgment state explicitly in SYS node status rather than deriving it only from instantaneous 0x206 fault bits.

### BUG-04: MTR post-authority reacquisition timing is undefined

- **Files**
  - `rt-esp32/src/main.cpp` lines 345–500
  - `mtr-stm32/src/motor_manager.h` lines 84–105, 281–310, 501–504
- **Symptom**
  - Reset or command path works in one test but not in the full pipeline.
- **Bug**
  - RT’s 100 Hz control loop can resynchronize safety authority, and MTR requires a valid mode/power stream plus a fresh drive command watchdog state. However, the exact timing order is not atomic:
    - RT can clear `g_no_sys_authority` before SYS authority is fully acquired downstream.
    - MTR can invalidate mode/power independently.
    - RT’s `0x204` watchdog and mode/power checks have separate timeouts and state transitions.
  - These interdependent validation threads are not synchronized, leading to nondeterministic post-reset behavior.
- **Fix direction**
  - Define and implement a state machine for the full authority reacquisition sequence across RT and MTR.
  - Expose the exact state as a Host-visible report so reset progress is observable.

## Severity 2 — command path and observability issues

### BUG-05: RT does not require a fresh Host command after an ESTOP clear

- **Files**
  - `rt-esp32/src/main.cpp` lines 345–500
  - `rt-esp32/src/can_dispatch.h` lines 445–465
- **Symptom**
  - RT may resume using the pre-ESTOP command expressed in its local `cmd` variable, when the Host has not yet sent a new command after the clear.
- **Bug**
  - During a safety trip, RT does overwrite `g_cmd_q` with `{0,0}`, which correctly forces a stop.
  - However, every control-tick iteration peeks that queue into the persistent local `cmd` variable, and `g_cmd_q` remains full with `{0,0}`. If the safety clear occurs immediately after the queue overwrite, RT continues to resolve `{0,0}` until the Host sends a new command, which is safe but lacks an explicit “fresh command required” state. It is therefore impossible to distinguish intentional zero motion from an idle post-clear state on the Host interface without knowing the safety-state transition.
  - This creates poor diagnosability and makes pipeline continuation depend on Host production timing.
- **Fix direction**
  - Reset RT’s local command state on an ESTOP clear and require a fresh `HOST_DRIVE_CMD` frame before marking the control path active, similar to the existing MTR rearm sequence.

### BUG-06: missing SEB status at boot is treated differently from SEB loss while running

- **Files**
  - `sys-esp32/src/inhibit_state.h` lines 7–60
  - `sys-esp32/src/main.cpp` lines 380–450, 900–930
- **Symptom**
  - Vehicle is stopped until SEB recovers, but no immediate estop/reset cause is surfaced.
- **Bug**
  - SYS uses `g_last_seb_status_tick = 0` as “never received,” and its staleness detector skips that case at startup.
  - A never-connected SEB therefore remains outside the estop latch and may be recoverable without the ESTOP reset transaction, while a connected-then-lost SEB becomes a transient traction inhibit. This allows boot behavior to depend on whether any SEB frame arrived before the fault.
- **Fix direction**
  - Define whether SEB is required before motion. If required, add a startup acquisition deadline independent of the “last frame seen” sentinel.

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

### BUG-10: estop reset is not observable through a dedicated Host command

- **Files**
  - `sys-esp32/src/mode_manager.cpp` lines 69–94
  - `sys-esp32/src/safety_monitor.cpp` lines 48–66
- **Symptom**
  - Host cannot reliably clear an estop after diagnosing the root cause.
- **Bug**
  - Reset is only supported through physical switches or long-press on SYS. The Host has no authorized, authenticated two-step reset command.
  - This is intentional from a safety perspective but leaves the Host unable to perform a demonstrably safe automated reset when the fault clears.
- **Fix direction**
  - Add a dedicated, sequence-protected Host reset command with clear cause-proven, confirmed-clear semantics.

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
