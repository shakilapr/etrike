# Big ESP Consolidated Controller Architecture and Migration Plan

## Status

This document is the implementation plan for replacing `rt-esp32` and
`sys-esp32` with one ESP32-S3 firmware project under `big-esp`.

The consolidation is intended to remove cross-controller coordination and
duplicate runtime infrastructure. It is not permission to weaken ESTOP,
freshness, output, watchdog, or physical test requirements.

The first supported target is open-loop operation with the RT encoder subsystem
disabled and speed PID disabled. Encoder and PID work remains optional and must
not block the consolidated baseline.

## Decision

Build one controller with these boundaries:

- one ESP32-S3 and one firmware image;
- two physical 500 kbit/s CAN buses, preserving the existing high/low topology;
- built-in TWAI for low CAN and an external MCP2515 over SPI for high CAN;
- one owner for mode, safety state, steering, brake, body I/O, and gateway policy;
- MTR remains a separate STM32 motor-actuation controller;
- PWT remains a separate controller on its standalone powertrain bus;
- existing external CAN bytes remain unchanged during consolidation;
- RT-to-SYS messages become typed in-process data, not looped-back CAN frames.

The new controller is called **Big ESP** in this plan. A product name and CAN
node name can be selected later without blocking implementation.

## Why This Direction

The current split creates complexity that does not add useful behavior in a
single-controller product:

- RT mirrors a mode owned by SYS.
- RT and SYS negotiate SEB ownership and can both produce `0x7B9`.
- SYS monitors RT heartbeat, state, and setpoint freshness even though those
  values can be local in the consolidated controller.
- Host/HMI frames are forwarded to SYS only so a second MCU can consume them.
- SYS status and diagnostics are sent to RT only so RT can forward them.
- both projects duplicate CAN RX, dispatch, TX, heartbeat, watchdog, build,
  bypass, and diagnostic infrastructure.

The merge removes these internal distributed-system failure modes while keeping
external buses and actuator protocols stable.

## Safety Tradeoff

The existing RT/SYS split provides physical fault containment between two
MCUs. One ESP32-S3 creates a common failure domain for planning-command
resolution and supervisory safety logic. Software tasks on one ESP32-S3 are not
equivalent to independent safety controllers or AURIX lockstep/memory-isolated
safety domains.

Therefore:

- Big ESP must initially be treated as a prototype/bench architecture.
- MTR's local command timeout, direct ESTOP input, safe DAC/gear behavior, and
  ESTOP acknowledgement remain mandatory independent protections.
- the physical ESTOP path must reach Big ESP and MTR independently of CAN;
- an external hardware watchdog must force safe outputs on Big ESP failure;
- consolidation cannot claim the same safety integrity as two independent MCUs
  without a reviewed safety argument and physical fault-injection evidence;
- if independent Level 2 supervision is a product requirement, retain a
  separate safety MCU or select safety hardware designed for that requirement.

No vehicle release is permitted from this plan until that decision is recorded.

## Scope

### Included

- RT kinematics, steering, obstacle limiting, command freshness, and gateway;
- SYS mode, ESTOP, SEB control, body inputs/outputs, and diagnostics;
- one consolidated build and deployment configuration;
- removal of internal RT/SYS CAN coordination;
- migration of native, simulation, firmware, CI, debug, and documentation paths;
- a new board pin map and harness definition;
- explicit output policy and artifact identity.

### Not Included

- merging MTR or PWT into Big ESP;
- changing external CAN wire layouts as part of controller consolidation;
- enabling active speed PID;
- approving unvalidated encoder hardware;
- redesigning the Control UI;
- pretending unsupported vendor semantics are generated from YAML;
- changing the high/low CAN topology to a single bus.

## Current Baseline

### RT currently owns

- high CAN through MCP2515 and low CAN through TWAI;
- Host command ingestion and high/low gateway routing;
- tricycle kinematics and obstacle speed/brake limiting;
- steering command generation and EPS-C supervision;
- MTR drive command generation;
- Auto brake requests and SEB takeover behavior;
- command, Host heartbeat, and SYS heartbeat supervision;
- RT telemetry and heartbeat publication.

Primary implementation: `rt-esp32/src/main.cpp`, `can_dispatch.h`,
`safety_monitor.h`, `steering_control.h`, and `config.h`.

### SYS currently owns

- authoritative Manual, Auto, and Estop mode state;
- physical ESTOP, mode, start, brake lever, and light inputs;
- lamps, indicators, and the 12 V relay;
- normal/manual/ESTOP SEB command generation;
- RT heartbeat and setpoint monitoring;
- MTR command/feedback comparison and ESTOP acknowledgement checks;
- SYS safety status, diagnostics, and heartbeat publication.

Primary implementation: `sys-esp32/src/main.cpp`, `mode_manager.cpp`,
`safety_monitor.cpp`, `brake_control.h`, and `config.h`.

### Existing consolidation reference

`rt-aurix-lite/rt-aurix-lite-architecture.md` demonstrates the correct logical
direction: keep two buses, give one controller one actuator owner, and remove
cross-MCU coordination. Its AURIX task isolation, MCMCAN peripherals, pin map,
and safety claims do not apply to ESP32-S3 and must not be copied.

## Target Topology

```text
 High CAN, 500 kbit/s
 Host / Jetson / HMI
          |
          | MCP2515 + SPI
          v
 +-----------------------------+
 |          BIG ESP            |
 |                             |
 | command and protocol input  |
 | one mode and safety owner   |
 | kinematics and steering     |
 | one brake arbiter/SEB owner |
 | body I/O and diagnostics    |
 | explicit CAN gateway        |
 +-----------------------------+
          |
          | ESP32-S3 TWAI
          v
 Low CAN, 500 kbit/s
 EPS-C / SEB / MTR

 Separate 250 kbit/s powertrain CAN: PWT / DC-DC
 Direct hardware ESTOP: Big ESP + MTR
```

Two buses are retained because they preserve Host/actuator separation, existing
wiring expectations, gateway policy, and external message instances. Combining
the buses would be a separate network and safety migration.

## Ownership Model

Big ESP is one controller, but it must not become one monolithic class or one
thousand-line task. Ownership is divided by behavior while safety-critical
state has one writer.

| Concern | Target owner |
|---|---|
| Physical mode and ESTOP state | `ModeManager` in control task |
| Host/HMI command validity | input supervision modules |
| Vehicle setpoint resolution | control task |
| Steering state machine | steering module, advanced by control task |
| Brake arbitration and SEB request | one brake module, advanced by control task |
| MTR supervision | safety module, evaluated by control task |
| Body input sampling/output | body I/O task through typed snapshots |
| CAN payload validation | selected generated/profile/custom codec |
| CAN freshness/counter tracking | supervision layer, outside codecs |
| Bus routing | explicit gateway table |
| Final CAN/GPIO authorization | output policy at the driver boundary |
| Diagnostics and logging | supervisor/service task |

### State flow

```text
CAN RX and GPIO sampling
        -> validated, timestamped InputSnapshot
        -> single control/safety evaluation
        -> immutable OutputSnapshot
        -> output policy
        -> CAN TX and GPIO drivers
```

Rules:

- only the control task changes authoritative mode and safety state;
- dispatch never directly commands an actuator;
- TX tasks encode approved snapshots and do not make mode decisions;
- stale or invalid input is represented explicitly, never as a valid numeric
  zero;
- reset/reconnect cannot restore a prior command or actuator authority;
- ESTOP dominates mode, command, PID, reconnect, and all normal output paths;
- output presence and output authorization remain separate decisions.

## CAN Contract During Migration

Controller consolidation must not be combined with a wire-format redesign.
Existing external consumers are a concrete compatibility requirement.

### Messages that remain external

| Message | Target behavior |
|---|---|
| `0x001 SAFETY_ESTOP` | Receive and originate on applicable buses with bounded forwarding/rate policy |
| `0x011 SYS_SAFETY_STS` | Produce directly on high CAN; keep low instance only if a verified external consumer requires it |
| `0x110 SYS_MODE_CMD` | Continue on low CAN for MTR and any verified external consumer |
| `0x120 SYS_THROTTLE_STS` | Continue gateway behavior if still produced by MTR |
| `0x169 VCU_SES_REQ` | One Big ESP sender on low CAN |
| `0x204 RT_DRIVE_CMD` | Continue on low CAN for MTR |
| `0x206 MTR_MOTOR_FBK` | Continue as low-CAN input |
| `0x210 RT_STATE_RPT` | Produce directly on high CAN; remove low copy after SYS removal unless another consumer is proven |
| `0x220 RT_PID_RPT` | Preserve as telemetry while semantics remain truthful |
| `0x300 HOST_DRIVE_CMD` | Consume directly from high CAN |
| `0x301 HOST_BRAKE_REQ` | Consume directly from high CAN |
| `0x302 HOST_LIGHT_CMD` | Consume directly; do not forward to low CAN solely for removed SYS |
| `0x310/0x311` diagnostics | Produce directly on high CAN |
| `0x600 SYS_DIAG_RPT` | Produce directly on high CAN |
| `0x7B9 VCU_SEB_REQ` | One Big ESP sender in every mode |
| `0x7FC HOST_HEARTBEAT` | Continue as high-CAN input |
| `0x7FD RT_HEARTBEAT` | Preserve on high and low with independent per-bus counters until a reviewed rename/version change |

### Messages that become internal or are retired

| Message/path | Migration action |
|---|---|
| `0x205 RT_BRAKE_CMD` to SYS | Replace with an in-process typed brake demand; stop physical TX after parity tests |
| `0x7FE SYS_HEARTBEAT` to RT | Remove; internal task health is supervised locally |
| low-CAN `0x210` used only by SYS | Remove after consumer audit |
| Host/HMI forwarding used only to reach SYS | Remove and consume on high CAN |
| SYS status/diagnostic forwarding used only to reach Host | Produce directly on high CAN |
| RT/SYS SEB takeover handshake | Delete; one brake owner cannot hand off to itself |

Before deleting any frame instance, generate a `bus + CAN ID` consumer report
and prove that no external ECU, logger, HMI, or test station depends on it.

### Protocol architecture alignment

Big ESP follows `docs/protocol-architecture-migration-plan.md`:

- YAML defines static wire layout, ownership, bus instances, and strategy;
- payload strategy is exactly one of `generated`, versioned `profile`, or
  versioned `custom`;
- CRC, counters, and vendor algorithms remain in explicit profile/custom code;
- freshness, frozen counters, dropout, and safety response remain outside codecs;
- runtime identity is `bus + CAN ID`, not CAN ID alone;
- Big ESP must not add a new local CAN dictionary or copied DTO layer.

Protocol migration and Big ESP migration may share tests, but a commit must not
silently change both controller ownership and wire bytes.

## Brake Simplification

The current distributed design has two potential `0x7B9` producers and uses
mode, heartbeat, RT state, setpoint freshness, and SEB rolling acknowledgement
to avoid or recover from dual ownership. Big ESP replaces that arrangement with
one brake state machine and one rolling-counter sequence.

Priority, highest first:

1. ESTOP brake command.
2. Physical brake lever override.
3. Safety-assisted stop caused by stale/faulted required input.
4. Host/obstacle Auto brake demand.
5. Released brake command.

The brake module owns boot synchronization, control mode, checksum, rolling
counter, command rate, following error, status freshness, fault latch, and
recovery. No other module may encode or send `0x7B9`.

This is the largest complexity reduction in the merge and must have exhaustive
mode/ESTOP/reset/fault sequence tests before hardware transmission is enabled.

## Proposed RTOS Layout

Do not copy the current RT and SYS task lists into one firmware. Start with this
bounded layout and split a task only when timing evidence requires it.

| Task | Priority | Trigger/period | Responsibility |
|---|---:|---|---|
| `can_rx_low` | 5 | event | Receive TWAI frames and enqueue bus-tagged envelopes |
| `can_rx_high` | 5 | event | Receive MCP2515 frames and enqueue bus-tagged envelopes |
| `dispatch` | 4 | event | Validate bus/ID/DLC/codec and update timestamped input store |
| `control` | 5 | 100 Hz | ESTOP, mode, safety, kinematics, steering, brake, final output snapshot |
| `can_tx_low` | 3 | 5 ms base | Scheduled actuator/MTR frames and bounded gateway queue |
| `can_tx_high` | 3 | 10 ms base | Telemetry, diagnostics, heartbeat, bounded gateway queue |
| `body_io` | 3 | 50 Hz | Sample non-ESTOP switches and apply approved lamp/relay snapshot |
| `supervisor` | 2 | 10 Hz base | task deadlines, CAN health, heartbeat schedule, NVS diagnostics, rate-limited logs |

The ESTOP GPIO is sampled in the control path or latched by a minimal ISR and
consumed by control; it must not depend on the lower-priority body task.

Use ESP task watchdog support plus per-task deadline counters. The external
watchdog is serviced only after all mandatory task deadlines and output-policy
health checks pass, not unconditionally by one task.

## Hardware and Pin Plan

The current RT and SYS pin maps overlap. Examples include GPIO 1/2, 6/7, 9/10,
17/18, and the RT encoder/SPI assignments. The two existing devkit pin maps
cannot simply be concatenated.

### Required hardware decision

Create and review one Big ESP board/harness map before target integration:

- preserve RT's low TWAI plus high MCP2515 architecture unless hardware review
  selects equivalent dual-CAN hardware;
- route ESTOP directly to Big ESP and MTR;
- reserve direct MCU pins for CAN, ESTOP, brake lever, mode/start, and watchdog;
- default every actuator/body output to non-actuating in hardware and software;
- avoid ESP32-S3 strapping, flash/PSRAM, and USB pins unless the exact module and
  boot behavior explicitly permit their use;
- document transceiver standby, termination, interrupt, reset, and bus-off paths;
- document brownout and watchdog effects on every relay/output driver;
- use protected drivers appropriate for 12 V loads rather than driving loads
  from GPIO;
- decide whether slow lamps/indicators use direct GPIO or a fail-off output
  expander based on a reviewed failure analysis, not pin convenience.

The first Big ESP profile keeps encoders disabled. This removes eight unproven
PCNT pin requirements from the initial board and prevents the consolidation
from depending on known encoder implementation gaps. Later encoder support
requires a separate approved pin/calibration map.

### Hardware exit criteria

- no pin has multiple owners;
- no safety input depends on an optional expander;
- reset/boot/flash states produce no actuator pulse;
- loss of MCU power or watchdog service produces the documented safe state;
- both CAN interfaces recover or fail safely under bus-off and transceiver loss;
- measured CPU, RAM, queue, ISR, SPI, and task margins meet the timing budget.

## Project Structure

Target structure:

```text
big-esp/
|-- platformio.ini
|-- sdkconfig.defaults
|-- README.md
|-- big-achitecture.md
|-- src/
|   |-- main.cpp
|   |-- app_controller.cpp
|   |-- input_store.cpp
|   |-- output_policy.cpp
|   |-- board_config.h
|   |-- can_gateway.cpp
|   |-- can_schedule.cpp
|   |-- mode_manager.cpp
|   |-- safety_monitor.cpp
|   |-- brake_control.cpp
|   |-- body_control.cpp
|   `-- imported RT modules with proven behavior
`-- test/
    |-- unit/
    |-- integration/
    `-- target/
```

This is a target boundary, not a requirement to create one file per name before
code exists. Reuse production modules where behavior is already testable. Do
not copy logic into Big ESP and leave a second maintained implementation.

## Configuration

Big ESP must consume one immutable, generated build configuration. Do not carry
forward the broad `SYSTEM_RUN_MODE` behavior that enables grouped bypass flags.

Minimum configuration dimensions:

- deployment profile: vehicle, controller-test, HIL, or SIL/native;
- external unit policy: disabled, required physical, or simulated;
- output permissions by destination/class;
- high/low bus topology and bitrate;
- board/harness revision;
- encoder subsystem enabled/disabled;
- speed feedback source;
- PID disabled/shadow/active;
- protocol and network-contract hashes.

The first profile is:

```text
encoder subsystem: disabled
speed feedback: none
PID: disabled
external units: explicit per test setup
physical outputs: inhibited unless an isolated procedure allows one class
```

The build must emit a manifest and startup identity containing firmware, git,
dirty state, environment, board revision, unit policies, output permissions,
feature state, protocol hash, configuration hash, and final binary hash.

## Output Policy

All physical effects pass through one final policy boundary:

- steering CAN request;
- MTR drive command;
- SEB brake request;
- ESTOP broadcast;
- body lamps and indicators;
- 12 V relay;
- gateway transmission;
- diagnostics and heartbeat, classified separately from actuation.

Policy checks include deployment profile, compatible unit policy, explicit
output permission, current mode/safety state, freshness, and bus destination.
Denied, expired, failed, and dropped outputs are counted and reported.

Controller-only and HIL builds must prove that no physical actuator output can
escape, including during boot, reset, queue pressure, and reconnect.

## Migration Plan

Phases are sequential. Do not remove RT/SYS behavior until Big ESP has parity
evidence for that behavior.

### Phase 0: Freeze decisions and baseline

- record the exact RT/SYS builds, tests, protocol hashes, pin maps, and known
  failures;
- inventory every RT/SYS CAN producer, consumer, gateway route, GPIO, NVS key,
  task, queue, timeout, and output path;
- classify each `bus + CAN ID` as external-preserved, internalized, or candidate
  for removal;
- record the safety decision on loss of the separate SYS MCU;
- select the exact ESP32-S3 module/board direction.

Exit gate: no unresolved safety, external-consumer, or hardware decision blocks
the first output-inhibited Big ESP build.

### Phase 1: Pin protocol behavior

- complete baseline vectors required by
  `docs/protocol-architecture-migration-plan.md`;
- add independent vectors for every message Big ESP sends or receives;
- include counter/checksum sequences for SES and SEB;
- preserve current wire hashes;
- make tests bus-aware.

Exit gate: current RT/SYS wire behavior can be reproduced independently of the
old controller implementations.

### Phase 2: Extract reusable production cores

- move monolithic RT/SYS decisions into host-testable modules where needed;
- preserve RT physics, steering, watchdog, mode, safety, brake, and body
  behavior through tests before integration;
- remove direct driver calls from reusable logic;
- pass typed input/time into modules and return typed decisions;
- do not copy algorithms into tests or parallel Big ESP-only versions.

Exit gate: each reused behavior runs in native tests without FreeRTOS, GPIO, or
CAN driver globals.

### Phase 3: Scaffold output-inhibited Big ESP

- add one PlatformIO project and SDK configuration;
- initialize deterministic safe GPIO before CAN and tasks;
- bring up low TWAI and high MCP2515 with bus-tagged frames;
- add configuration identity, NVS reset diagnostics, queues, task creation
  checks, and task deadline monitoring;
- implement final output policy with all physical outputs denied by default.

Exit gate: target firmware boots repeatedly, reports exact identity, monitors
both isolated buses, and emits no actuator/body output.

### Phase 4: Integrate input, mode, and safety ownership

- consume Host/HMI directly from high CAN;
- consume actuator/MTR feedback directly from low CAN;
- integrate one `ModeManager` and one safety evaluation;
- make physical ESTOP dominant and independent from ordinary CAN dispatch;
- replace RT's mirrored mode events with direct typed state;
- replace SYS heartbeat supervision with local task deadline supervision;
- preserve Host and MTR external freshness supervision.

Exit gate: native and SIL sequences prove deterministic Manual/Auto/Estop,
timeout, reset, reconnect, and conflicting-input behavior.

### Phase 5: Integrate control and single brake authority

- connect kinematics, obstacle limiting, steering, and MTR command generation;
- replace `RT_BRAKE_CMD` with a typed internal brake demand;
- implement one SEB state machine and one `0x7B9` producer;
- delete takeover/handoff decisions from the Big ESP path;
- keep all physical outputs inhibited while comparing expected frames with
  recorded RT/SYS behavior.

Exit gate: every mode and fault sequence has exactly one bounded steering,
drive, and brake decision, with ESTOP dominant.

### Phase 6: Integrate body I/O, telemetry, and gateway

- merge SYS body input/output behavior;
- publish `0x011`, `0x210`, diagnostics, and heartbeats directly on their target
  buses;
- preserve only gateway routes with verified external consumers;
- remove forwarding whose sole purpose was communication with removed SYS;
- add structured, stable-ID, rate-limited diagnostics.

Exit gate: high/low captures match the approved consolidated message catalog,
with no duplicate sender or unexpected forwarding.

### Phase 7: Hardware map and target timing

- implement the reviewed Big ESP board pin map;
- verify safe boot and watchdog behavior electrically;
- measure task periods, jitter, SPI latency, queue depth, stack high-water marks,
  heap, CAN load, RX overflow, TX failure, and bus-off recovery;
- test both buses at maximum expected traffic plus bounded bursts;
- run with actuator power disconnected.

Exit gate: timing/resource margins and fail-safe electrical behavior are
recorded against the exact firmware and board revision.

### Phase 8: SIL, controller, and HIL parity

- add a Big ESP simulation model or host-compiled production core;
- run the same scenario corpus formerly used for RT/SYS integration;
- inject stale, frozen, corrupt, wrong-bus, duplicate, reset, reconnect, queue
  pressure, and bus-off faults;
- compare commanded and measured state separately;
- use the Control UI/backend only through the same protocol/API contracts used
  by automated tools;
- keep physical actuator outputs inhibited.

Exit gate: software and controller/HIL matrices pass with deterministic replay
and no hidden dropped evidence.

### Phase 9: Isolated physical acceptance

Connect one external unit/output class at a time:

1. Host/HMI and protected body I/O loads.
2. EPS-C on a constrained steering fixture.
3. SEB on a guarded hydraulic fixture.
4. MTR with motor output initially inhibited, then on an approved constrained
   rig.
5. Complete open-loop system with encoders and PID still disabled.

Each step uses a previously approved binary and records configuration, protocol,
firmware, hardware, capture, and test hashes.

Exit gate: each unit passes its command, feedback, timeout, ESTOP, reset,
reconnect, and power-cycle procedure before combined operation.

### Phase 10: Cutover and cleanup

- switch CI and release tooling to `big-esp`;
- update architecture, flashing, wiring, test, simulation, and debug docs;
- remove obsolete RT/SYS integration frames from active network contracts only
  after consumer evidence permits it;
- archive or remove `rt-esp32` and `sys-esp32` after Big ESP release acceptance;
- reject new production changes to retired controller paths;
- retain historical evidence without presenting old plans as current design.

Exit gate: one firmware, one board map, one active controller architecture, and
one authoritative test/evidence path remain.

## Verification Matrix

Minimum automated and bench coverage:

| Area | Required evidence |
|---|---|
| Protocol | Generated/profile/custom golden vectors and bus-scoped identity |
| Mode | Manual/Auto/Estop transitions, debounce, long press, invalid requests |
| ESTOP | GPIO, CAN, internal fault, MTR report, reset, and simultaneous events |
| Freshness | Never seen, stale, frozen, reconnecting, and recovered inputs |
| Steering | boot sync, limits, slew, following error, status loss, ESTOP ramp |
| Brake | one sender, priority order, checksum/counter, following error, status loss |
| Drive/MTR | bounded command, gear, feedback mismatch, timeout, ESTOP ACK |
| Gateway | exact allowlist, direction, bus identity, loop prevention, queue pressure |
| Body I/O | safe boot, switch behavior, light arbitration, relay fail-off |
| Scheduling | period, jitter, deadline miss, task stall, stack/heap margin |
| CAN health | TX failure, RX overflow, error passive, bus-off, recovery |
| Configuration | forbidden combinations fail before build; manifest is deterministic |
| Output policy | no denied output pulse at boot, reset, fault, or reconnect |
| Artifact | tests, manifest, flashed binary, board, and captures share exact hashes |

## CI Changes

During migration:

- keep RT and SYS tests/builds green as the behavioral reference;
- add Big ESP native tests before target builds;
- add Big ESP target build and static analysis;
- run protocol vector and generation checks once for all controllers;
- add RT/SYS-to-Big parity/replay tests;
- keep Linux/ROS Jetson validation in its supported CI environment;
- mark hardware-only gates explicitly rather than replacing them with fake
  software passes.

After cutover, remove separate `pio-rt` and `pio-sys` release requirements and
replace them with one `pio-big-esp` gate. Historical projects may keep a
non-release build temporarily only if an owner and removal date are recorded.

## Stop Conditions

Stop the current and later phases if:

- the loss of physical RT/SYS isolation is not accepted for the intended use;
- an external consumer of a proposed retired frame is unresolved;
- Big ESP introduces a second protocol definition or competing codec path;
- more than one path can produce an actuator frame;
- ESTOP can be delayed by a full queue or lower-priority body task;
- boot/reset causes an actuator or relay pulse;
- a stale/reconnected input restores authority automatically;
- target timing or memory margin is unmeasured or inadequate;
- the hardware map uses an unsafe/ambiguous ESP32-S3 pin;
- test evidence does not identify the exact firmware/configuration/hardware;
- physical behavior differs from the approved output-inhibited software result.

## Definition of Done

The consolidation is complete only when:

- `big-esp` is the only active RT/SYS controller firmware;
- both physical CAN buses operate with explicit bus-scoped contracts;
- existing required external CAN bytes remain compatible or have a separately
  approved versioned migration;
- `0x205`, `0x7FE`, low-only SYS coordination, and SEB ownership handoff are
  removed where no external consumer exists;
- one mode/safety owner and one SEB command owner exist;
- every physical output passes final output policy;
- broad grouped bypass behavior is absent from vehicle firmware;
- the open-loop, no-encoder, PID-disabled configuration passes native, SIL,
  target, controller, HIL, and isolated physical gates;
- exact artifact and hardware identity is present in every release record;
- wiring, architecture, protocol, test, debug, and flashing documentation agree;
- a reviewed safety decision explicitly accepts the single-ESP common failure
  domain or requires additional independent safety hardware.
