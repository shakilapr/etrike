# RT/SYS Pre-Vehicle Validation Gate

This document defines the minimum validation required before flashing RT and SYS
firmware onto a real vehicle. Passing host tests or compiling firmware is not
enough by itself. RT and SYS control steering, braking, mode authority, ESTOP,
and CAN gateway behavior, so validation must be staged from software-only tests
to bench hardware before any powered vehicle test.

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

No real vehicle upload should happen until the required gates below pass.

## Gate 1: Software And Firmware Build

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

RT host regression executables should also be rebuilt from source before running,
because stale `.exe` files can fail due to missing runtime dependencies.

Required RT host commands:

```powershell
cd rt-esp32\test
g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared .stale/test_qa_bugs.cpp ../src/physics_model.cpp -o test_qa_bugs.exe
g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared .stale/test_physics_0_0_4.cpp ../src/physics_model.cpp -o test_physics_0_0_4.exe
g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared .stale/test_pid_controller.cpp -o test_pid.exe
g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared .stale/test_heartbeat.cpp -o test_heartbeat.exe
g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared .stale/test_steering_control.cpp ../src/physics_model.cpp -o test_steering_control.exe

.\test_qa_bugs.exe
.\test_physics_0_0_4.exe
.\test_pid.exe
.\test_heartbeat.exe
.\test_steering_control.exe
.\test_signals.exe
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

## Gate 2: CAN Contract And Simulation

Validate RT/SYS frame contracts against `shared/can/can_protocol.h` and the CAN
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

Pass criteria:

| Check | Required result |
|-------|-----------------|
| All expected CAN IDs | present at expected rates |
| All safety frames | valid DLC, checksum, and rolling counter where applicable |
| All fault injections | deterministic safe state within documented timeout |
| Logs | CAN trace saved with git hash and firmware version |

## Gate 3: Bench HIL Without Vehicle Actuation

Flash RT and SYS onto real ESP32 boards on a bench CAN network. Do not connect
vehicle steering, brake, or motor actuators yet.

Required setup:

| Equipment | Purpose |
|-----------|---------|
| RT ESP32-S3 board | Production RT firmware |
| SYS ESP32-S3 board | Production SYS firmware |
| CAN analyzer, two channels preferred | Low and high bus logging |
| 12 V bench supply | MCU and CAN transceiver power |
| Oscilloscope or logic analyzer | ESTOP GPIO and CAN timing |
| Terminated CAN harness | 60 ohm measured between CANH and CANL |

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

Pass criteria:

All bench HIL tests must pass with CAN logs attached before connecting physical
actuators. Any unexpected reset, watchdog event, bus-off, duplicated ESTOP frame,
or unsafe actuator request is a stop condition.

## Gate 4: Actuator Bench

Connect actuators only on a bench setup where motion and hydraulic pressure can
be safely constrained and measured.

Required tests:

| Actuator | Required validation |
|----------|---------------------|
| EPS-C / SES | Align, zero angle, positive/negative angle, clamp, following-error fault |
| SEB | Align, zero stroke, pressure mode, stroke takeover, CAN-loss behavior |
| MTR or motor mimic | Zero torque on ESTOP, gear behavior, stale speed behavior |

Required safety checks:

| Fault | Required result |
|-------|-----------------|
| ESTOP while steering command active | Steering ramps or holds according to RT safety state |
| ESTOP while brake command active | Brake command goes to safe max or documented safe behavior |
| SEB CAN disconnected | Pressure behavior measured and documented |
| EPS-C CAN disconnected | Steering output behavior measured and documented |
| RT reset during AUTO | Outputs go safe and SYS detects loss |
| SYS reset during AUTO | RT brake takeover behavior verified |

Pass criteria:

Actuator motion and pressure must match command bounds. Any uncontrolled motion,
unexpected brake release, checksum rejection, alignment failure, or CAN-loss
behavior that contradicts the safety assumptions blocks vehicle upload.

## Gate 5: Vehicle Dry Run

Only start after Gates 1 through 4 pass.

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

## Gate 6: Low-Speed Field Test

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
| Build logs | RT and SYS native plus vehicle builds |
| Host test logs | RT and SYS test outputs |
| CAN traces | Bench HIL and actuator bench traces |
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
| `docs/integration-test-procedure.md` | Step-by-step hardware integration sequence |
| `docs/hil-safety-test-plan.md` | Detailed HIL safety scenarios and timing criteria |
| `docs/can-bench-test.md` | CAN bench setup guidance |
| `docs/firmware-flashing.md` | Firmware flashing procedure |
| `docs/estop.md` | ESTOP design and behavior |
| `docs/defense-in-depth-safety.md` | Safety architecture rationale |
