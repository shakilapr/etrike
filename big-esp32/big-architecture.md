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

## Authority and Document Status

This file is the authoritative architecture and execution plan for the
`big-esp` variant. It does not make proposed configuration, commissioning, or
test infrastructure appear implemented before code and evidence exist.

| Subject | Source used by Big ESP | Current status | Big ESP rule |
|---|---|---|---|
| Consolidated architecture | This document | Planning authority | Update this file when a Big ESP design decision changes |
| Protocol migration | `docs/protocol-architecture-migration-plan.md` | Live repository-wide checklist | Big ESP consumes the shared protocol; it does not fork contracts |
| Protocol evidence | `docs/protocol-testing-plan.md` | Normative evidence model | Adopt its evidence levels, verdicts, and cross-language vectors |
| Configuration/unit policy | `docs/rt-sys-feature-configuration-and-test-plan.md` | Proposed requirements | Adapt RT/SYS requirements to one Big ESP artifact |
| Implementation ordering | `docs/rt-sys-configuration-implementation-work-plan.md` | Proposed requirements | Preserve configuration, output, software, HIL, and physical gates |
| Profiles/sessions | `docs/commissioning-test-profiles.md` | Proposed requirements | Replace RT/SYS sessions with Big ESP sessions |
| Distributed validation | `docs/validation/rt-sys-pre-vehicle-validation.md` | Legacy topology | Translate applicable behavior; do not copy RT/SYS heartbeat tests |
| Detailed safety tests | `docs/hil-safety-test-plan.md` | Unexecuted legacy plan | Reclassify component, controller, HIL, and physical tests correctly |
| Conflicting Big ESP design | `big-esp32/big-architecture.md` | Superseded for this variant | Do not use its local protocol copy, pin map, PID, or heartbeat decisions |

The `big-esp32/protocol/contracts/` copies are not an approved protocol source.
Useful contract restructuring must happen once in the repository-wide protocol
tree and be consumed by every ECU and tool.

Until Phase 11 cutover, `rt-esp32` and `sys-esp32` remain behavioral references,
not alternative places to add new product behavior.

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

Big ESP is therefore bench/prototype-only until a consolidation-specific
HARA/FMEA delta and intended-use decision are approved. Approval must name the
permitted ODD: bench, riderless low-speed ground, closed track, or product use.
No lower stage implies approval for a higher stage.

### Consolidation safety delta

| Distributed protection | Effect of consolidation | Required replacement or decision |
|---|---|---|
| SYS survives an RT failure and can command SEB | Big ESP failure removes the sole SEB command producer | Independent brake/hold path, separate safety MCU, proven SEB comm-loss hold, or explicitly accepted coast response for a restricted ODD |
| RT takes over SEB after SYS loss | No handoff exists because there is one owner | One thoroughly tested brake owner; this does not protect against total Big ESP failure |
| SYS independently monitors RT steering/drive intent | Command generation and monitoring share one MCU and configuration | Independent measurement/monitor or explicit statement that no independence/ASIL decomposition is claimed |
| SYS compares RT command with MTR feedback | Big ESP compares its own command with MTR feedback | Independent physical speed evidence and common-cause analysis before calling this EGAS Level 2 |
| Two MCU watchdog/reset domains | One reset removes mode, gateway, steering, brake, and body control together | Per-output electrical safe-state design plus independent MTR motor kill and a resolved brake safe state |
| Cross-MCU heartbeats detect one controller hang | Internal heartbeats become meaningless | Deadline supervision, frozen-snapshot detection, and watchdog service gated by all mandatory task health |
| Separate power/communication failures may leave one controller alive | One rail/MCU fault can remove all consolidated functions | Power-domain and brownout analysis, hardware output enables, and measured reset behavior |

### Mandatory safety conditions

- The physical ESTOP path reaches Big ESP and MTR independently of CAN.
- MTR's local timeout, direct ESTOP, safe DAC/gear state, and acknowledgement are
  required target behavior, but are currently release-blocking and unproven.
- Big ESP output inhibition cannot inhibit MTR manual pass-through, PWT DC-DC,
  an externally injected CAN source, or actuator behavior after Big ESP silence.
  Physical isolation and matching MTR/PWT manifests are also required.
- An external watchdog reset is not itself proof of safe braking. The watchdog
  part, timeout, WDI, reset wiring, transceiver/output enables, and every output
  state during reset must be defined and measured.
- SEB behavior on CAN loss and Big ESP reset is a critical blocker. If SEB
  releases, a deterministic independent brake/hold mitigation is required before
  any moving test.
- The dynamic steering clamp is a single software safety barrier after the
  merge. Independent validation or explicit restricted-use acceptance is
  required before moving beyond bench tests.
- If independent Level 2 supervision is required, retain a separate safety MCU
  or select hardware designed for safety isolation.

No vehicle release is permitted until these conditions and the intended-use
decision are recorded against evidence.

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

## System Responsibility Boundaries

| Unit | Command/control responsibility | Independent safe-state responsibility | Big ESP relationship |
|---|---|---|---|
| Host/Jetson | Perception, planning, `0x300`, `0x301`, `0x302`, `0x400`, `0x7FC` | Stops command publication on Big ESP loss | Required, simulated, or disabled high-CAN peer |
| HMI | Mode and power requests only | No ESTOP-clear or actuator authority | Required, simulated, or disabled high-CAN peer |
| Big ESP | Mode, command bounding, steering, brake, body I/O, two-bus gateway | Safe commands while alive; reset/watchdog behavior must be proven | Artifact under test, never an optional peer in its own configuration |
| MTR | Physical throttle DAC, gear relays, local manual pass-through | Direct ESTOP, command timeout, DAC zero, gear off | Separate low-CAN actuator ECU; currently hardware-incomplete |
| EPS-C/SES | Steering actuation and internal diagnostics | Vendor command-loss/fault behavior | Required, simulated, or disabled low-CAN unit |
| SEB | Brake actuation and internal diagnostics | Vendor command-loss/pressure-hold behavior, currently unverified | Required, simulated, or disabled low-CAN unit |
| PWT | Standalone powertrain-bus DC-DC command | Local timeout/watchdog policy | Separate 250 kbit/s subsystem with no Big ESP bus connection |
| DC-DC | 72 V to 12 V conversion | Vendor power behavior | Controlled by PWT, not Big ESP |
| Traction motor controller | Converts MTR analog/gear outputs to torque | Vendor/local electrical behavior | Plant behind MTR, not a Big ESP CAN peer |
| Control UI/test station | Observation, finite test routines, evidence | Stops TX on lease/session loss | Never a parallel safety controller or unrestricted command source |

Big ESP does not send `0x012`, does not control PWT/DC-DC, and does not claim a
PWT heartbeat or low-to-powertrain gateway. A future Big ESP-to-PWT interface is
a separate network and safety change.

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

Controller consolidation must not silently change payload bytes. Compatibility
has four independent dimensions that must be reviewed for every instance:

| Dimension | Required evidence |
|---|---|
| Payload layout | ID, frame type, DLC, bit layout, scaling, enum, checksum, and counter vectors |
| Physical instance | Exact bus on which the frame exists before and after consolidation |
| Producer/consumer | Approved node ownership and complete external consumer audit |
| Signal semantics | Meaning of every retained legacy RT/SYS field after consolidation |

The wire hash covers payload semantics. A separate network-contract hash covers
bus instances, ownership, rates, routes, and instance lifecycle.

### Complete high-CAN instance plan

| ID | Name | Big ESP action | Migration status |
|---:|---|---|---|
| `0x001` | `SAFETY_ESTOP` | Consume locally, latch, and forward once to low; local ESTOP originates independently on both buses | Preserve with measured gateway policy |
| `0x011` | `SYS_SAFETY_STS` | Produce directly on high CAN | Preserve payload pending legacy-field decision |
| `0x111` | `HMI_MODE_REQ` | Consume directly with counter/freshness validation; never directly clears ESTOP | Remove high-to-low forwarding |
| `0x112` | `HMI_PWR_REQ` | Consume directly under explicit power policy | Remove high-to-low forwarding |
| `0x120` | `SYS_THROTTLE_STS` | Transparently forward from MTR low instance if retained by consumer audit | Preserve legacy name; verify physical meaning |
| `0x206` | `MTR_MOTOR_FBK` | Consume on low and transparently forward to high for Host/tools | Preserve unless a versioned diagnostic replaces it |
| `0x210` | `RT_STATE_RPT` | Produce directly on high | Preserve layout; define Big ESP field semantics |
| `0x220` | `RT_PID_RPT` | Produce only when telemetry semantics are truthful; PID-disabled values must be explicit | Reserved/conditional, not assumed active |
| `0x300` | `HOST_DRIVE_CMD` | Consume directly; requires Host policy, Auto authority, freshness, and bounds | Preserve |
| `0x301` | `HOST_BRAKE_REQ` | Consume directly into typed brake arbitration | Preserve |
| `0x302` | `HOST_LIGHT_CMD` | Consume directly into body arbitration | Remove high-to-low forwarding |
| `0x310` | `STEER_DIAG` | Produce directly from validated EPS-C telemetry | Preserve |
| `0x311` | `BRAKE_DIAG` | Produce directly from validated SEB telemetry | Preserve |
| `0x400` | `HOST_OBSTACLE_DIST` | Consume directly with explicit valid/clear/stale states | Preserve |
| `0x600` | `SYS_DIAG_RPT` | Produce directly on high until a versioned Big ESP diagnostic replaces it | Preserve layout only after semantic mapping |
| `0x7FC` | `HOST_HEARTBEAT` | Consume directly; loss causes controlled assisted stop, not implicit full ESTOP | Preserve |
| `0x7FD` | `RT_HEARTBEAT` | Produce as the legacy high-bus controller heartbeat | Preserve until versioned identity migration |

### Complete low-CAN instance plan

| ID | Name | Big ESP action | Migration status |
|---:|---|---|---|
| `0x001` | `SAFETY_ESTOP` | Consume locally, latch, and forward once to high; local ESTOP originates independently on both buses | Preserve |
| `0x011` | `SYS_SAFETY_STS` | No longer needed to reach RT; retain only if an external low-bus consumer is proven | Candidate retirement |
| `0x110` | `SYS_MODE_CMD` | Produce for MTR and verified low-bus consumers | Preserve legacy name and payload |
| `0x111` | `HMI_MODE_REQ` | No internal SYS receiver remains | Retire low instance |
| `0x112` | `HMI_PWR_REQ` | No internal SYS receiver remains | Retire low instance |
| `0x120` | `SYS_THROTTLE_STS` | Consume from MTR and forward to high if required | Preserve |
| `0x169` | `VCU_SES_REQ` | Sole Big ESP steering command at 50 Hz after LBS | Preserve |
| `0x201` | `SES_STATUS` | Consume for readiness, angle, following error, checksum/counter, and freshness | Preserve |
| `0x202` | `SES_ErrInfo` | Consume; L3 response remains safety policy outside codec | Preserve |
| `0x203` | `SES_Version` | Capture raw/version capability; unsupported semantics remain explicit | Preserve |
| `0x204` | `RT_DRIVE_CMD` | Produce for MTR at 100 Hz after final output policy | Preserve; remove former SYS receiver |
| `0x205` | `RT_BRAKE_CMD` | Replace with typed in-process brake demand | Retire physical instance after parity and consumer audit |
| `0x206` | `MTR_MOTOR_FBK` | Consume for readiness, status, faults, and supervision; forward high | Preserve |
| `0x210` | `RT_STATE_RPT` | No internal SYS receiver remains | Candidate retirement after consumer audit |
| `0x302` | `HOST_LIGHT_CMD` | Consume on high, not forwarded solely for removed SYS | Retire low instance |
| `0x600` | `SYS_DIAG_RPT` | Produce directly on high | Retire low instance |
| `0x6FA` | `SES_Test` | Consume for steering current, temperature, voltage, and diagnostics | Preserve |
| `0x6FB` | `SEB_Test` | Consume for brake current, temperature, voltage, and diagnostics | Preserve |
| `0x721` | `SEB_STATUS` | Consume for LBS, stroke/pressure, following error, counter, checksum, and freshness | Preserve |
| `0x731` | `SEB_ErrInfo` | Consume; fault-level response remains Big ESP policy | Preserve |
| `0x741` | `SEB_Version` | Capture raw/version capability; unsupported semantics remain explicit | Preserve |
| `0x7B9` | `VCU_SEB_REQ` | Sole Big ESP brake command at 50 Hz after LBS | Preserve with one sender and one counter sequence |
| `0x7FD` | `RT_HEARTBEAT` | Retain only if MTR or another external low-bus consumer is proven | Consumer decision required |
| `0x7FE` | `SYS_HEARTBEAT` | Replace with internal deadline/snapshot supervision | Retire |

Before deleting or moving any frame instance, generate a `bus + CAN ID` consumer
report and prove that no ECU, logger, HMI, Control UI, replay fixture, or test
station depends on it.

### Legacy field semantic mapping

| Message/field | Required Big ESP decision |
|---|---|
| `0x011 SYS_HeartbeatOk` | Must not silently change from "RT alive" to "Big ESP healthy". Define a compatible meaning, reserve it, or version the diagnostic. |
| `0x210 RT_TaskHealth` | Define a stable Big ESP task-bit mapping or introduce a versioned health message. |
| `0x210 RT_RxOverflow` | It cannot represent both TWAI and MCP2515 loss. Document the retained controller or version the report. |
| `0x220` PID values | Report disabled/unavailable truthfully; never imply encoder or PID authority. |
| `0x600 SYS_DiagHeartbeatOk` | Define or reserve; old RT-heartbeat meaning disappears. |
| `0x600 TEC/REC` | One pair cannot represent both CAN controllers. Define which bus it covers or add a versioned dual-bus diagnostic. |
| `0x7FD health_flags` | Define Big ESP mode/ESTOP/bus semantics per instance without implying removed SYS liveness. |

No field is reinterpreted merely because its bit position is convenient.

### ESTOP bus policy

- Process and latch every valid `0x001` locally before logging or forwarding.
- A high-bus event is forwarded only to low; a low-bus event only to high.
- A physical/local event schedules independent frames on both buses.
- Echo/loop detection prevents a forwarded event from returning indefinitely.
- Rate limiting bounds repeated TX/logging but never suppresses local ESTOP
  processing.
- ESTOP uses front-of-queue/reserved-driver priority and measured worst-case
  latency; queue-full behavior must still retain the local safety state.
- TX failure is counted and retried according to a bounded safety policy.
- A wedged high-bus MCP2515/SPI path must not delay low-bus ESTOP/control.

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

## Vehicle Mode and Fault Response

Vehicle modes are independent from firmware profiles and commissioning
sessions. `Manual`, `Auto`, and `Estop` never grant engineering-test authority.

### Mode behavior

| Mode | Drive/MTR | Steering | Brake | Body/power | Entry/recovery |
|---|---|---|---|---|---|
| Manual | Big ESP sends no autonomous nonzero drive; MTR manual behavior is separately governed by MTR policy | No autonomous `0x169`; EPS-C manual behavior is monitored | Physical lever through the single Big ESP SEB owner | Manual indication; normal switch arbitration | Boot default unless reset-latch policy requires Estop acknowledgement |
| Auto | Fresh bounded Host command may produce `0x204` | LBS complete, fresh valid EPS-C, dynamic/hard limits, following-error supervision | ESTOP > lever > safety stop > Host/obstacle > release | Host lights merge with local safety overrides | Requires explicit mode request and all required unit/readiness gates |
| Estop | `0x204` zero/N while Big ESP can transmit; MTR direct ESTOP remains independent | Approved ramp/hold/silent behavior based on trigger | Maximum approved brake while Big ESP/SEB path is available | Brake/ESTOP indicators on; accessory relay policy safe/off | Trigger must clear; START or MODE long-press exits only to Manual; never directly to Auto |

### Trigger response matrix

The exact numeric limits and timing come from versioned configuration/test
policy, not this table. Any change is reviewed and tested as a safety-policy
change.

| Trigger | Classification | Drive/gear | Steering | Brake | Recovery |
|---|---|---|---|---|---|
| Physical ESTOP or valid `0x001` | Full Estop | Zero/N plus MTR direct hardware kill | Trigger-specific safe ramp/hold/silence | Maximum if command path remains available | Explicit Manual-only acknowledgement after trigger clears |
| EPS-C L3/following error | Full Estop | Zero/N | Bounded ramp or silent fault behavior | Maximum | Manual-only after fault policy and actuator resync |
| SEB L3/brake following failure | Full Estop/fault | Zero/N | Safe steering response | Best available brake; inability to apply is a latched critical fault | No release until fault and brake-safe-state decision are resolved |
| MTR fault, missing feedback, or ESTOP ACK failure | Full Estop/fault | MTR local kill expected; Big ESP commands zero/N | Safe steering response | Maximum | Manual-only after MTR stable-health window |
| Host heartbeat loss | Controlled assisted stop, not full Estop | Zero/N | Stop/ramp under approved policy | Bounded assisted pressure | Fresh stable Host plus explicit Auto authority revalidation; no stale replay |
| Host drive-command stale | Coast/safe stop | Zero/N | Stop/ramp under approved policy | Lever/safety brake only unless policy escalates | Only a new valid sequence after freshness/readiness checks |
| Obstacle emergency threshold | Full Estop or approved emergency stop | Zero/N | Dynamic-clamped hold then silence, as validated | Maximum | Manual-only after obstacle state and operator acknowledgement |
| High CAN loss/bus-off | Controlled assisted stop | Zero/N | Stop/ramp | Bounded assisted pressure | Stable bus/Host identity plus explicit Auto revalidation |
| Low CAN loss/bus-off | Full actuator-communication fault | TX unavailable; MTR timeout must kill locally | Actuator vendor timeout behavior | SEB vendor timeout behavior, currently release-blocking | Resync LBS and Manual-only recovery |
| Mandatory task/snapshot deadline missed | Full Estop then watchdog if unresolved | Safe snapshot if TX alive | Safe snapshot if TX alive | Maximum if path alive | Reset, persistent record, full resync, no prior authority |
| Big ESP watchdog/reset/brownout | Hardware-defined failure state | MTR direct timeout/ESTOP required | EPS-C command-loss behavior | SEB behavior unresolved and release-blocking | Boot safe, re-run LBS, require Manual acknowledgement; never restore session/command |

### ESTOP persistence and exit

- Reset/power-cycle persistence is an explicit safety decision; booting to
  Manual must not silently clear an unresolved physical or latched critical
  trigger.
- Physical ESTOP release alone does not resume actuation.
- START short press or MODE long-press can request exit only after mandatory
  trigger-clear, actuator-safe-state, and resynchronization conditions pass.
- Steering centering completion may delay handoff; brake and motor safety remain
  active during that delay.
- HMI, Host, ordinary CAN, reconnect, or a commissioning request cannot clear
  ESTOP.

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


# Part 2: Explicit big-esp32 Unified Implementations

# Big-ESP32 Unified Architecture

This document defines the complete architecture for the unified `big-esp32` controller, which replaces the legacy dual-board (`sys-esp32` and `rt-esp32`) topology on the E-Trike. By consolidating real-time kinematics and system body/safety controls onto a single ESP32-S3 ECU, we eliminate inter-MCU communication delays, simplify state management, and enforce a localized CAN protocol model.

---

## 1. Top-Level Responsibilities

The `big-esp32` is the central vehicle control unit, acting as the sole interface between the High CAN bus (Jetson ORIN) and the Low CAN bus (Actuators). Its primary domains are:

1. **CAN Gateway & Protocol Enforcement:** Routes telemetry and commands between Jetson and Actuators, translating and enforcing protocol integrity.
2. **Vehicle Dynamics & Kinematics:** Executes the real-time physics model, calculates speed PID targets, and interpolates steering angles.
3. **Safety & Mode Supervision:** Maintains the global system state (ESTOP / MANUAL / AUTO), monitoring hardware limits, heartbeat timeouts, and EGAS fault conditions.
4. **Body Control:** Drives relays and bulbs for turn signals, headlights, brake lights, and reads physical switches (mode button, ESTOP mushroom button, ignition).

---

## 2. Protocol Ownership and YAML Rules

`big-esp32` departs from the legacy "monolithic shared folder" design. Instead, the controller strictly owns its local protocol definition to guarantee deterministic generation and complete test decoupling.

### Divide Contracts Without Duplicating Messages
Instead of monolithic `can_high.yaml` and `can_low.yaml` files, the CAN protocol definitions inside `big-esp32/protocol/contracts/` are divided logically by originating ECU and protocol family:

- `network.yaml`: Contains buses, nodes, bitrate, and forwarding references.
- `host.yaml`: Jetson-originated message definitions.
- `big-esp32.yaml`: Messages originated by this unified controller (merging the legacy RT and SYS definitions).
- `mtr.yaml`: Motor controller message definitions.
- `ses.yaml`: Externally owned steering protocol.
- `seb.yaml`: Externally owned braking protocol.
- `pwt.yaml`: PWT/DC-DC manufacturer protocol.

- **Single Source of Truth:** Each message layout is defined exactly once in its respective file. A receiver never duplicates the YAML layout. Forwarded routes are represented by reference.
- **Bus Instances:** The network topology is defined by explicit bus instances (`bus` + `CAN ID`). Runtime identity is always resolved by the physical bus instance.

### CAN Codec Strategies (Payload Integrity)
To decouple stateless payload parsing from stateful wire supervision, every CAN message in the YAML strictly selects exactly **one** codec strategy:

1. **`generated`**: An ordinary, stateless C++ payload codec is generated deterministically from the layout.
2. **`profile`**: A small named and versioned integrity implementation is applied (e.g., a repeated XOR checksum, or an AUTOSAR E2E profile). The `profile` method encapsulates sequence counters, freshness checks, and checksum validation, evaluating integrity *before* the payload is parsed or utilized.
3. **`custom`**: An explicit handwritten codec owns the algorithm. This is reserved solely for vendor protocols (like legacy EPS-C or SEB) where manufacturer-specific overlapping bits or undocumented state machines cannot be mapped via generic generation or standard profiles.

---

## 3. Hardware Architecture & Complete GPIO Unification

Merging SYS and RT into a single ESP32-S3 requires resolving overlapping pin assignments from the legacy `config.h` files. Below is the complete, unified, conflict-free pinout for `big-esp32`:

### CAN Interfaces (No conflicts)
- **Low CAN (Native TWAI):** `kCanLowTxGpio` = 5, `kCanLowRxGpio` = 4
- **High CAN (SPI MCP2515):** `kSpiSckGpio` = 15, `kSpiMosiGpio` = 16, `kSpiMisoGpio` = 17, `kSpiCsGpio` = 18, `kMcpIntGpio` = 7

### Safety & System Inputs (Kept on legacy SYS pins)
- `kEstopGpio` = 1 (Active-low, physical mushroom)
- `kBrakeLeverGpio` = 2 (Active-low)
- `kIgnitionGpio` = 8
- `kModeBtnGpio` = 11
- `kStartBtnGpio` = 41

### Encoders (Quadrature PCNT) (Moved to resolve conflicts)
*Legacy RT pins (1, 2, 6, 9, 10, 12, 13, 14) conflicted with SYS. Moved to higher unused GPIOs.*
- `kEncRearMotorA` = 35, `kEncRearMotorB` = 36
- `kEncFrontWheelA` = 37, `kEncFrontWheelB` = 38
- `kEncRearLeftA` = 42, `kEncRearLeftB` = 43
- `kEncRearRightA` = 44, `kEncRearRightB` = 47

### Body Control & Lighting (Resolved minor conflicts)
- `kSwitchRightTurn` = 6
- `kSwitchLeftTurn` = 9
- `kLightHead` = 10
- `kSwitchHeadlight` = 12 *(Moved from 7 to avoid MCP INT)*
- `kLightLeftTurn` = 14 *(Moved from 18 to avoid SPI CS)*
- `kLightRightTurn` = 19
- `kLightBrake` = 21

### Mode Indicator Bulbs & Relays (Resolved minor conflicts)
- `kBulbReady` = 13 *(Moved from 17 to avoid SPI MISO)*
- `kBulbEstop` = 20
- `kBulbManual` = 39
- `kPower12vRelay` = 40
- `kBulbAuto` = 48

---

## 4. State Machine & Mode Control

The `big-esp32` operates an internal, synchronous state machine determining the vehicle's capability to actuate motors and steering.

```
[MANUAL] <────────(Mode Button)────────> [AUTO]
   |                                        |
   v                                        v
[ ESTOP ] <──────(Faults, Button, CAN)──────┘
```

- **MANUAL Mode:** Operator steers manually. Brake lever directly commands the SEB (brake-by-wire). Jetson drive commands (`HOST_DRIVE_CMD`) are ignored.
- **AUTO Mode:** The Jetson commands steering (`0x300`), speed, and braking. Actuation is managed by the `control` task.
- **ESTOP State:** A hardware-enforced overlay. Triggers include the physical ESTOP button, Jetson heartbeat timeout, EGAS tracking faults, or Low CAN timeouts. When active, `big-esp32` immediately sets speed target to 0, ramps steering to center, and commands maximum braking. Exit requires holding the `kStartBtnGpio`.

---

## 5. RTOS Task Schedule

Tasks are explicitly pinned to CPU cores and prioritized to ensure real-time determinism.

| Task Name        | Core | Priority | Freq | Description |
|------------------|------|----------|------|-------------|
| `can_rx_high`    | 1    | 5 (Highest)| ISR  | Polls MCP2515 INT pin, drops High CAN frames into `rx_queue`. |
| `can_rx_low`     | 1    | 5        | ISR  | TWAI driver event loop, drops Low CAN frames into `rx_queue`. |
| `can_dispatch`   | 1    | 4        | Asyc | Pops `rx_queue`, applies `profile` integrity checks, updates memory structs. |
| `control_loop`   | 1    | 4        | 100Hz| Reads unified state, executes PID & Steering interpolation, pushes to `tx_queue`. |
| `can_tx_high`    | 1    | 3        | Asyc | Flushes outgoing High CAN frames to MCP2515. |
| `can_tx_low`     | 1    | 3        | Asyc | Flushes outgoing Low CAN frames to TWAI. |
| `body_lights`    | 0    | 2        | 50Hz | Reads switches, drives relays and indicator bulbs. |
| `body_mode`      | 0    | 2        | 50Hz | Debounces mode button, manages mode transitions. |
| `safety_monitor` | 0    | 1        | 20Hz | Cross-checks Jetson heartbeat, EGAS faults, controls ESTOP event flag. |

---

## 6. Diagnostic and Telemetry Strategy

`big-esp32` natively multiplexes telemetry without requiring external bus polling:
- `RT_STATE_RPT` (High CAN): Broadcasts the unified `MANUAL/AUTO/ESTOP` state, task health bits, and steering angle feedback.
- `RT_PID_RPT` (High CAN): Shadow telemetry of the active PID controller parameters.
- `STEER_DIAG` / `BRAKE_DIAG` (High CAN): Translated status bytes originally emitted by the EPS-C and SEB units on the Low CAN bus. 

---

## 7. Remediation of Legacy Architecture Gaps

This unified architecture explicitly resolves the following gaps identified in `architecture-yaml-code-gaps.md`:

### Frame & Payload Gaps (FRM)
- **FRM-001 (`0x210` ambiguous routing):** Resolved. `big-esp32` generates `0x210 RT_STATE_RPT` solely for the Jetson over High CAN. Since `SYS` functionality is now internal, the low bus transmission of `0x210` is retired.
- **FRM-002, FRM-003, FRM-004 (Heartbeat collisions and bit packing):** Resolved. We eliminate `SYS_HEARTBEAT` and `RT_HEARTBEAT` passing between ESP32s entirely. A single `VEHICLE_HEARTBEAT` (DLC 2) is sent to the Jetson encompassing the unified safety state.

### RT Implementation Gaps (RT)
- **RT-001 (Task CPU Affinity):** Resolved. All tasks are explicitly pinned using `xTaskCreatePinnedToCore` as shown in Section 5, guaranteeing that body/I/O tasks on Core 0 cannot preempt critical control tasks on Core 1.
- **RT-002 (Safety Queue Overflows using `xQueueOverwrite`):** Resolved. We replace the depth-16 safety queue with FreeRTOS **Event Groups**. Event flags natively coalesce redundant state transitions without overflowing, safely mitigating burst failures.
- **RT-003 (Gateway drop counters missing):** Resolved by elimination. Because the High-to-Low gateway logic is internalized into a shared memory dispatch loop, inter-bus CAN forwarding drops no longer exist as a failure mode.
- **RT-004 (Log Flooding):** Resolved. `profile` integrity faults (like E2E checksum failures) increment an internal counter rather than `printf`ing every frame. Telemetry exports the counters instead of flooding the UART.

### SYS Implementation Gaps (SYS)
- **SYS-001 (`SYSTEM_RUN_MODE` hardcoded):** Resolved. Run modes (`vehicle`, `bench`, `hardware_bench`) are injected exclusively via PlatformIO environments (`-D ETRIKE_SYSTEM_RUN_MODE=X`) and are no longer hardcoded in `system_mode.h`.
- **SYS-002 (`g_brake_fault_active` never cleared):** Resolved. The unified `safety_monitor` task owns all fault states and applies explicit hysteresis timers for recovery, eliminating permanently latched, un-clearable phantom faults.
- **SYS-003 (TEC `< 255` treated as CAN OK):** Resolved. The `can_dispatch` layer reads the native TWAI status flags (Error Active, Error Passive, Bus-Off) and accurately degrades the unified system state to ESTOP if Error Passive is reached, rather than waiting for full Bus-Off.
- **SYS-004 (Bench vs Vehicle wiring proof):** Resolved via PlatformIO environments isolating simulated logic from production wiring.
- **SYS-005 (Checksum failure logs):** Addressed alongside RT-004 using `profile` payload codec strategies to quietly aggregate checksum streak violations.
