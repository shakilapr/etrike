# Commissioning and Test Profiles for RT and SYS

## Status

This document is a proposed development and validation architecture. The
profiles and sessions described here are not yet implemented unless explicitly
identified as existing behavior.

These profiles are engineering controls for bench testing. They are not a
safety certification and do not make an incomplete test setup safe by
themselves. Physical restraints, guarded test rigs, current-limited supplies,
and an independent emergency power disconnect remain mandatory.

## Purpose

RT and SYS need controlled ways to test one controller, sensor, or actuator at
a time before the complete vehicle is connected. A single general-purpose
"debug mode" is not appropriate because it can unintentionally enable several
outputs or bypass several missing-unit checks at once.

This proposal defines:

- standard terminology for software-only, controller, component, subsystem,
  and vehicle tests;
- build profiles that determine where an artifact is permitted to run;
- narrowly scoped commissioning sessions for RT and SYS;
- independent selections for control strategy and feedback source;
- physical and software authorization required before actuation;
- output allowlists, timeouts, reduced limits, and evidence requirements;
- a staged path from isolated component testing to vehicle commissioning.

The objective is to make an unsafe combination difficult to select and obvious
in firmware logs and CAN diagnostics.

## Existing Configuration

The current firmware has three system run modes in
[`shared/system_mode.h`](../shared/system_mode.h):

| Value | Existing name | Existing intent |
|---|---|---|
| `0` | Production | Real hardware with safety checks enforced |
| `1` | Hardware bench | Physical I/O; a developer jumper can enable peer bypasses |
| `2` | CAN/software bench | Missing peers and physical inputs may be simulated |

RT and SYS currently use the following broad runtime flags:

- `g_bench_solo_mode`
- `g_bypass_eps_sync`
- `g_bypass_seb_sync`
- `g_bypass_mtr_absent`

In software-bench mode, and in hardware-bench mode when the override pin is
active, several bypasses are enabled together. That is useful for early
simulation but is not sufficiently specific for physical actuator
commissioning. For example, a steering test should not implicitly bypass brake
or motor supervision.

The new design should not add more meanings to `SYSTEM_RUN_MODE`. Build
profiles, commissioning sessions, control strategies, and feedback sources
should be represented separately.

## Industry Terminology

| Term | Meaning in this project |
|---|---|
| Software-in-the-loop (SIL) | RT, SYS, actuators, sensors, and CAN buses are simulated in software |
| Controller bench test | A real RT or SYS board is tested with no physical actuator connected |
| Component bench test | An EPS-C, SEB, motor controller, or sensor is tested directly by a test station |
| Hardware-in-the-loop (HIL) | A real RT or SYS board runs against simulated peer ECUs and a simulated plant |
| Hardware-software integration test | One real controller is connected to one real actuator or sensor subsystem |
| Commissioning session | A restricted, temporary session that permits a defined engineering operation |
| I/O checkout | Individual inputs and outputs are observed or actuated to verify wiring and polarity |
| Open-loop characterization | A bounded command is applied without feedback control while feedback is recorded |
| Shadow control | A controller such as PID is calculated and logged but cannot affect the output |
| Closed-loop commissioning | Feedback control is enabled with reduced authority and additional abort conditions |
| Fault injection | Missing, stale, malformed, delayed, or electrically faulted behavior is introduced deliberately |
| Subsystem integration | Multiple previously accepted components are tested together on a constrained rig |
| Vehicle commissioning | The assembled vehicle is tested in controlled stages after lower-level gates pass |
| End-of-line test | A repeatable manufacturing test for every assembled production unit |

Physical actuator rigs are referred to as component or subsystem benches in
this document. HIL is reserved for tests where the controller is real and the
external unit or vehicle dynamics are simulated.

## Configuration Model

Four independent dimensions are required. Combining all four into one enum
would create ambiguous modes and an unsafe number of combinations.

### 1. Firmware Profile

The firmware profile is selected at build time and cannot be elevated at
runtime.

| Profile | Physical I/O | Physical actuation | Intended use |
|---|---:|---:|---|
| `VEHICLE_RELEASE` | Enabled | Normal vehicle rules | Vehicle validation and operation |
| `COMMISSIONING` | Enabled | Only through an authorized session | Controller, component, and subsystem benches |
| `HIL` | Enabled where required | Inhibited | Real controller with simulated peers and plant |
| `SIL_NATIVE` | Simulated | Not possible | Native tests and software simulation |

`VEHICLE_RELEASE` must not contain a path that opens a commissioning session.
A commissioning artifact must identify itself as non-roadworthy at boot and in
its build/capability manifest.

### 2. Commissioning Session

A session selects the single test purpose currently authorized. Only one
session may be active on a controller at a time.

Examples are `RT_STEERING_COMMISSIONING`, `SYS_BRAKE_COMMISSIONING`, and
`SYS_OUTPUT_CHECKOUT`. The complete session catalogs are defined below.

### 3. Control Strategy

| Strategy | Output behavior |
|---|---|
| `OUTPUT_INHIBITED` | Commands are decoded/calculated and logged but physical or actuator-CAN output is blocked |
| `OPEN_LOOP` | A bounded direct command is applied; feedback does not modify the command |
| `SHADOW` | The normal/open-loop command is applied while a feedback controller runs for telemetry only |
| `CLOSED_LOOP_LIMITED` | Feedback control is applied with reduced authority and stricter limits |
| `CLOSED_LOOP_NORMAL` | Normal validated controller authority; permitted only in approved integration profiles |

### 4. Feedback Source

| Source | Meaning |
|---|---|
| `PHYSICAL` | Data is read from the installed physical sensor or actuator status frame |
| `SIMULATED` | Data is supplied by an approved deterministic simulator |
| `UNAVAILABLE` | No feedback exists; the session must not claim feedback-dependent coverage |

`UNAVAILABLE` is not equivalent to healthy zero feedback. Code and diagnostics
must distinguish the two.

### 5. Sensor Acquisition and Use

Sensor acquisition and sensor use are separate decisions. The RT encoder
subsystem is enabled or disabled as one immutable build feature. The installed
channel set comes from the reviewed hardware map; individual channels are not
user-facing PlatformIO switches. Enabled channel data can be used for telemetry,
supervision, or closed-loop control as defined by the selected feedback source.

| Acquisition state | Meaning |
|---|---|
| `DISABLED_BY_CONFIG` | The encoder subsystem is intentionally disabled and is not required by the selected operating configuration |
| `ENABLED` | The subsystem initializes channels present in the approved hardware map |
| `FAULTED` | The channel was enabled but failed initialization, plausibility, freshness, or runtime health checks |

| Use role | Meaning |
|---|---|
| `NOT_USED` | The sensor has no authority and is not required for the selected configuration |
| `TELEMETRY_ONLY` | Measurements are reported but do not affect actuation or safety decisions |
| `SUPERVISION` | Measurements monitor an open-loop command and can reduce or remove authority on a fault |
| `CLOSED_LOOP` | Measurements are an input to the active feedback controller and are safety-required |

`DISABLED_BY_CONFIG` is a supported capability, not a fault. `FAULTED` is a
runtime failure and must produce the response defined for the selected
configuration. Diagnostics must never collapse disabled, missing, stale,
faulted, and valid-zero into one value.

### 6. PID State

PID execution and PID output authority are independently observable.

| PID state | Meaning |
|---|---|
| `DISABLED` | PID is reset and does not calculate or affect the command |
| `SHADOW` | PID calculates telemetry, but its output cannot affect actuation |
| `ENABLED_LIMITED` | PID affects actuation with commissioning limits and additional abort conditions |
| `ENABLED_NORMAL` | PID affects actuation with the approved vehicle limits |

The full system must support a documented encoder-subsystem-disabled, PID-disabled,
open-loop operating configuration. It must also support encoder-enabled,
PID-disabled operation for acquisition, telemetry, and open-loop supervision.
Closed-loop performance, encoder-based overspeed detection, and
encoder-dependent diagnostics are unavailable when their required encoders are
disabled; the capability manifest and operating constraints must say so.

## Separation from Vehicle Modes

The normal vehicle modes remain `Manual`, `Auto`, and `Estop`. They describe
vehicle authority and safety state. They do not authorize engineering tests.

A commissioning session is a separate state and must never be selected through
the normal mode command. Entering or leaving a commissioning session must not
cause an automatic transition into `Auto`.

When a commissioning session ends, times out, faults, or the controller resets:

1. All session-authorized outputs return to their defined safe state.
2. The session authorization is cleared.
3. A new physical and operator authorization is required.
4. The controller does not resume the previous command automatically.

## Component Bench Sessions

Component acceptance should happen before the component is connected to RT or
SYS. The test station must use generated typed codecs and the same golden
vectors as production firmware.

| Session | Connected hardware | Required coverage |
|---|---|---|
| `EPS_COMPONENT_BENCH` | EPS-C, constrained steering rack, CAN test station | Boot/alignment, center, direction, angle scaling, slew, limits, status, faults, counters/checksum, command loss, power cycling |
| `SEB_COMPONENT_BENCH` | SEB, guarded hydraulic rig, pressure gauge, CAN test station | Boot/alignment, zero stroke, bounded stroke/pressure, release, pressure hold, status, faults, counters/checksum, command loss, power cycling |
| `MOTOR_COMPONENT_BENCH` | Motor controller, unloaded motor or dynamometer, test station | Enable, direction, throttle mapping, zero torque, gear behavior, feedback, command loss, power cycling |
| `ENCODER_COMPONENT_BENCH` | Encoder and acquisition hardware | Channel polarity, quadrature phase, counts per revolution, speed conversion, noise, disconnect, and maximum input rate |

Component sessions do not validate RT or SYS behavior. They establish that the
unit and its documented wire contract behave as expected.

## RT Commissioning Sessions

### RT Session Catalog

| Session | Physical equipment | Permitted output class | Purpose |
|---|---|---|---|
| `RT_CONTROLLER_BENCH` | RT and CAN analyzer only | Bench CAN traffic; actuator commands observed but physically disconnected | Test the complete RT board with simulated Host, SYS, EPS-C, SEB, and MTR |
| `RT_PASSIVE_MONITOR` | RT on an isolated bus | No actuator commands | Validate receive paths, decoding, timing, bus statistics, and diagnostics |
| `RT_GATEWAY_TEST` | RT and two CAN analyzers | Generated forwarding allowlist only | Validate high/low routing, latency, load, bus isolation, and duplicate suppression |
| `RT_STEERING_COMMISSIONING` | RT and EPS-C only | Steering request only | Validate RT-to-EPS-C alignment, angle commands, limits, following error, and ESTOP response |
| `RT_BRAKE_TAKEOVER_TEST` | RT and SEB only | RT SEB request only | Validate autonomous brake command and SYS-loss takeover behavior |
| `RT_SENSOR_CHECKOUT` | RT and selected physical sensors | No actuator commands | Validate raw readings, direction, scaling, noise, plausibility, and disconnect behavior |
| `RT_OPEN_LOOP_CHARACTERIZATION` | RT, motor path, encoder, constrained plant | Bounded drive command only | Characterize command-to-speed behavior while recording encoder data |
| `RT_CONTROL_SHADOW` | RT, motor path, encoder, constrained plant | Validated open-loop command | Run PID calculations without allowing PID to alter the command |
| `RT_CLOSED_LOOP_COMMISSIONING` | RT, encoder, constrained motor rig | Reduced drive authority only | Tune and validate PID with strict limits and abort conditions |
| `RT_FAULT_INJECTION` | RT and deterministic simulator | Output inhibited unless a procedure explicitly authorizes one output | Test stale/missing Host and SYS, malformed frames, counters, bus-off, reset, and reconnect |

### RT Output Allowlists

The implementation must derive concrete CAN IDs and DLCs from the generated CAN
contract. The table describes behavior, not a second hand-maintained protocol
definition.

| Session | Steering | Brake | Drive/motor | Gateway | Other outputs |
|---|---:|---:|---:|---:|---:|
| `RT_CONTROLLER_BENCH` | Observe only | Observe only | Observe only | Test harness controlled | Inhibited |
| `RT_PASSIVE_MONITOR` | Inhibited | Inhibited | Inhibited | Inhibited | Inhibited |
| `RT_GATEWAY_TEST` | Inhibited | Inhibited | Inhibited | Allowlisted | Inhibited |
| `RT_STEERING_COMMISSIONING` | Reduced authority | Inhibited | Inhibited | Inhibited except required status | Inhibited |
| `RT_BRAKE_TAKEOVER_TEST` | Inhibited | Reduced authority | Inhibited | Inhibited except required status | Inhibited |
| `RT_SENSOR_CHECKOUT` | Inhibited | Inhibited | Inhibited | Inhibited | Inhibited |
| `RT_OPEN_LOOP_CHARACTERIZATION` | Inhibited | Safety stop only | Reduced authority | Inhibited | Inhibited |
| `RT_CONTROL_SHADOW` | Inhibited | Safety stop only | Reduced open-loop authority | Inhibited | Inhibited |
| `RT_CLOSED_LOOP_COMMISSIONING` | Inhibited | Safety stop only | Reduced closed-loop authority | Inhibited | Inhibited |
| `RT_FAULT_INJECTION` | Inhibited by default | Inhibited by default | Inhibited by default | Procedure-specific | Inhibited |

The steering session should use finite routines such as center, small positive
angle, center, and small negative angle. It should not accept unrestricted Host
drive commands.

## SYS Commissioning Sessions

### SYS Session Catalog

| Session | Physical equipment | Permitted output class | Purpose |
|---|---|---|---|
| `SYS_CONTROLLER_BENCH` | SYS and CAN analyzer/load box | Bench CAN and protected low-power outputs | Test the complete SYS board with simulated RT, SEB, and MTR |
| `SYS_PASSIVE_MONITOR` | SYS on an isolated bus | No actuator commands | Validate receive paths, decoding, timing, bus statistics, and diagnostics |
| `SYS_INPUT_CHECKOUT` | SYS and switches/sensors | No actuator commands | Validate ESTOP, mode, start, brake lever, gear sense, and debounce behavior |
| `SYS_OUTPUT_CHECKOUT` | SYS and protected dummy loads | One selected low-power output at a time | Validate lamps, relays, polarity, current, and inactive state |
| `SYS_BRAKE_COMMISSIONING` | SYS and SEB only | SYS SEB request only | Validate normal brake authority, lever path, pressure/stroke mapping, and degraded behavior |
| `SYS_MTR_SUPERVISION_TEST` | SYS and MTR or MTR simulator | Safety/monitoring path only | Validate command/feedback comparison, gear disagreement, stale feedback, and ESTOP acknowledgement |
| `SYS_RT_MIMIC_TEST` | SYS and simulated RT | No physical actuator output | Validate mode authority, RT heartbeat loss, stale drive command, and recovery |
| `SYS_POWER_SEQUENCE_TEST` | SYS and protected loads | Safe-state outputs only | Validate boot, brownout, reset, and power-order behavior |
| `SYS_FAULT_INJECTION` | SYS and deterministic simulator | Inhibited unless explicitly authorized | Test missing/frozen RT, SEB, MTR, malformed frames, bus-off, and reconnect |

### SYS Output Allowlists

| Session | SEB brake | Motor/gear | Lamps/relays | Mode authority | Other outputs |
|---|---:|---:|---:|---:|---:|
| `SYS_CONTROLLER_BENCH` | Observe only | Observe only | Dummy loads only | Simulated | Inhibited |
| `SYS_PASSIVE_MONITOR` | Inhibited | Inhibited | Inhibited | Observe only | Inhibited |
| `SYS_INPUT_CHECKOUT` | Inhibited | Inhibited | Status indication only | Observe/test inputs | Inhibited |
| `SYS_OUTPUT_CHECKOUT` | Inhibited | Inhibited | One output at reduced duty/time | Manual only | Inhibited |
| `SYS_BRAKE_COMMISSIONING` | Reduced authority | Inhibited | Brake indication only | Commissioning authority | Inhibited |
| `SYS_MTR_SUPERVISION_TEST` | Safety stop only | Commands observed or simulated | Status indication only | Simulated | Inhibited |
| `SYS_RT_MIMIC_TEST` | Inhibited | Inhibited | Dummy loads only | Simulated RT interaction | Inhibited |
| `SYS_POWER_SEQUENCE_TEST` | Safe state only | Safe state only | Procedure-specific | Manual/safe only | Inhibited |
| `SYS_FAULT_INJECTION` | Inhibited by default | Inhibited by default | Procedure-specific | Procedure-specific | Inhibited |

## Brake Validation Decomposition

The complete brake path cannot be accepted with one general brake mode. RT and
SYS have different brake responsibilities, so the following gates are
independent:

| Gate | Connected hardware | Authority under test |
|---|---|---|
| Brake component acceptance | SEB and test station | SEB internal behavior and wire contract |
| SYS brake integration | SYS and SEB | Normal/manual brake authority and monitoring |
| RT autonomous brake integration | RT and SEB | Autonomous brake request generation |
| RT takeover integration | RT and SEB with simulated SYS loss | SYS-heartbeat-loss takeover path |
| RT/SYS brake arbitration | RT, SYS, and SEB on constrained rig | Single-sender authority transitions and collision prevention |

Every gate must test command loss and reconnect. Brake pressure or stroke must
not release unexpectedly when authority changes or a node reconnects.

## Steering Validation Decomposition

| Gate | Connected hardware | Authority under test |
|---|---|---|
| Steering component acceptance | EPS-C and test station | EPS-C internal behavior and wire contract |
| RT steering integration | RT and EPS-C | Alignment, bounded command generation, checksum/counter, and feedback |
| Following-error test | RT, EPS-C, constrained rack | Persistent command/actual error detection |
| Command-loss test | RT and EPS-C | Ramp, hold, center, or silence behavior defined by the safety design |
| Dynamic-limit test | RT, EPS-C or actuator simulator | Speed-dependent and absolute angle limits |

The rack must be mechanically constrained before power is applied. The test
procedure must define a safe initial angle, permitted range, slew rate, and
abort behavior.

## Encoder and PID Commissioning

Encoder acquisition and PID authority must be validated in stages. Encoder
reading should normally remain active during open-loop testing; only its ability
to alter the command is disabled.

### Supported Operational Combinations

| Encoder acquisition | Encoder use | PID state | Control behavior | Supported |
|---|---|---|---|---:|
| Disabled by configuration | Not used | Disabled | Open-loop command mapping | Yes |
| Enabled and healthy | Telemetry only | Disabled | Open loop with encoder reporting | Yes |
| Enabled and healthy | Supervision | Disabled | Open loop with encoder-based monitoring | Yes |
| Enabled and healthy | Telemetry only | Shadow | Open loop; PID output is reported but cannot affect actuation | Yes |
| Enabled and healthy | Closed loop | Enabled limited | Reduced-authority closed-loop commissioning | Yes |
| Enabled and healthy | Closed loop | Enabled normal | Validated closed-loop vehicle operation | Yes, after acceptance |
| Disabled, unavailable, stale, or faulted | Any | Shadow | PID has no valid measurement | No |
| Disabled, unavailable, stale, or faulted | Closed loop | Enabled limited/normal | Closed loop has no valid measurement | No |

The open-loop configuration is a first-class supported configuration, not a
temporary accidental fallback. It must have its own tests, capability manifest,
limits, diagnostics, and acceptance evidence.

### Reconfiguration Rules

- Encoder and PID settings are versioned configuration, not hidden compile-time
  side effects.
- An artifact may omit an unsupported hardware driver, but it must still expose
  the resulting disabled capability in its manifest.
- Changing encoder acquisition, encoder use, or PID authority is permitted only
  while drive output is at its safe value, gear is safe, and the controller is
  in an approved non-driving state.
- Enabling PID requires an enabled, healthy, fresh, scaled, and direction-checked
  encoder plus accepted gains and output limits.
- Disabling PID immediately removes PID output authority and resets integral,
  derivative, filter, saturation, and previous-error state.
- Moving from open loop to closed loop requires a bumpless transfer check so the
  first PID output cannot create a command step.
- A runtime encoder fault while PID is active removes PID authority immediately.
  Automatic fallback to powered open-loop driving is prohibited unless a
  separately reviewed safety requirement and test suite explicitly permit it.
- An intentionally disabled encoder does not generate encoder-failure alarms,
  but diagnostics report the associated capabilities as unavailable.
- Configuration changes are logged with old/new state, reason, authority,
  timestamp, and artifact/configuration identity.

| Stage | Actuation | Encoder | PID | Exit evidence |
|---|---|---|---|---|
| Open-loop system baseline | Normal bounded open-loop path | Disabled by configuration | Disabled | Complete RT/SYS system behavior, limits, ESTOP, stale-command, and diagnostics validated without encoder dependency |
| Sensor checkout | Inhibited | Physical data recorded | Off | Direction, phase, scale, noise, zero-speed, and disconnect validated |
| Open-loop characterization | Reduced bounded command | Physical data recorded | Off | Command-to-speed response and safe zero command characterized |
| Shadow control | Validated open-loop command | Physical data required | Calculated, not applied | PID output, saturation, and candidate gains recorded |
| Limited closed loop | Reduced authority | Physical data required and supervised | Applied | Stable response, timeout, anti-windup, and sensor-fault abort pass |
| Normal closed loop | Approved normal limits | Physical data required and supervised | Applied | Subsystem and vehicle acceptance gates pass |

Mandatory failure tests include:

- encoder A or B wire disconnected;
- encoder polarity reversed;
- encoder frozen at zero while a nonzero command is applied;
- implausible speed jump or count rate;
- intermittent pulses and noisy edges;
- controller reset while command or PID state is nonzero;
- integral saturation followed by command removal;
- feedback loss during acceleration and deceleration.

A missing or failed encoder must never be interpreted as a valid stationary
measurement that permits increasing throttle.

## Session Authorization State Machine

Physical-actuation sessions should use the following state model:

```text
DISABLED -> REQUESTED -> ARMED -> ACTIVE
    ^            |          |        |
    |            v          v        v
    +---------- ABORTED <- FAULTED <-+
```

| State | Required behavior |
|---|---|
| `DISABLED` | All commissioning outputs inhibited |
| `REQUESTED` | Test station has requested a named session; no actuation permitted |
| `ARMED` | Build profile, physical interlock, configuration, and safe initial feedback verified |
| `ACTIVE` | Deadman held and finite test routine running within its allowlist and limits |
| `FAULTED` | Outputs immediately command or transition to the defined safe state |
| `ABORTED` | Session cleared; reauthorization required before another attempt |

The transition to `ACTIVE` requires all of the following:

- a `COMMISSIONING` firmware artifact;
- a physical commissioning jumper, key, or equivalent local interlock;
- an independent emergency power disconnect verified before the test;
- the expected connected-unit manifest;
- healthy and plausible required feedback;
- a selected finite routine with approved bounds;
- continuous deadman authorization;
- no active ESTOP or unresolved controller fault;
- protocol hash agreement between firmware and test station.

Any failed prerequisite, deadman release, communication timeout, reset, invalid
feedback, or limit violation leaves `ACTIVE` immediately.

## Safety Rules for Physical Actuation

The following rules apply to every commissioning session that can move an
actuator or create hydraulic pressure:

- Only one actuator class is authorized at a time.
- Outputs not listed in the session allowlist remain inhibited.
- ESTOP, watchdogs, generated-codec validation, rolling counters, checksums,
  hard limits, and command timeouts remain active.
- Only the dependency explicitly simulated by the session may be bypassed.
- Commands use approved engineering units, not unrestricted raw register or
  payload writes.
- Limits and slew rates are lower than or equal to the approved subsystem
  limits and are stored in the session manifest.
- Every command has a short validity period and requires continuous refresh.
- A finite routine ends in a defined safe output and cannot loop indefinitely
  without renewed authorization.
- A reset, reconnect, or test-station restart cannot resume actuation.
- Mechanical restraints and guarded exclusion zones are documented in the test
  procedure.
- High-voltage or high-current tests use current limiting and appropriate
  isolation.
- Brake tests use a guarded hydraulic rig, pressure relief, and an independent
  pressure measurement.
- Steering tests use a constrained rack and keep personnel outside its sweep.
- Motor tests begin unloaded or on a controlled dynamometer, not with driven
  wheels on the ground.

Safety monitors should be disabled only when that exact monitor is the subject
of the test. The test procedure must then provide an independent replacement
abort mechanism.

## Test Station and Command Interface

The commissioning mechanism should follow the automotive diagnostic pattern of
a restricted diagnostic session plus finite routines. A complete UDS
implementation is optional, but the concepts are applicable:

| Automotive diagnostic concept | Project use |
|---|---|
| Diagnostic session | Enter a named commissioning context |
| Security/authorization | Prove that the local test station and physical interlock are present |
| I/O control | Temporarily control one allowlisted output |
| Routine control | Start, stop, and query a finite test sequence |
| Data identifiers | Read build, protocol, capability, limits, state, and results |

The normal vehicle command frames must not double as commissioning-control
commands. A test station may use CAN, USB, or serial transport, but it must use
generated typed codecs for vehicle CAN traffic and record all raw frames.

## Capability and Session Manifest

At startup and through a machine-readable diagnostic response, RT and SYS must
publish at least:

| Field | Example meaning |
|---|---|
| Firmware identity | Version, git commit, and dirty state where available |
| Protocol identity | Semantic protocol version and hash |
| Firmware profile | `VEHICLE_RELEASE`, `COMMISSIONING`, `HIL`, or `SIL_NATIVE` |
| Active session | Session name or `NONE` |
| Control strategy | Inhibited, open loop, shadow, limited closed loop, or normal closed loop |
| Feedback source | Physical, simulated, or unavailable for each required input |
| Sensor configuration | Acquisition state and use role for every encoder/sensor channel |
| PID configuration | Disabled, shadow, enabled limited, or enabled normal, including gain/configuration identity |
| Output allowlist | The exact output classes currently permitted |
| Session limits | Angle, pressure, speed, torque, duration, and slew limits as applicable |
| Bypasses | Explicit list; an empty list in vehicle release |
| Interlock state | Physical authorization, deadman, ESTOP, and independent output enable |
| Connected units | Expected, detected, simulated, missing, or faulted |
| Result state | Not started, active, passed, failed, aborted, or incomplete |

The manifest must not claim a capability merely because code for it was
compiled. Required hardware, feedback, and acceptance evidence must also be
present.

## Pre-Hardware Software Verification Strategy

Every firmware profile and commissioning session must pass software verification
before it is permitted to control physical hardware. This is an entry gate, not
proof that the physical unit is safe or correct. Physical behavior must still be
measured during the component and subsystem bench gates.

### Engineering Principles

- Use the same codec, state-machine, safety-monitor, and output-limiting code in
  software tests, commissioning firmware, and vehicle firmware.
- Commissioning should wrap the production control path with additional
  authorization and tighter limits, not introduce an unrelated actuator path.
- Keep hardware access behind narrow interfaces so native tests can substitute
  deterministic CAN, GPIO, ADC, encoder, clock, watchdog, and persistent-storage
  implementations.
- Avoid `#ifdef TESTING` branches that change safety decisions. Build-time code
  selection should be limited to platform adapters and explicit profile
  capabilities.
- Use a deterministic virtual clock for timeout, debounce, watchdog, rolling
  counter, slew-rate, and reconnect tests. Do not make host tests depend on real
  sleeps or scheduler timing.
- Use behavioral unit models, not mocks that always return the expected value.
  EPS-C, SEB, MTR, Host, RT, and SYS simulators must model boot, healthy
  operation, command loss, stale feedback, malformed frames, faults, and reset.
- Define safety invariants once and execute them against unit, integration,
  scenario, HIL, replay, and physical-capture tests.
- Fail closed when a profile, session, connected-unit manifest, feedback source,
  protocol hash, or output permission is unknown.

### Verification Layers

Tests should progress through the following layers. A later layer does not
replace an earlier one.

| Layer | Execution environment | Required verification |
|---|---|---|
| Contract generation | Host/CI | YAML/schema validation, deterministic generation, typed codecs, hashes, routes, and golden vectors |
| Unit tests | Host/native | Pure calculations, limits, state transitions, checksums, counters, conversions, and invalid-input behavior |
| Component tests | Host/native | Complete RT/SYS subsystem with fake HAL and deterministic clock |
| Session conformance | Host/native | Authorization, output allowlist, bounds, deadman, timeout, abort, reset, and forbidden combinations for every session |
| SIL scenarios | Software simulation | RT, SYS, Host, actuator models, sensors, and buses executing nominal and fault scenarios |
| Target-controller tests | Real RT/SYS board, no actuator | Drivers, GPIO, CAN timing, watchdog, memory, reset, and bus-load behavior |
| HIL tests | Real RT/SYS board, simulated units/plant | End-to-end sessions, protocol timing, fault injection, reconnect, and diagnostics |
| Physical component bench | One physical unit and test station | Actual unit behavior, measurements, command loss, limits, and faults |
| Hardware-software integration | One controller and one physical unit | Production control path, real feedback, ESTOP, timeout, and recovery |
| Constrained system integration | Accepted units combined progressively | Authority arbitration and interactions between previously accepted units |

The first physical actuator connection is not allowed until contract, unit,
component, session-conformance, SIL, and applicable controller/HIL tests pass.

### Executable Session Contract

Each session must have a version-controlled test contract. The contract is used
to generate or parameterize both software tests and the physical test procedure.

| Contract field | Required content |
|---|---|
| Identity | Session name and revision |
| Applicable controllers | RT, SYS, test station, or an approved combination |
| Required profile | Usually `COMMISSIONING`; never implicit |
| Connected units | Required, optional, simulated, and prohibited units |
| Entry conditions | Interlock, deadman, ESTOP state, feedback health, protocol hash, and safe initial position |
| Allowed outputs | Exact CAN message types and physical output classes |
| Forbidden outputs | Outputs that must remain inactive for the entire session |
| Control strategy | Inhibited, open loop, shadow, limited closed loop, or normal closed loop |
| Feedback source | Physical, simulated, or unavailable for every input |
| Bounds | Value, slew, duration, duty-cycle, and cumulative-energy limits |
| Safe state | Required output on abort, timeout, reset, or communication loss |
| Fault responses | Expected state and timing for each injected fault |
| Evidence | Required signals, raw captures, logs, measurements, and acceptance limits |

Tests must consume the contract rather than copying IDs, DLCs, limits, or
permissions into multiple hand-maintained tables. CAN facts come from the
generated protocol package. Safety and test-policy limits come from a separate
versioned test-policy definition.

### Session Conformance Suite

Every commissioning session must run the same generic conformance suite in
addition to its function-specific tests.

| Test class | Required cases |
|---|---|
| Profile authorization | Allowed profile succeeds; release, HIL-only, unknown, and mismatched profiles are denied |
| Session exclusivity | A second session cannot start while another session is requested, armed, active, faulted, or aborting |
| Physical authorization | Missing, intermittent, and removed commissioning interlock are handled safely |
| Deadman | Missing at entry, released while active, bouncing, frozen, and stale deadman inputs abort safely |
| Connected-unit manifest | Required unit present; missing; unexpected unit present; duplicate identity; wrong protocol version |
| Feedback readiness | Healthy, stale, frozen, implausible, simulated without permission, and unavailable feedback |
| Encoder/PID combinations | Every supported combination operates as declared; every forbidden combination is denied |
| Output allowlist | Every allowed output can operate only in its permitted state; every non-allowlisted output remains inactive |
| Bounds | Minimum, nominal, maximum, just outside limits, overflow, invalid enum, and excessive slew/duration |
| Command freshness | Missing refresh, delayed frame, duplicate frame, reordered frame, and stale replay |
| Codec validation | Bad DLC, checksum, counter, reserved bits, range, message on wrong bus, and unknown message |
| ESTOP priority | ESTOP before entry, during arming, during motion, during fault handling, and during reconnect |
| Timeout and abort | Request timeout, arm timeout, active timeout, routine timeout, test-station loss, and operator abort |
| Reset and power events | Controller reset, watchdog reset, brownout indication, test-station restart, and peer restart |
| Reconnect | Reconnect with old counter, stale command, changed identity, changed hash, and active ESTOP |
| Diagnostics | Manifest, state, limits, bypasses, reason for denial, reason for abort, and result are truthful |
| Determinism | The same seed, event sequence, and virtual times produce the same outputs and result |

The output-allowlist test must inspect every output on every control cycle, not
only the final state. A brief non-allowlisted pulse is a test failure.

### Session-Specific Software Tests

The generic suite is supplemented by functional tests for each session.

| Session family | Additional software verification |
|---|---|
| RT steering | Alignment state machine, angle sign/offset, clamp, slew, following error, checksum/counter, feedback loss, and ESTOP behavior |
| RT brake takeover | SYS-health arbitration, direct SEB command, rolling counter, no dual sender, takeover timing, hold/release policy, and reconnect |
| RT gateway | Generated route allowlist, source bus, destination bus, no echo, congestion, queue overflow, ESTOP priority, and bus isolation |
| RT sensor/PID | Encoder phase/scale, zero/frozen detection, open-loop mapping, shadow isolation, anti-windup, output authority, and feedback-loss abort |
| SYS inputs | Raw polarity, debounce, stuck-at states, contradictory inputs, ESTOP priority, and mode transition rules |
| SYS outputs | One-output-only selection, inactive level, duration/duty limit, stuck output detection where observable, and abort state |
| SYS brake | Lever path, pressure/stroke mapping, SEB alignment, stale status, degraded state, normal/takeover authority transition, and ESTOP |
| SYS MTR supervision | Command/feedback mismatch, gear conflict, stale feedback, ESTOP acknowledgement, reset, and safe recovery |

The encoder/PID family must include this minimum configuration matrix:

| Test configuration | Required result |
|---|---|
| Encoder subsystem disabled, PID disabled | Complete system boots, enters permitted Manual/Auto states, executes bounded open-loop commands, handles ESTOP/timeouts, and truthfully reports reduced capabilities |
| Encoder subsystem enabled, PID disabled | Open-loop output is unchanged by encoder values; telemetry and configured supervision remain correct |
| Encoder subsystem enabled, PID shadow | PID calculations are observable, but changing gains or measured speed cannot change the actuator command |
| Encoder subsystem enabled, PID limited | PID authority is bounded and feedback loss immediately removes closed-loop authority |
| Encoder subsystem disabled, PID enable requested | Build configuration is rejected before PID can affect any output |
| Encoder faults while PID disabled | Response follows the declared telemetry/supervision role and never silently changes control strategy |
| Encoder faults while PID enabled | PID authority is removed, controller state is reset, safe response occurs, and no automatic powered open-loop fallback occurs unless separately approved |
| CAN request attempts to change encoder/PID build state | Request is rejected or ignored and recorded; ordinary CAN cannot mutate build configuration |
| New encoder/PID configuration selected | Full rebuild, software tests, artifact approval, reflash, and startup identity verification are required |
| Reset in every supported combination | Configured combination is restored only from validated configuration; stale PID state and command authority are not restored |

Tests must also prove the open-loop system does not read disabled encoder values,
wait for encoder initialization, report encoder health as valid, or block normal
non-encoder functionality on an encoder heartbeat or freshness condition.

### Property and State-Machine Testing

Example-based tests are necessary but not sufficient for session logic. Use
property-based or generated state-machine tests to explore combinations and
event ordering.

The following invariants must hold for every generated sequence:

- Physical actuation is impossible outside an authorized active session.
- Only outputs in the active session allowlist can change from their safe state.
- ESTOP dominates session requests, commands, reconnects, and recovery events.
- Stale or invalid data cannot retain or restore actuation authority.
- Output value, slew, duration, and cumulative limits are never exceeded.
- Missing required physical feedback cannot be represented as healthy.
- Intentionally disabled optional feedback does not become an undeclared runtime
  dependency or prevent the supported open-loop configuration from operating.
- PID output authority is impossible unless its required encoder is enabled,
  healthy, fresh, and assigned the `CLOSED_LOOP` use role.
- Reset and reconnect never resume the previous command or session.
- No transition goes directly from a fault or ESTOP state to physical actuation.
- Session denial and abort reasons remain observable and deterministic.
- `VEHICLE_RELEASE` cannot enter any commissioning state for any input sequence.

Generated tests should use reproducible seeds and save any failing sequence as a
permanent regression fixture.

### Coverage and Test Quality

- Measure line and branch coverage for commissioning and safety logic.
- Require condition/decision coverage for authorization, allowlist, ESTOP,
  timeout, and abort decisions where practical.
- Use mutation testing on host-testable safety logic to prove tests fail when a
  limit, comparison, timeout, or authorization decision is incorrectly changed.
- Treat coverage as evidence of exercised code, not evidence of correctness.
- Review test code with the same rigor and ownership rules as production code.
- Require at least one negative test for every positive authorization test.
- Test every documented forbidden profile/session/strategy/source combination.

This project does not claim ISO 26262 compliance solely from these practices.
Formal safety classification may require additional independence, methods, and
coverage evidence.

### CI and Release Gates

| Gate | Trigger | Required scope |
|---|---|---|
| Fast pull-request gate | Every change | Schema/codegen check, affected native tests, profile compile checks, and core safety invariants |
| Safety-sensitive pull-request gate | CAN, RT, SYS, session, HAL, or safety change | Full RT/SYS native suite, session conformance, SIL scenarios, static analysis, and forbidden-combination audit |
| Nightly gate | Scheduled | Full generated matrices, randomized seeded sequences, replay corpus, fault injection, soak tests, and sanitizers |
| Release-candidate software gate | Candidate artifact | All supported profiles, complete traceability, clean generated artifacts, test report, and artifact hashes |
| Controller/HIL gate | Candidate firmware on boards | Target timing, watchdog, CAN load, reset, bus-off, and all applicable session tests with no actuator |
| Physical-test authorization | Before each unit connection | Approved software/HIL report, exact artifact hash, session contract, rig checklist, and operator approval |

CI must compile every supported combination and deliberately verify that
unsupported combinations fail to compile or are rejected before arming.

### Traceability

Every requirement should map in both directions:

```text
hazard/control requirement
        -> session contract
        -> software test case(s)
        -> HIL test case(s)
        -> physical procedure step(s)
        -> captured evidence and result
```

A physical test must be blocked when its required software or HIL test is
missing, failed, stale for the selected artifact, or executed against a
different protocol hash.

## One-Unit-at-a-Time Hardware Practice

All initial real actuator testing uses an approved commissioning session and
connects one physical unit at a time. Unrelated actuator power and command paths
remain physically disconnected, not merely disabled in software.

### Standard Sequence for Each Unit

| Step | State | Required check |
|---|---|---|
| 1. Evidence check | Unit disconnected | Exact firmware, profile, session contract, software/HIL report, and protocol hash match |
| 2. Harness check | Power off | Pinout, polarity, isolation, grounding, termination, fusing, resistance, and mechanical restraint verified |
| 3. Logic power | Actuator power inhibited | Controller and unit logic boot, current is normal, diagnostics readable, and no command is sent |
| 4. Passive observation | Output inhibited | Expected status frames, counters, initial position, faults, and timing are captured |
| 5. Authorization test | Output still inhibited | Interlock, deadman, deny paths, manifest, and session state transitions are demonstrated |
| 6. Safe-state test | Output enabled but zero/safe command | No movement or unexpected pressure/torque; ESTOP and timeout return safe |
| 7. Reduced routine | Minimum approved authority | Direction, polarity, scaling, feedback, slew, and bounds are measured |
| 8. Bounded range | Incremental approved points | Repeatability, tracking, current, temperature, pressure/angle/speed, and limits are measured |
| 9. Fault tests | Reduced authority | Command loss, feedback loss, ESTOP, deadman release, reset, and disconnect behavior pass |
| 10. Reconnect/power cycle | Safe command | No automatic authority restoration or unexpected movement occurs |
| 11. Soak | Approved bounded cycle | Timing, thermal behavior, counters, drops, and resource stability remain acceptable |
| 12. Evidence review | Power removed | Capture completeness, deviations, pass/fail result, and next-gate approval recorded |

The next physical unit is not connected until the current unit has a reviewed
result. A failed, aborted, blocked, or incomplete result does not permit
progression.

### Required Isolated Hardware Order

| Order | Physical unit connected | Controlling equipment/session |
|---:|---|---|
| 1 | EPS-C only | Test station using `EPS_COMPONENT_BENCH` |
| 2 | SEB only | Test station using `SEB_COMPONENT_BENCH` |
| 3 | Encoder only | Acquisition hardware using `ENCODER_COMPONENT_BENCH` |
| 4 | Motor controller and unloaded motor only | Test station using `MOTOR_COMPONENT_BENCH` |
| 5 | RT only, no actuator | `RT_CONTROLLER_BENCH`, `RT_PASSIVE_MONITOR`, and `RT_GATEWAY_TEST` |
| 6 | SYS only, protected loads only | `SYS_CONTROLLER_BENCH`, input checkout, and output checkout |
| 7 | RT plus EPS-C only | `RT_STEERING_COMMISSIONING` |
| 8 | SYS plus SEB only | `SYS_BRAKE_COMMISSIONING` |
| 9 | RT plus SEB only | RT autonomous brake and `RT_BRAKE_TAKEOVER_TEST` |
| 10 | SYS plus MTR/motor subsystem only | `SYS_MTR_SUPERVISION_TEST` and approved motor session |
| 11 | RT plus encoder/motor rig only | Open-loop, shadow, then limited closed-loop sessions |
| 12 | RT plus SYS, no actuators | Controller integration with simulated external units |
| 13 | RT plus SYS and one accepted actuator class | Subsystem arbitration and failure tests |
| 14 | Complete constrained stack | Integration profile with wheels lifted and actuators guarded |

The order can be changed only by a reviewed test plan that preserves dependency
gates and physical isolation.

### Commissioning Versus Vehicle-Release Testing

Commissioning sessions are used for isolated bring-up because they reduce
authority and constrain outputs. They do not replace tests of the final vehicle
artifact.

After every unit and subsystem has passed commissioning:

1. Build the exact `VEHICLE_RELEASE` artifact intended for the vehicle.
2. Repeat controller-only and HIL regression tests against that artifact.
3. Perform constrained full-stack tests with no commissioning session active.
4. Verify that release firmware denies all commissioning requests and contains
   no bypasses.
5. Proceed to low-speed vehicle commissioning only after release-artifact
   evidence passes.

This prevents a commissioning-only implementation from passing while the
actual vehicle control path remains untested.

## Required Evidence

Every physical session produces an evidence bundle containing:

- date, test operator, test procedure revision, and test rig identity;
- firmware version, git commit, local-diff status, and build profile;
- protocol version/hash and generated-codec version;
- active session, control strategy, feedback sources, limits, and bypass list;
- wiring/connected-unit manifest and physical interlock checks;
- raw CAN capture and relevant serial/diagnostic logs;
- commanded and measured values with timestamps;
- reset, watchdog, bus error, drop, timeout, and fault counters;
- expected pass/fail criteria and actual result;
- any abort, anomaly, deviation, or unavailable measurement.

`PASS`, `FAIL`, `ABORTED`, `BLOCKED`, and `INCOMPLETE` are distinct outcomes.
An aborted or incomplete test is not a pass.

## Integration Gates

The required order is:

| Gate | Connected hardware | Exit condition |
|---|---|---|
| 1. Software verification | No physical hardware | SIL, codecs, state machines, limits, and fault matrices pass |
| 2. Component acceptance | One actuator or sensor with test station | Unit wire contract, operation, command loss, and faults pass |
| 3. Controller bench | RT or SYS with analyzers/simulators | Board I/O, CAN, timing, watchdog, and safe-state behavior pass |
| 4. Single-unit integration | One controller and one physical unit | Direction, bounds, feedback, ESTOP, loss, and recovery pass |
| 5. RT/SYS controller integration | RT and SYS, no physical actuators | Modes, heartbeats, arbitration, gateway, and failure responses pass |
| 6. Subsystem integration | RT/SYS plus one actuator class at a time | Authority transitions and cross-controller monitoring pass |
| 7. Constrained complete stack | All hardware, wheels lifted or mechanically constrained | No unintended actuation; all loss and ESTOP paths pass |
| 8. Vehicle commissioning | Closed area at walking speed | Approved maneuver, stop, ESTOP, timeout, and recovery tests pass |

No later gate compensates for a failed or skipped earlier gate.

## Stop Conditions

Testing stops and authorization is cleared when any of the following occurs:

- unexpected actuator motion or direction;
- output exceeds the session limit or expected slew rate;
- required feedback is missing, stale, frozen, or implausible;
- brake pressure releases unexpectedly;
- steering does not follow the specified abort behavior;
- ESTOP, deadman, watchdog, or command timeout fails;
- a non-allowlisted CAN frame or physical output is actuated;
- checksum, rolling-counter, DLC, protocol-hash, or unit mismatch occurs;
- CAN bus-off, repeated error frames, queue overflow, or uncontrolled reconnect
  occurs;
- controller reset, brownout, or unexpected watchdog event occurs;
- the physical rig, restraint, guard, supply limit, or emergency disconnect is
  not in the documented state;
- evidence recording is unavailable.

## Migration from Existing Modes

The migration should be incremental:

1. Keep `VEHICLE_RELEASE`, `COMMISSIONING`, `HIL`, and `SIL_NATIVE` as explicit
   build profiles with a fail-safe default.
2. Add a machine-readable build/capability manifest and expose it at startup.
3. Replace the single hardware override behavior with named session requests.
4. Require each session to declare connected units, feedback sources, output
   allowlist, limits, and timeouts.
5. Replace `g_bypass_eps_sync`, `g_bypass_seb_sync`, and
   `g_bypass_mtr_absent` with scoped dependency simulation states consumed only
   by the authorized session.
6. Add controller-bench sessions first because they have no physical actuation.
7. Add steering, brake, motor, and encoder sessions one at a time with tests for
   every authorization and abort condition.
8. Make CI prove that `VEHICLE_RELEASE` cannot enter a commissioning session and
   contains no active bypass.
9. Make CI build and test each supported profile/session combination and reject
   combinations not present in an explicit allowlist.
10. Retire `SYSTEM_RUN_MODE` only after all production and test procedures use
    the new profile/session model.

During migration, existing broad bench bypasses must not be used for physical
actuation unless a reviewed test procedure independently constrains every
unrelated output.

## Minimum Implementation Acceptance Criteria

The design is ready for physical commissioning only when:

- vehicle modes and commissioning sessions are separate state machines;
- `VEHICLE_RELEASE` has no commissioning entry path;
- every session has generated or compile-checked output allowlists;
- only one physical actuator class can be enabled at a time;
- session entry requires the firmware profile, physical interlock, local
  authorization, valid manifest, healthy feedback, and deadman;
- session timeout, deadman release, reset, ESTOP, malformed command, and feedback
  loss all produce the defined safe state;
- reconnect never restores authority automatically;
- the active profile, session, strategy, feedback source, limits, and bypasses
  are visible in logs and diagnostics;
- the test station and firmware use generated typed codecs and matching protocol
  hashes;
- native tests cover every authorization transition and forbidden combination;
- native, SIL, controller, and HIL tests prove complete supported operation with
  optional encoders disabled and PID disabled;
- tests prove encoder enable/disable and PID enable/disable transitions are
  rejected outside the approved safe state;
- the manifest distinguishes intentionally disabled, unavailable, valid,
  stale, and faulted sensors and reports every unavailable dependent capability;
- controller-bench evidence passes before any physical-actuation session is
  enabled.

## Related Documents

- [`rt-sys-feature-configuration-and-test-plan.md`](rt-sys-feature-configuration-and-test-plan.md)
- [`validation/rt-sys-pre-vehicle-validation.md`](validation/rt-sys-pre-vehicle-validation.md)
- [`hil-safety-test-plan.md`](hil-safety-test-plan.md)
- [`can-bench-test.md`](can-bench-test.md)
- [`integration-test-procedure.md`](integration-test-procedure.md)
- [`hardware-safety.md`](hardware-safety.md)
- [`estop.md`](estop.md)
- [`../shared/system_mode.h`](../shared/system_mode.h)
