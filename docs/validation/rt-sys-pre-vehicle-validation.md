# RT/SYS Pre-Vehicle Validation Gate

This document defines the minimum validation required before using RT and SYS
firmware on a real vehicle. Passing host tests or compiling firmware is not
enough by itself. RT and SYS control steering, braking, mode authority, ESTOP,
and CAN gateway behavior, so validation is split into four phases:

| Phase | Scope | Hardware connected |
|-------|-------|--------------------|
| 1 | Software-only validation | None |
| 2 | Controller-only validation | RT and/or SYS controller boards only |
| 3 | Unit-by-unit validation | One external unit at a time |
| 4 | Full hardware integration | Complete vehicle hardware stack |

This is an engineering release gate, not a safety certification.

## Current Baseline

The current RT checks validate only a subset of behavior:

| Area | Current coverage | Gap |
|------|------------------|-----|
| RT physics model | Native host tests cover steering sign, low-speed yaw, clamps | Does not prove real-time task timing or CAN driver behavior |
| RT SEB request helper | QA test checks `align_enable = 1` | Does not prove SEB accepts frames on hardware |
| RT heartbeat encoding | Host test checks ID, DLC, counter, default health flags | Does not prove bus timing under load |
| RT steering ESTOP logic | Host test covers state behavior | Does not prove EPS-C follows commands physically |
| SYS framework | Native build target exists | Needs explicit SYS host, CAN, and HIL validation before vehicle upload |
| RT/SYS integration | Existing docs describe bench procedures | Needs execution with recorded CAN logs and pass/fail evidence |

No full vehicle integration should happen until Phases 1 through 3 pass.

## Phase 1: Software-Only Validation

All framework logic and CAN behavior must be tested with no hardware connected.
This phase proves code-level behavior, CAN contracts, checksums, rolling counters,
state machines, timeout logic, and fault handling before any controller board is
flashed or wired.

Validation is performed by a repeatable software gate: record the exact inputs,
build all required profiles, rebuild host tests from source, run deterministic
unit/component/integration/scenario tests, apply shared safety invariants, save
logs/traces/reports, and produce a single PASS/FAIL/BLOCKED/INCOMPLETE result.
Only PASS allows entry to Phase 2.

The same gate should run in CI. Fast checks run on every PR, broader RT/SYS/CAN
checks run on safety-sensitive PRs, full generated matrices and soak tests run
nightly, and a release-candidate CI gate produces the evidence bundle required
before controller-only testing.

### Builds And Host Tests

Run from a clean worktree or record the exact git hash and local diff used.

Required commands:

```powershell
cd rt-esp32
pio run -e native
pio run -e vehicle

cd ..\sys-esp32
pio run -e native
pio run -e vehicle
```

RT and SYS tests should be run using PlatformIO's unified testing framework.

Required test commands:

```powershell
cd rt-esp32
pio test -e native
pio test -e vehicle

cd ..\sys-esp32
pio test -e native
pio test -e vehicle
```

Pass criteria:

| Check | Required result |
|-------|-----------------|
| RT native build | success |
| RT vehicle build | success |
| SYS native build | success |
| SYS vehicle build | success |
| RT host tests | 0 failures |
| SYS host tests | 0 failures or documented missing coverage before proceeding |
| CAN signal tests | 0 failures |

### CAN Contract And Simulation

Validate RT/SYS frame contracts against `protocol/generated/cpp/protocol.h` and the CAN
dictionary before hardware is connected.

Required coverage:

| Scenario | Required checks |
|----------|-----------------|
| RT heartbeat `0x7FD` | DLC, counter increment, health flags, low/high bus behavior |
| SYS heartbeat `0x7FE` | DLC, counter increment, timeout visibility |
| ESTOP `0x001` | DLC 0, forwarded both directions, no echo loop |
| SYS mode command `0x110` | Manual, Auto, ESTOP transitions |
| RT state report `0x210` | Mode, safety state, stale command flags |
| RT drive command `0x204` | Speed clamp, gear, stale command zeroing |
| RT brake command `0x205` | Obstacle brake curve and zero command behavior |
| SES request `0x169` | Alignment enable, control enable, checksum, rolling counter |
| SEB request `0x7B9` | Alignment enable, control enable, pressure/stroke mode, checksum, rolling counter |

Fault injection required before bench hardware:

| Fault | Expected behavior |
|-------|-------------------|
| Host heartbeat missing | RT stops autonomous drive path and reports fault state |
| SYS heartbeat missing | RT brake takeover path sends safe SEB command if configured |
| RT heartbeat missing | SYS enters safe state and emits ESTOP/brake command |
| Frozen alive counter | Treated as heartbeat failure, not healthy communication |
| Stale drive command | RT zeros speed and suppresses unsafe actuator output |
| Bad checksum or rolling counter | Frame rejected or faulted according to receiver contract |
| CAN bus-off | Node recovers or enters safe state without uncontrolled output |

### Mode Coverage Matrix

Phase 1 must test every vehicle mode explicitly. A test that passes only in the
default startup mode does not prove the framework is safe in other modes.

Vehicle modes from `protocol/generated/cpp/protocol.h`:

| Mode | Value | Authority meaning |
|------|-------|-------------------|
| Manual | 0 | Human/manual control is authoritative; autonomous actuator commands must be silent or safe-zeroed |
| Auto | 1 | Host/RT autonomous command path may control steering/brake/motor within limits |
| Estop | 2 | Emergency state; all outputs must go to their safest available command |

Required SYS mode-manager tests:

| Scenario | Required result |
|----------|-----------------|
| Boot default | SYS starts in Manual, not Auto |
| Manual to Auto | Mode button release changes Manual to Auto after debounce rules |
| Auto to Manual | Mode button release changes Auto to Manual after debounce rules |
| Manual to Estop | ESTOP input forces Estop immediately |
| Auto to Estop | ESTOP input forces Estop immediately |
| Estop to Manual | START button or documented long-press exits only to Manual |
| Estop to Auto | Direct Estop to Auto transition is blocked |
| Rapid button bounce | Debounce prevents repeated unintended toggles |
| CAN-set Manual/Auto | Accepted only for allowed values |
| CAN-set Estop | Rejected unless the design explicitly permits software ESTOP through this path |
| Invalid mode value | Ignored or forced to safe state, never treated as Auto |

Required RT behavior per mode:

| RT input mode | Required RT behavior |
|---------------|----------------------|
| Manual | Suppress host autonomous drive, suppress steering request, suppress RT SEB auto request, report Manual |
| Auto | Accept fresh valid host drive/brake commands, apply physics/clamps, emit bounded actuator requests |
| Estop | Zero drive command, enter steering ESTOP behavior, send safe brake/takeover command if configured, report Estop/safety fault |
| Invalid mode | Treat as Manual or safe fault; never enable Auto outputs |
| Stale mode command | Hold last safe state or degrade to safe state according to timeout rules |

Required SYS behavior per mode:

| SYS mode | Required SYS behavior |
|----------|----------------------|
| Manual | Manual indicators active, manual gear/throttle path allowed if configured, autonomous commands suppressed |
| Auto | Auto indicator active, CAN-controlled path allowed, RT heartbeat required for autonomous operation |
| Estop | Brake lamp active, throttle/motor command cut, gear forced safe/neutral, brake command forced safe |
| Invalid mode | Ignored or forced safe; never drives Auto outputs |

Required RT/SYS interaction tests:

| Scenario | Required result |
|----------|-----------------|
| SYS sends Manual | RT state report follows Manual; RT does not emit autonomous actuator requests |
| SYS sends Auto | RT state report follows Auto only when safety preconditions are valid |
| SYS sends Estop | RT enters ESTOP behavior and reports ESTOP/safe state |
| RT reports Manual while SYS Manual | SYS status remains consistent; no false fault |
| RT reports Auto while SYS Auto | SYS allows Auto only while RT heartbeat and state are healthy |
| RT reports fault while SYS Auto | SYS exits or suppresses unsafe Auto path according to safety design |
| RT heartbeat lost in Auto | SYS transitions to safe/ESTOP response within timeout |
| SYS heartbeat lost in Auto | RT executes safe takeover behavior within timeout |
| Mode command out of order | System remains in the safest applicable state |

Required CAN-output checks by mode:

| Frame | Manual | Auto | Estop |
|-------|--------|------|-------|
| `0x110` SYS_MODE_CMD | mode=0 when commanded | mode=1 when commanded | mode=2 only through ESTOP path |
| `0x210` RT_STATE_RPT | reports Manual and safe state | reports Auto and active/safe state | reports Estop/fault/safe state |
| `0x204` RT_DRIVE_CMD | absent or speed=0 | bounded fresh speed/gear | speed=0 and safe gear |
| `0x205` RT_BRAKE_CMD | release or manual-safe value | bounded requested/obstacle brake | safe max or documented ESTOP brake |
| `0x169` VCU_SES_REQ | absent or safe hold/listen | bounded angle with valid checksum/counter | ramp/hold/silent ESTOP behavior |
| `0x7B9` VCU_SEB_REQ | release/zero unless takeover required | bounded pressure/stroke with alignment bit | safe takeover brake command |
| `0x7FD` RT_HEARTBEAT | health flags reflect Manual | mode_auto bit set | estop_active bit set |
| `0x7FE` SYS_HEARTBEAT | health flags reflect Manual | mode_auto bit set | estop_active bit set |

### Software-Only User Scenario Catalog

Phase 1 must include realistic user scenarios, not only isolated unit tests. The
software simulator should execute these scenarios with virtual RT, SYS, Host,
SES, SEB, MTR, and CAN buses. No physical hardware is needed; every external
unit can be represented by a deterministic CAN mimic.

Nominal driving scenarios:

| Scenario | Required checks |
|----------|-----------------|
| Start in Manual, no command | No autonomous actuator output; heartbeats and status continue |
| Switch Manual to Auto while stopped | RT accepts mode only after SYS command; no lurch |
| Move forward straight | Positive speed command produces bounded `0x204`, zero/near-zero steering |
| Move forward then stop | Speed ramps or commands to zero; brake behavior follows design; no stale speed remains |
| Move forward and take gentle left turn | Steering sign and magnitude are correct; no clamp unless expected |
| Move forward and take gentle right turn | Steering sign mirrors left-turn case |
| Hard turn at low speed | Steering bounded by low-speed hard limit; no rollover fault |
| Hard turn at high speed | Dynamic angle clamp limits steering to high-speed bound |
| Reverse straight | Reverse speed and gear are encoded correctly; no forward lurch |
| Reverse left/right turn | Reverse steering sign is correct and mirrored from forward behavior |
| Stop, reverse, then forward | Gear and speed transitions do not produce unsafe mixed commands |
| Brake while moving forward | Brake command is bounded; speed command and brake command do not conflict unsafely |
| Brake while turning | Steering remains bounded while brake applies; no unexpected steering sign flip |
| Obstacle far away | No brake assist beyond normal request |
| Obstacle approaching | Brake assist increases according to distance curve |
| Obstacle emergency distance | ESTOP or max safe brake path activates as designed |
| Auto to Manual while moving | Autonomous outputs stop or safe-zero within timeout |
| Manual to Auto while moving | Auto only takes authority when preconditions are valid |
| ESTOP while stopped | System enters ESTOP without bad frames or state corruption |
| ESTOP while moving straight | Speed command zeroes; brake safe command appears; heartbeats reflect ESTOP |
| ESTOP while turning | Steering ESTOP behavior uses ramp/hold/silent rules; brake safe command appears |
| ESTOP while reversing | Reverse command is cancelled; brake/neutral behavior is safe |
| Recover from ESTOP | Recovery exits only to Manual; Auto requires a separate command |

Command boundary scenarios:

| Input class | Values to test |
|-------------|----------------|
| Speed | 0, creep, nominal, max allowed, above max, negative reverse, below reverse max |
| Yaw rate | 0, small left/right, hard left/right, above limit, sign changes while moving |
| Brake pressure | 0, small, nominal, max allowed, above max, negative/invalid raw input |
| Obstacle distance | none, clear, assist threshold, emergency threshold, invalid negative, stale |
| Gear | N, D, R, invalid enum, gear change while speed nonzero |
| Mode | Manual, Auto, Estop, invalid enum, stale command, repeated command |
| Time | t=0 event, just before timeout, exactly at timeout, just after timeout, long soak |

### Connection, Disconnection, And Reconnection Scenarios

Phase 1 must simulate nodes and units connecting, disconnecting, freezing, and
reconnecting. A software-only disconnect means the mimic stops transmitting its
expected CAN frames or resumes after an absence; it does not require hardware.

Controller and host connection scenarios:

| Scenario | Required behavior |
|----------|-------------------|
| Host never connected | RT stays safe; no autonomous output without valid Host command/heartbeat |
| Host connects after boot | RT accepts Host only after valid heartbeat and command sequence |
| Host disconnects in Manual | No unsafe change; diagnostic state records Host missing if applicable |
| Host disconnects in Auto | RT performs assisted stop or safe fallback within timeout |
| Host reconnects after timeout | System does not jump back to Auto output without fresh valid command and mode authority |
| RT missing at SYS boot | SYS remains safe during startup grace, then faults if RT stays absent |
| RT connects after SYS boot | SYS accepts RT heartbeat and clears missing-RT state according to design |
| RT disconnects in Auto | SYS enters safe response and emits expected ESTOP/brake behavior |
| RT reconnects after fault | Recovery path requires explicit operator/mode action, not automatic Auto |
| SYS missing at RT boot | RT remains safe and does not depend on stale SYS mode data |
| SYS disconnects in Auto | RT brake takeover path activates within timeout if configured |
| SYS reconnects after fault | RT/SYS resynchronize mode and heartbeat before enabling Auto output |

External-unit connection scenarios:

| Unit | Disconnect behavior to test | Reconnect behavior to test |
|------|-----------------------------|----------------------------|
| SES/EPS-C | RT detects missing `0x201`; steering request enters safe state | Boot sync/listen resumes before steering is active |
| SEB | SYS/RT detects missing `0x721`; brake degraded or takeover behavior is safe | Alignment/sync required before normal brake control resumes |
| MTR | SYS detects missing motor feedback if modeled; throttle command remains safe | Gear/throttle resumes only after healthy feedback |
| PWT | Gateway/powertrain status missing is reported; no unsafe dependent output | Heartbeat/status restored before dependent control resumes |
| CAN bus high side | Host commands disappear; RT high-bus heartbeat/state behavior safe | Host path resumes only with fresh valid frames |
| CAN bus low side | RT/SYS communication fails safe; no stale cross-node mode is trusted | Heartbeats and mode handshakes re-establish before Auto output |

Connection fault variants:

| Variant | Required check |
|---------|----------------|
| Clean disconnect | Expected frames stop completely; timeout response occurs once |
| Intermittent disconnect | Repeated drop/recover cycles do not produce unsafe output or state oscillation |
| Frozen node | Same counter or same payload is treated as unhealthy, not connected |
| Reconnect with old counter | Receiver rejects or safely handles counter rollback/reuse |
| Reconnect with invalid mode | Invalid mode cannot enable Auto |
| Reconnect during ESTOP | ESTOP remains latched until explicit recovery action |
| Reconnect during braking | Brake command does not release unexpectedly |
| Reconnect during steering ramp | Steering state remains bounded and deterministic |

### CAN Fault And Data-Corruption Scenarios

These scenarios validate that every receiver treats malformed CAN as untrusted.

| Fault | Frames to apply to | Required behavior |
|-------|--------------------|-------------------|
| Bad DLC | `0x110`, `0x204`, `0x205`, `0x300`, `0x301`, `0x169`, `0x7B9`, heartbeats | Frame rejected or ignored; no unsafe output |
| Bad checksum | `0x169`, `0x7B9`, third-party status frames if checksummed | Frame rejected; stale/timeout logic eventually acts |
| Frozen rolling counter | Security-critical command/status frames | Treated as stale or faulted |
| Counter jump forward | Security-critical command/status frames | Accepted only if contract allows; otherwise faulted |
| Counter rollback | Security-critical command/status frames | Rejected or safe-handled |
| Unknown CAN ID | Both buses | Ignored unless explicitly routed by gateway policy |
| Valid ID on wrong bus | Low-only and high-only frames | Dropped or ignored; no authority leak between buses |
| Duplicate frame burst | Mode, ESTOP, drive, brake | Idempotent behavior; no repeated side effects |
| Frame reordering | Mode, heartbeat, command frames | Newer safe state wins; stale unsafe state cannot override |
| Delayed frame | Host drive/brake/mode | Timeout logic prevents delayed unsafe command reuse |
| Bit flip in mode byte | `0x110`, status reports | Invalid values cannot enable Auto |
| Bit flip in gear byte | Drive/motor frames | Invalid gear becomes neutral/safe or is rejected |

### Generated Scenario Matrix

The goal is not to hand-write only a few scenario tests. Phase 1 should include a
generated matrix that produces hundreds of deterministic cases from equivalence
classes. The exact count can change, but the release gate should report how many
cases ran and how many passed.

Recommended matrix dimensions:

| Dimension | Values |
|-----------|--------|
| Mode | Manual, Auto, Estop |
| Motion | stopped, forward creep, forward nominal, forward max, reverse creep |
| Steering | straight, gentle left, gentle right, hard left, hard right, sign change |
| Braking | none, normal brake, obstacle assist, ESTOP brake, brake release |
| Gear | N, D, R, invalid |
| Host state | absent, healthy, stale command, frozen heartbeat, reconnecting |
| RT state | healthy, missing heartbeat, frozen heartbeat, reconnecting |
| SYS state | healthy, missing heartbeat, frozen heartbeat, reconnecting |
| SES state | absent, booting, aligned, stale feedback, reconnecting |
| SEB state | absent, booting, aligned, stale feedback, reconnecting |
| CAN quality | clean, dropped frame, delayed frame, corrupt frame, duplicate frame |
| Event timing | before startup grace, during startup grace, before timeout, at timeout, after timeout |

Minimum generated Phase 1 scenario sets:

| Set | Minimum cases | Purpose |
|-----|---------------|---------|
| Nominal maneuver matrix | 100 | Forward/reverse/turn/brake/stop combinations across modes |
| Mode transition matrix | 60 | Transitions while stopped, moving, turning, braking, and faulted |
| Connection matrix | 80 | Host, RT, SYS, SES, SEB, MTR, PWT absent/disconnect/reconnect cases |
| CAN corruption matrix | 80 | Bad DLC, checksum, counters, bus placement, duplicate/delayed frames |
| Boundary-value matrix | 80 | Speed/yaw/brake/gear/obstacle/time limits and invalid values |
| Soak and sequence matrix | 20 | Long randomized but seeded sequences with invariant checks |

Every generated case must assert safety invariants, not just "no crash":

| Invariant | Must always hold |
|-----------|------------------|
| Auto authority | Auto actuator output is allowed only in Auto with fresh valid prerequisites |
| ESTOP priority | ESTOP overrides drive, steering, brake, mode, and reconnect events |
| Manual silence | Manual mode does not emit autonomous steering/drive/brake requests except documented safe-zero/release frames |
| No stale actuation | Stale command data cannot keep speed, steering, or brake authority alive |
| Bounded output | Speed, steering angle, pressure, stroke, gear, and mode stay within valid ranges |
| Valid security fields | Checksums, rolling counters, enable bits, and alignment bits are correct on outbound safety frames |
| No unsafe reconnect | Reconnected nodes do not immediately restore Auto output without fresh valid state |
| Deterministic recovery | Recovery from fault/ESTOP follows documented Manual-first path |
| Rate compliance | Required periodic frames stay within allowed virtual timing tolerance |
| Diagnostic visibility | Faults that affect safety appear in heartbeat health flags, status, or diagnostic output |

Pass criteria:

| Check | Required result |
|-------|-----------------|
| All expected CAN IDs | present at expected rates |
| All safety frames | valid DLC, checksum, and rolling counter where applicable |
| All fault injections | deterministic safe state within documented timeout |
| Logs | CAN trace saved with git hash and firmware version |

Phase 1 exit criteria:

| Check | Required result |
|-------|-----------------|
| RT framework logic | All available host/native tests pass |
| SYS framework logic | All available host/native tests pass, or missing tests are explicitly listed as blockers |
| CAN encode/decode | All RT/SYS command, status, heartbeat, ESTOP, SES, and SEB frames validated |
| State machines | Manual, Auto, ESTOP, recovery, stale-command, heartbeat-loss paths tested |
| Scenario catalog | Nominal maneuvers, mode transitions, braking, obstacle, disconnect, reconnect, and corruption cases tested |
| Generated matrix | Minimum scenario counts met and all safety invariants pass |
| Bypass behavior | Production, prototype, software, EPS, SEB, MTR, and build-flag bypass cases tested |
| Fault injection | Missing, frozen, malformed, stale, and delayed frames produce safe behavior |
| Evidence | Test logs saved with commit hash |

Detailed Phase 1 scope, missing tests, scenario matrices, invariants, current
blockers, and implementation roadmap are maintained in
`docs/validation/phase1-software-validation-details.md`.

## Phase 2: Controller-Only Validation

Flash RT and SYS onto real ESP32 boards on a bench CAN network. No external unit
is connected in this phase: no brake-by-wire, no steering unit, no motor unit,
no powertrain unit, and no vehicle actuator harness. CAN analyzers or simulators
may inject and observe frames.

Required setup:

| Equipment | Purpose |
|-----------|---------|
| RT ESP32-S3 board | Production RT firmware |
| SYS ESP32-S3 board | Production SYS firmware |
| CAN analyzer, two channels preferred | Low and high bus logging |
| 12 V bench supply | MCU and CAN transceiver power |
| Oscilloscope or logic analyzer | ESTOP GPIO and CAN timing |
| Terminated CAN harness | 60 ohm measured between CANH and CANL |

### RT Only

Power and test the RT controller alone.

| Test | Required result |
|------|-----------------|
| RT boot | Serial log normal, no watchdog reset, no brownout |
| RT low-bus heartbeat | `0x7FD` present with expected DLC, rate, counter, and health flags |
| RT high-bus heartbeat | `0x7FD` present on high bus if high bus is enabled |
| RT state report | `0x210` reports safe default mode and state |
| Host command injection | Simulated host frames are decoded and bounded correctly |
| Stale host command | RT suppresses unsafe output after command timeout |
| High-side ESTOP injection | RT forwards or reacts according to contract without duplicates |
| CAN bus load | RT keeps required rates without reset or bus-off |

### SYS Only

Power and test the SYS controller alone.

| Test | Required result |
|------|-----------------|
| SYS boot | Serial log normal, no watchdog reset, no brownout |
| SYS heartbeat | `0x7FE` present with expected DLC, rate, and counter |
| SYS safety status | `0x011` reports safe default state |
| Mode input | Mode button or simulated input produces correct `0x110` |
| ESTOP input | ESTOP button produces `0x001` within required latency |
| RT heartbeat missing | SYS detects missing RT and enters safe state as designed |
| Brake command generation | Any simulated brake command is bounded and safe |
| CAN bus load | SYS keeps required rates without reset or bus-off |

### RT And SYS Together

Connect RT and SYS only. Do not connect any other unit.

Required tests:

| Test | Required result |
|------|-----------------|
| Power-up | No overcurrent, 3.3 V rails in range, serial logs normal |
| CAN termination | 60 ohm on each active bus |
| RT-only boot | `0x7FD` present, expected DLC and rate |
| SYS-only boot | `0x7FE` and `0x011` present, expected DLC and rate |
| RT + SYS boot | Heartbeats independent, no bus errors |
| Mode button | SYS mode command reaches RT and RT report follows |
| ESTOP button | `0x001` appears on bus within required latency |
| High-to-low ESTOP | RT forwards emergency frame without duplication |
| Low-to-high ESTOP | RT forwards emergency frame without duplication |
| Heartbeat loss | Opposite node enters safe state within timeout |
| Bus load | Required frames still meet rate and latency under injected traffic |

Phase 2 exit criteria:

All RT-only, SYS-only, and RT+SYS controller-only tests must pass with CAN logs
attached before connecting any external unit. Any unexpected reset, watchdog
event, bus-off, duplicated ESTOP frame, or unsafe actuator request is a stop
condition.

## Phase 3: Unit-By-Unit Validation

Connect one external unit at a time on a bench setup where motion and hydraulic
pressure can be safely constrained and measured. Do not connect the full vehicle
stack yet. Each unit must be validated independently before combined operation.

Required tests:

| Unit | Required validation |
|------|---------------------|
| EPS-C / SES | Align, zero angle, positive/negative angle, clamp, following-error fault |
| SEB | Align, zero stroke, pressure mode, stroke takeover, CAN-loss behavior |
| MTR or motor mimic | Zero torque on ESTOP, gear behavior, stale speed behavior |
| PWT if used | Gateway behavior, heartbeat behavior, powertrain command bounds |

Unit test order:

| Step | Connected hardware | Required focus |
|------|--------------------|----------------|
| 1 | RT + SES only | Steering CAN contract, alignment, limits, ESTOP steering behavior |
| 2 | RT/SYS + SEB only | Brake command contract, alignment, pressure/stroke behavior, brake takeover |
| 3 | SYS/MTR or SYS + motor mimic only | Throttle cut, gear behavior, ESTOP motor behavior |
| 4 | RT/SYS + PWT only if applicable | Gateway forwarding, heartbeat loss, powertrain fault isolation |

Required safety checks:

| Fault | Required result |
|-------|-----------------|
| ESTOP while steering command active | Steering ramps or holds according to RT safety state |
| ESTOP while brake command active | Brake command goes to safe max or documented safe behavior |
| SEB CAN disconnected | Pressure behavior measured and documented |
| EPS-C CAN disconnected | Steering output behavior measured and documented |
| RT reset during AUTO | Outputs go safe and SYS detects loss |
| SYS reset during AUTO | RT brake takeover behavior verified |

Phase 3 exit criteria:

Actuator motion and pressure must match command bounds. Any uncontrolled motion,
unexpected brake release, checksum rejection, alignment failure, or CAN-loss
behavior that contradicts the safety assumptions blocks vehicle upload.

## Phase 4: Full Hardware Integration

Only start after Phases 1 through 3 pass. This phase connects the complete
hardware stack and then proceeds from constrained vehicle dry run to low-speed
field test.

### Vehicle Dry Run

Required conditions:

| Condition | Requirement |
|-----------|-------------|
| Vehicle state | Wheels lifted or mechanically constrained |
| Operator | One person at controls, one person at hard ESTOP |
| Logging | Full CAN log on low and high buses |
| Speed | No autonomous ground movement yet |
| Firmware | Exact git hash recorded |

Required tests:

| Test | Required result |
|------|-----------------|
| Manual boot | No autonomous actuator output |
| Manual ESTOP | All outputs safe |
| Auto enable with zero command | No lurch, no steering jump |
| Low-speed command with wheels lifted | Bounded steering, bounded throttle, brake available |
| Heartbeat loss during dry run | Safe state within timeout |
| Power cycle | Returns to Manual or safe default, not Auto |

Pass criteria:

No unexpected actuator command, no mode surprise, no stale command reuse, and no
CAN fault can remain unresolved before low-speed field testing.

### Low-Speed Field Test

Only start after vehicle dry run passes.

Required constraints:

| Constraint | Requirement |
|------------|-------------|
| Location | Closed area, no pedestrians or obstacles |
| Speed | Limited to walking speed for first run |
| ESTOP | Hardware ESTOP and software ESTOP both verified immediately before run |
| Operator | Safety operator able to cut power and brake |
| Logs | CAN, serial, and observed behavior recorded |

First-run checks:

| Test | Required result |
|------|-----------------|
| Straight low-speed command | No oscillation, no unintended acceleration |
| Gentle steering command | Correct direction in forward and reverse |
| Stop command | Vehicle stops within expected distance |
| ESTOP command | Vehicle enters safe state immediately |
| Fault injection if safe | Heartbeat loss or command stop produces safe behavior |

## Required Evidence Per Release

Before uploading to the real vehicle, collect:

| Artifact | Required content |
|----------|------------------|
| Git reference | Commit hash and dirty diff status |
| Build logs | RT and SYS native plus vehicle builds from Phase 1 |
| Host test logs | RT and SYS test outputs |
| CAN traces | Controller-only, unit-by-unit, and full-integration traces |
| Timing measurements | ESTOP, heartbeat timeout, gateway propagation |
| Fault-injection results | Pass/fail for each injected failure |
| Known limitations | Anything not tested or intentionally bypassed |

## Stop Conditions

Stop validation and do not proceed to the next gate if any of these occur:

| Stop condition | Reason |
|----------------|--------|
| Unexpected actuator motion | Vehicle safety cannot be assumed |
| Brake releases on comm loss without mitigation | Safety assumption invalid |
| ESTOP propagation misses timing target | Emergency path not proven |
| Heartbeat loss not detected | Distributed safety monitoring failed |
| Bus-off not handled | CAN reliability not proven |
| Firmware resets under bus load | Real-time behavior not stable |
| Checksum or rolling-counter rejection on actuator frames | CAN contract mismatch |
| Build uses undocumented local diff | Release cannot be reproduced |

## Related Documents

| Document | Purpose |
|----------|---------|
| `docs/testing-guide.md` | Project-wide software test commands |
| `docs/validation/phase1-software-validation-details.md` | Detailed Phase 1 software-only validation scope and roadmap |
| `docs/integration-test-procedure.md` | Step-by-step hardware integration sequence |
| `docs/hil-safety-test-plan.md` | Detailed HIL safety scenarios and timing criteria |
| `docs/can-bench-test.md` | CAN bench setup guidance |
| `docs/firmware-flashing.md` | Firmware flashing procedure |
| `docs/estop.md` | ESTOP design and behavior |
| `docs/defense-in-depth-safety.md` | Safety architecture rationale |
