# RT/SYS Feature Configuration and Test Plan

## Status

This document is an implementation and verification plan. The configuration
model, manifest, tests, and flashing gate described here are not yet fully
implemented.

Do not use the current broad `bench` or GPIO42 override behavior as permission
to operate a physical actuator. Current bench behavior does not provide a
complete output inhibit or one-actuator allowlist.

## Objective

The project must support building RT and SYS with optional hardware and control
features deliberately enabled or disabled. A selected configuration must be:

- explicit in version-controlled build input;
- rejected before compilation when internally inconsistent;
- used by native tests, simulation, target builds, and test tooling;
- compiled into the firmware and visible at startup;
- represented by a configuration hash;
- connected to an exact firmware binary hash;
- software-tested before that exact binary is flashed;
- hardware-tested one physical unit at a time;
- unable to change through an ordinary operational CAN signal.

The first required configuration is the complete open-loop vehicle with the RT
encoder subsystem disabled and speed PID disabled.

## Dependency Order and Hard Gates

Implementation is firmware-first. Do not begin Control UI synthetic-peer/HIL
work or physical-unit execution while the enable/disable foundation and its
software verification are incomplete.

| Foundation stage | Scope | Hardware/CAN transmission |
|---:|---|---|
| 1 | Configuration types, unit policies, output permissions, encoder/PID settings, validation | None |
| 2 | RT/SYS production-code integration and safe disabled behavior | None |
| 3 | Host/native unit, component, state-machine, and configuration-matrix tests | None |
| 4 | Deterministic artifacts, manifest, hashes, and CI enforcement | None |
| 5 | Pure software/SIL integration and fault scenarios | Virtual buses only |
| 6 | Controller-only and HIL through future Control UI backend | Isolated CAN bench, no physical actuators |
| 7 | One-unit-at-a-time physical integration | Only the approved physical unit |
| 8 | Constrained full physical integration | All earlier gates must pass |

Foundation Stage 6 is blocked until Stages 1-5 pass. Stage 7 is blocked until the
applicable Stage 6 controller/HIL evidence passes. No passing later test may
compensate for a missing or failed earlier stage.

## Scope

This document covers:

- presence and test policy for Host, HMI, MTR, EPS-C, SEB, PWT, DC-DC, and the
  traction motor controller;
- the RT PCNT encoder subsystem as one enable/disable feature;
- RT speed-feedback selection;
- RT PID disabled, shadow, and active states;
- RT/SYS build integration and capability reporting;
- configuration validation;
- software, CI, controller, HIL, and isolated hardware tests;
- artifact approval and flashing.

This document does not make SYS an encoder or PID owner. It does not define PID gains or approve
closed-loop vehicle operation.

## Actual Code Baseline

### System unit inventory

The system contains controllers, CAN actuator units, command sources, local
sensors, and physical plant. They do not all use the same configuration
mechanism.

| Unit | Connection | Owner/consumer | Configuration relevance |
|---|---|---|---|
| Host / Jetson | High CAN | RT | External command source; may be physical, simulated, or disabled for isolated tests |
| RT | High + low CAN | System controller | Firmware under test; not a peer toggle inside RT |
| SYS | Low CAN | System controller | RT peer and firmware under test |
| HMI | High CAN through RT forwarding | SYS/Host | Optional command source; may be physical, simulated, or disabled |
| MTR | Low CAN plus motor I/O | RT/SYS | Planned motor actuator ECU; hardware implementation is incomplete |
| EPS-C / SES | Low CAN | RT | Physical steering actuator and feedback unit |
| SEB | Low CAN | RT/SYS | Physical brake actuator and feedback unit with shared command authority |
| PWT | Standalone 250 kbit/s powertrain CAN | Powertrain subsystem | Separate firmware; not currently an RT/SYS low-bus peer |
| DC-DC converter | Powertrain CAN | PWT | Physical power actuator commanded directly by PWT |
| Traction motor controller | Analog throttle/gear from MTR; telemetry CAN TBD | MTR/PWT | Physical plant behind MTR, not an RT/SYS CAN-peer switch |
| RT encoder subsystem | Local PCNT GPIO | RT | Optional local sensor subsystem; one build enable/disable setting |
| SYS body I/O | Local GPIO | SYS | Local hardware capability, not an external CAN unit |

The current implemented gateway is RT between high and low CAN. PWT has one
powertrain CAN interface and is not a low-to-powertrain gateway. Configuration
must not claim the older gateway topology.

Unit presence has two distinct effects:

- whether firmware requires and monitors the unit;
- whether commands toward that unit are allowed.

These effects must remain separate. Marking EPS-C physical does not by itself
authorize steering output. A controller-only build may simulate EPS-C while all
actuator output remains inhibited. A steering bench may require physical EPS-C
and allow only steering messages.

### RT responsibilities

RT currently:

- receives Host commands on high CAN;
- calculates steering, speed, and brake setpoints;
- sends `RT_DRIVE_CMD` to MTR/SYS;
- sends `RT_BRAKE_CMD` to SYS;
- sends steering requests directly to EPS-C;
- sends direct SEB requests in Auto and during SYS-loss takeover;
- bridges allowlisted frames between high and low CAN;
- receives MTR `MTR_MOTOR_FBK` and uses its reported speed for steering and PID
  telemetry.

Relevant code:

- [`rt-esp32/src/main.cpp`](../rt-esp32/src/main.cpp)
- [`rt-esp32/src/can_dispatch.h`](../rt-esp32/src/can_dispatch.h)
- [`rt-esp32/src/steering_control.h`](../rt-esp32/src/steering_control.h)
- [`rt-esp32/src/safety_monitor.h`](../rt-esp32/src/safety_monitor.h)

### SYS responsibilities

SYS currently:

- owns Manual, Auto, and Estop authority;
- reads physical ESTOP, mode, start, brake, and light inputs;
- drives lamps, indicators, and the 12 V relay;
- normally commands SEB and arbitrates that authority with RT;
- monitors RT command freshness and heartbeat;
- compares RT drive commands with MTR feedback;
- monitors MTR and SEB status and faults.

SYS does not currently own speed PID or the RT PCNT encoders.


### MTR responsibilities

MTR is intended to own motor DAC and gear actuation. Its current
`actual_speed_mmps` is not proven independent encoder feedback. In Auto it is
largely derived from the command. It must not be treated as validated physical
closed-loop feedback until the MTR implementation and hardware tests prove that
claim.

### Current encoder implementation

RT declares four planned channels:

| Index | Name | GPIO A | GPIO B | Current status |
|---:|---|---:|---:|---|
| 0 | Rear motor | 1 | 2 | Planned speed-control feedback |
| 1 | Front wheel | 10 | 6 | Sensor TBD; header comment currently says GPIO10 |
| 2 | Rear left | 9 | 12 | Sensor TBD |
| 3 | Rear right | 13 | 14 | Sensor TBD |

Sources:

- [`rt-esp32/src/config.h`](../rt-esp32/src/config.h)
- [`rt-esp32/src/encoder_pcnt.h`](../rt-esp32/src/encoder_pcnt.h)
- [`rt-esp32/src/encoder_pcnt.cpp`](../rt-esp32/src/encoder_pcnt.cpp)

Current limitations:

- `ETRIKE_RT_ENCODERS` enables all four channels together.
- `app_main()` does not call `encoder_init()`.
- The control task does not read PCNT encoder data.
- Disabled stubs return numeric zero and do not distinguish disabled from valid
  stationary feedback.
- Read failures also return numeric zero.
- Speed conversion uses the accumulated counter rather than a cleared or stored
  interval delta.
- The implementation configures a simplified 2x count path while conversion and
  logs claim 4x decoding.
- PPR and wheel geometry are hard-coded placeholders.
- Per-channel health, freshness, calibration, and initialization state do not
  exist.

### Current PID implementation

RT runs shadow or active PID depending on `ETRIKE_RT_PID_MODE` and the selected `ETRIKE_RT_SPEED_FEEDBACK_SOURCE` (which provides the measured speed).

`ETRIKE_RT_PID_MODE=2` (active) conditionally adds the PID correction to the local setpoint.

Additional limitations:

- A zero measurement is treated as both stationary and missing feedback.
- Gains are placeholders and saturate against raw mm/s errors.
- No feedback freshness, direction, plausibility, or bumpless-transfer gate
  exists.
- No PID configuration identity is reported.

### Current build and test limitations

- RT and SYS provide `vehicle`, `bench`, `hardware_bench`, and `native`
  environments.
- CI builds vehicle profiles but does not exercise the encoder/PID matrix.
- RT native excludes `main.cpp` and `encoder_pcnt.cpp`, so it does not test
  startup configuration, encoder hardware integration, or final queue ordering.
- Existing bypass tests are placeholders.
- Simulation does not model encoder/PID feature configuration.
- No RT/SYS feature manifest or configuration hash exists.
- `FW_VERSION` is defined by PlatformIO but is not currently published by RT or
  SYS code.

## Design Decisions

### Build-time, not operational CAN control

Encoder acquisition, feedback source, and PID authority are selected when the
firmware artifact is built. They remain immutable while that artifact runs.

An ordinary CAN signal must not enable or disable them because:

- a frame can be injected, replayed, delayed, duplicated, or corrupted;
- the runtime state would no longer match the tested artifact configuration;
- a transition could occur while the vehicle is moving;
- RT, SYS, Host, and the test station could disagree about effective behavior;
- an unexpected reset could restore a different state.

CAN may report the compiled configuration and current health. It may not change
PID authority in the initial implementation.

If runtime reconfiguration is added later, it must use a separate restricted
diagnostic protocol and a reviewed safety design. It must require stopped
Manual state, safe gear, zero output, physical authorization, explicit timeout,
and complete transition tests.

### Boolean switches only where behavior is binary

The RT encoder subsystem is one `0` or `1` build switch. The installed encoder
channel set belongs to the reviewed hardware map and calibration, not to a list
of user-facing enable switches. Feedback source, PID authority, and unit policy
are multi-state selectors because one bit cannot represent their required
meanings truthfully.

### Unit policy

Every external unit relevant to an ECU has one immutable build policy:

| Value | Policy | Meaning |
|---:|---|---|
| `0` | Disabled | Unit is intentionally absent; no readiness/freshness dependency and no output toward it |
| `1` | Required physical | Unit must boot, identify, provide fresh valid status, and follow the physical test/release contract |
| `2` | Simulated | Deterministic test equipment supplies/consumes the unit protocol; test-only artifact |

`Simulated` is forbidden in a vehicle release artifact. A runtime unit that was
configured as required physical but disappears is faulted, not disabled.

### Meaning of simulated

`Simulated` does not mean that an EPS-C, SEB, MTR, Host, HMI, PWT, or DC-DC model
runs inside normal RT or SYS firmware.

| Test level | Where the simulated unit runs | Where RT/SYS runs |
|---|---|---|
| Native/unit test | In the host test process on the development computer | Production core logic compiled for the computer |
| SIL | In the software simulation on the development computer | RT/SYS software models or host-compiled production core |
| Controller/HIL | On a PC test application, CAN test station, or dedicated simulator connected to CAN | Real RT/SYS firmware on the ESP32 board |
| Physical integration | No simulator for the unit under test; the real unit is connected | Real RT/SYS firmware on the ESP32 board |

For controller/HIL tests, the external simulator sends the same generated CAN
frames, timing, counters, status, and faults as the missing physical unit. It
also receives and validates commands that RT/SYS would have sent to that unit.
RT/SYS uses its normal production decode, state-machine, timeout, and safety
logic.

CAN itself cannot prove whether a frame came from a physical unit or a test
station. The `Simulated` policy is therefore test metadata and an artifact
restriction, not source authentication. The test harness, wiring manifest, and
captured evidence establish which device was connected.

On-device ESP unit tests may use fake HAL implementations to test a driver or
task with actuator power disconnected. That is a separate test firmware and
must not be confused with a vehicle or physical-integration artifact.

Vehicle firmware must not contain an internal task that fabricates healthy peer
heartbeats or actuator feedback. Doing that could hide a disconnected or failed
physical unit.

The unit policy does not replace an output allowlist. Physical actuator output
remains separately inhibited or explicitly allowed by the selected test setup.

### Optional means intentionally absent, not failed

The software must distinguish:

| State | Meaning |
|---|---|
| Disabled | Configuration deliberately excludes the channel |
| Initializing | Configured channel has not completed startup |
| Valid | Fresh plausible sample is available |
| Stale | Last valid sample exceeded its freshness limit |
| Faulted | Initialization, read, direction, scaling, or plausibility failed |
| Unavailable | Required source cannot currently provide a sample |

Disabled channels do not generate missing-sensor faults. Enabled channels that
fail must not silently become disabled or valid zero.

### Safety functions are not feature switches

The following remain mandatory and cannot be disabled by this configuration:

- ESTOP handling;
- command freshness;
- safe boot outputs;
- reset and watchdog behavior;
- CAN DLC, checksum, counter, and range validation;
- steering, brake, speed, gear, and output hard limits;
- invalid mode rejection;
- safe response to missing required feedback;
- no automatic command restoration after reset or reconnect.

## Configuration Model

### Unit settings

The canonical system configuration defines:

| Setting | Used by | Unit |
|---|---|---|
| `ETRIKE_UNIT_HOST` | RT | Host / Jetson |
| `ETRIKE_UNIT_SYS` | RT, integration tooling | SYS |
| `ETRIKE_UNIT_HMI` | RT, SYS | HMI |
| `ETRIKE_UNIT_MTR` | RT, SYS | MTR |
| `ETRIKE_UNIT_EPS_C` | RT | EPS-C / SES steering unit |
| `ETRIKE_UNIT_SEB` | RT, SYS | SEB brake unit |
| `ETRIKE_UNIT_PWT` | System manifest, PWT tooling | PWT powertrain controller |
| `ETRIKE_UNIT_DCDC` | PWT | DC-DC converter |
| `ETRIKE_UNIT_MOTOR_CONTROLLER` | MTR/PWT test tooling | Traction motor controller |

Each setting uses `0=disabled`, `1=required physical`, or `2=simulated`.
Each ECU consumes only relevant settings, but all artifacts and test reports use
one canonical system configuration hash.

RT and SYS themselves are not disableable features in their own firmware. A
controller-only test selects which artifact is running and configures its peers.

### Output permissions

Unit policy is not sufficient to authorize physical output. The test/release
configuration also declares final output permissions:

| Output class | Producer | Physical destination |
|---|---|---|
| `RT_STEERING` | RT | EPS-C request |
| `RT_DRIVE` | RT | MTR drive command |
| `RT_BRAKE_TO_SYS` | RT | SYS brake request |
| `RT_BRAKE_DIRECT` | RT | SEB direct request |
| `RT_GATEWAY` | RT | High/low forwarding |
| `SYS_BRAKE` | SYS | SEB request |
| `SYS_BODY` | SYS | Lamps, indicators, and 12 V relay |
| `MTR_MOTOR` | MTR | DAC and gear outputs |
| `PWT_DCDC` | PWT | DC-DC command |

Each output permission is a `0` or `1` setting, for example
`ETRIKE_OUTPUT_RT_STEERING`, `ETRIKE_OUTPUT_RT_DRIVE`,
`ETRIKE_OUTPUT_RT_BRAKE_TO_SYS`, `ETRIKE_OUTPUT_RT_BRAKE_DIRECT`,
`ETRIKE_OUTPUT_RT_GATEWAY`, `ETRIKE_OUTPUT_SYS_BRAKE`,
`ETRIKE_OUTPUT_SYS_BODY`, `ETRIKE_OUTPUT_MTR_MOTOR`, and
`ETRIKE_OUTPUT_PWT_DCDC`.

Controller-only and HIL configurations inhibit physical actuator classes even
when simulated peers are enabled. An isolated physical test enables only the
class required by its procedure.

### Encoder subsystem switch

Add one build setting:

| Setting | Values |
|---|---|
| `ETRIKE_RT_ENCODERS` | `0` subsystem disabled, `1` subsystem enabled |

When enabled, RT initializes the encoder channels listed by the approved
hardware map. The hardware map may initially contain only the rear motor
encoder. Front, rear-left, and rear-right remain unavailable until their
hardware, calibration, and tests are approved. Users do not toggle channels
individually in PlatformIO.

### Speed feedback source

Add `ETRIKE_RT_SPEED_FEEDBACK_SOURCE`:

| Value | Source | Meaning |
|---:|---|---|
| `0` | None | Open-loop control has no speed feedback dependency |
| `1` | MTR report | Use `MTR_MOTOR_FBK` for telemetry/supervision only until proven physical |
| `2` | RT rear motor encoder | Use validated RT PCNT rear-motor encoder samples |
| `3` | Calculated | Use estimated speed calculated from executed motor commands and plant model |

MTR report must not be accepted as active closed-loop feedback until an
independent physical measurement path is implemented and validated.

### PID authority

Add `ETRIKE_RT_PID_MODE`:

| Value | State | Effect on command |
|---:|---|---|
| `0` | Disabled | PID is reset and does not calculate or affect output |
| `1` | Shadow | PID calculates telemetry but cannot affect output |
| `2` | Active | PID correction affects the bounded drive setpoint |

### Kinematics resolver strategy

Add `ETRIKE_RT_KINEMATICS_RESOLVER` (implemented in a separate file):

| Value | Strategy | Meaning |
|---:|---|---|
| `0` | Bicycle Kinematics | Resolves commands via standard tricycle bicycle model kinematics (`physics_model.cpp`) |
| `1` | Direct Passthrough | Bypasses kinematics solver, directly mapping/scaling commands (`direct_resolver.cpp`) |

**Implementation Requirement: Compile-Time Type Aliasing**
To prevent `#ifdef` spaghetti code in the core control loop, the selection of the active kinematics resolver must use **Compile-Time Type Aliasing** (the Policy Pattern). 

What to do for each:
1. **Centralize Macro Logic in a Header**: Create a single configuration header (e.g., `resolver_config.h`) that evaluates the `ETRIKE_RT_KINEMATICS_RESOLVER` macro and defines a static type alias (`ActiveResolver`):
   ```cpp
   #pragma once
   #if ETRIKE_RT_KINEMATICS_RESOLVER == 1
       #include "direct_resolver.h"
       namespace rt { using ActiveResolver = DirectResolver; }
   #else
       #include "physics_model.h"
       namespace rt { using ActiveResolver = PhysicsModel; }
   #endif
   ```

2. **Keep the Control Loop Branch-Free**: In `main.cpp`, statically allocate and use the aliased type. Do not use `#if` blocks in the execution path.
   ```cpp
   #include "resolver_config.h"
   
   rt::ActiveResolver g_resolver;
   
   // Inside t_control loop:
   rt::ResolvedSetpoint sp;
   g_resolver.resolve({cmd.speed_mmps, cmd.yaw_rate_mrad_s}, sp);
   ```
   This approach guarantees zero runtime overhead (no dynamic allocation or virtual functions) and ensures the control loop code remains clean and universally readable regardless of the active configuration.

### First required configuration

The complete no-encoder open-loop system uses:

```ini
-D ETRIKE_RT_ENCODERS=0
-D ETRIKE_RT_SPEED_FEEDBACK_SOURCE=0
-D ETRIKE_RT_PID_MODE=0
-D ETRIKE_RT_KINEMATICS_RESOLVER=0
```

Its unit settings are selected separately. A full release eventually requires
physical Host, SYS, MTR, EPS-C, SEB, PWT, DC-DC, and motor-controller policies
that match the approved topology. Until MTR/PWT deployment blockers are closed,
the manifest must report those capabilities unavailable rather than claiming a
complete vehicle release.

Expected behavior:

- no PCNT unit is initialized;
- no encoder readiness or freshness is required;
- no encoder fault is emitted;
- PID state is reset and remains inactive;
- drive commands use the validated bounded open-loop mapping;
- all other RT/SYS functions continue;
- diagnostics report all encoder-dependent capabilities unavailable;
- ESTOP, braking, steering, command timeout, mode, CAN, and watchdog behavior
  remain active.

### Other supported feature configurations

| Encoder subsystem | Feedback source | PID | Purpose |
|---:|---:|---:|---|
| 0 | None | Disabled | Open-loop operation without speed feedback |
| 0 | MTR report | Disabled | Open loop with existing MTR telemetry/supervision |
| 1 | None | Disabled | Encoder-subsystem electrical and telemetry checkout |
| 1 | RT rear encoder | Disabled | Open loop with validated encoder telemetry/supervision |
| 1 | RT rear encoder | Shadow | Open-loop actuation with PID analysis only |
| 1 | RT rear encoder | Active | Closed-loop control after all acceptance gates |

### Forbidden combinations

The build must reject:

- any unit policy outside `0..2`;
- simulated units in a vehicle release;
- any encoder-subsystem value outside `0` or `1`;
- feedback source outside `0..2`;
- PID mode outside `0..2`;
- RT rear-encoder feedback source while the encoder subsystem or rear-encoder
  hardware map entry is disabled;
- shadow PID with no feedback source;
- active PID with no feedback source;
- active PID using MTR report until that source is independently approved;
- active PID without RT rear encoder in control-quality state;
- actuator output enabled for a disabled unit;
- more than one physical actuator class in an isolated-unit test;
- PWT configured as an RT/SYS low-bus peer in the current standalone topology;
- DC-DC configured as directly controlled by RT or SYS in the current topology;
- vehicle builds with test-only synthetic feedback;
- SYS motor ownership.

## Build Integration

### Generated central configuration header

Generate `rt-esp32/src/build_config.h` from the canonical system configuration.
It should:

- define safe defaults only for explicitly identified non-vehicle tooling;
- convert numeric macros to typed enums/constants;
- validate ranges with `static_assert`;
- validate dependencies with `static_assert`;
- provide one `constexpr` resolved configuration object;
- expose readable names for startup reporting;
- contain no runtime mutation.

Use `#if ETRIKE_RT_ENCODERS != 0`, not `#ifdef`, because a macro defined as `0`
is still considered defined.

Illustrative structure:

```cpp
#pragma once

#ifndef ETRIKE_RT_ENCODERS
#define ETRIKE_RT_ENCODERS 0
#endif

#ifndef ETRIKE_RT_SPEED_FEEDBACK_SOURCE
#define ETRIKE_RT_SPEED_FEEDBACK_SOURCE 0
#endif

#ifndef ETRIKE_RT_PID_MODE
#define ETRIKE_RT_PID_MODE 0
#endif

namespace rt::build {

enum class UnitPolicy {
    Disabled = 0,
    RequiredPhysical = 1,
    Simulated = 2,
};

enum class SpeedFeedbackSource {
    None = 0,
    MtrReport = 1,
    RearMotorEncoder = 2,
};

enum class PidMode {
    Disabled = 0,
    Shadow = 1,
    Active = 2,
};

constexpr bool kEncodersEnabled = ETRIKE_RT_ENCODERS != 0;

constexpr auto kSpeedFeedbackSource =
    static_cast<SpeedFeedbackSource>(ETRIKE_RT_SPEED_FEEDBACK_SOURCE);
constexpr auto kPidMode = static_cast<PidMode>(ETRIKE_RT_PID_MODE);

static_assert(ETRIKE_RT_ENCODERS == 0 || ETRIKE_RT_ENCODERS == 1);
static_assert(ETRIKE_RT_SPEED_FEEDBACK_SOURCE >= 0 &&
              ETRIKE_RT_SPEED_FEEDBACK_SOURCE <= 3);
static_assert(ETRIKE_RT_PID_MODE >= 0 && ETRIKE_RT_PID_MODE <= 2);
static_assert(ETRIKE_RT_SPEED_FEEDBACK_SOURCE != 2 ||
              ETRIKE_RT_ENCODERS == 1);
static_assert(ETRIKE_RT_PID_MODE == 0 ||
              ETRIKE_RT_SPEED_FEEDBACK_SOURCE != 0);
static_assert(ETRIKE_RT_PID_MODE != 2 ||
              ETRIKE_RT_SPEED_FEEDBACK_SOURCE >= 2);

}  // namespace rt::build
```

Error messages should be added to production assertions.

### PlatformIO configuration

PlatformIO must consume values generated from the canonical system
configuration rather than becoming another editable source of truth. A resolved
generated RT feature section may look like:

```ini
[rt_features]
build_flags =
    -D ETRIKE_RT_ENCODERS=0
    -D ETRIKE_RT_SPEED_FEEDBACK_SOURCE=0
    -D ETRIKE_RT_PID_MODE=0
    -D ETRIKE_RT_KINEMATICS_RESOLVER=0
```

Unit policies and output permissions come from the same canonical
configuration. RT and SYS receive generated numeric values, and CI compares the
resolved RT and SYS values and fails on disagreement.

Both `vehicle` and `native` must include `${rt_features.build_flags}`. Any HIL
or controller-test environment that claims the same configuration must include
the same settings.

Generate shared unit policies into one imported PlatformIO configuration rather
than copying them independently into RT and SYS. A generated file such as
`build/generated/system_units.ini` can contain:

```ini
[system_units]
build_flags =
    -D ETRIKE_UNIT_HOST=2
    -D ETRIKE_UNIT_SYS=2
    -D ETRIKE_UNIT_HMI=0
    -D ETRIKE_UNIT_MTR=2
    -D ETRIKE_UNIT_EPS_C=2
    -D ETRIKE_UNIT_SEB=2
    -D ETRIKE_UNIT_PWT=0
    -D ETRIKE_UNIT_DCDC=0
    -D ETRIKE_UNIT_MOTOR_CONTROLLER=0
```

This resolved example is controller/HIL-only because it contains simulated
units. RT and SYS import the same generated section and add their generated
output permissions. A vehicle artifact must use physical or disabled values
only.

Example RT controller-only output settings:

```ini
[rt_outputs_controller_only]
build_flags =
    -D ETRIKE_OUTPUT_RT_STEERING=0
    -D ETRIKE_OUTPUT_RT_DRIVE=0
    -D ETRIKE_OUTPUT_RT_BRAKE_TO_SYS=0
    -D ETRIKE_OUTPUT_RT_BRAKE_DIRECT=0
    -D ETRIKE_OUTPUT_RT_GATEWAY=0
```

Example isolated RT plus EPS-C settings allow only
`ETRIKE_OUTPUT_RT_STEERING=1`; MTR, SEB, PWT, DC-DC, and motor-controller
policies remain disabled and their outputs remain `0`.

Required setup examples:

| Setup | Required/simulated units | Disabled units | Allowed output |
|---|---|---|---|
| RT controller only | Host/SYS/MTR/EPS-C/SEB simulated as needed | HMI/PWT/DC-DC/motor controller unless under test | None |
| SYS controller only | RT/MTR/SEB simulated as needed | Host/EPS-C/PWT/DC-DC/motor controller | None or protected SYS body dummy loads |
| RT plus EPS-C | EPS-C physical; Host/SYS simulated as needed | MTR/SEB/PWT/DC-DC/motor controller | RT steering only |
| SYS plus SEB | SEB physical; RT simulated as needed | MTR/EPS-C/PWT/DC-DC/motor controller | SYS brake only |
| RT plus SEB | SEB physical; SYS simulated as needed | MTR/EPS-C/PWT/DC-DC/motor controller | RT direct brake only |
| RT/SYS plus MTR | MTR physical; RT/SYS physical | EPS-C/SEB/PWT/DC-DC unless separately accepted | RT drive to MTR; MTR motor output initially inhibited |
| MTR plus motor controller | MTR and motor controller physical | EPS-C/SEB/PWT/DC-DC | MTR motor only |
| PWT plus DC-DC | PWT and DC-DC physical on powertrain bus | RT/SYS/MTR/EPS-C/SEB not connected to that bus | PWT DC-DC only |

These rows are setup presets, not runtime vehicle modes. Every preset resolves
to the same unit-policy and output-permission primitives.

The values may be edited deliberately, tested, committed, built, and flashed.
For common approved combinations, named PlatformIO presets may inherit the same
feature section. Those presets are build conveniences, not runtime modes.

Do not leave vehicle selection dependent on missing macros. The vehicle
environment must set all safety-relevant configuration values explicitly.

### Canonical configuration file

The first implementation uses a version-controlled canonical configuration file
and deterministically generates the PlatformIO values, typed header, test
parameters, and manifest. Hand-editing generated PlatformIO flags is not an
approved configuration workflow.

Initial locations may be:

```text
config/system/vehicle-open-loop.yaml
config/system/vehicle-encoder-telemetry.yaml
config/system/vehicle-pid-shadow.yaml
config/system/vehicle-pid-active.yaml
```

Do not build a second independent source of truth. The generated PlatformIO
values, header, manifest, native tests, simulation, and target firmware must all
consume the same resolved configuration.

## Required RT Code Changes

### 1. Encoder interface

Replace numeric/no-op stubs with explicit subsystem/channel state and samples.

Suggested types:

```cpp
enum class EncoderId : uint8_t {
    RearMotor,
    FrontWheel,
    RearLeft,
    RearRight,
};

enum class SensorState : uint8_t {
    Disabled,
    Initializing,
    Valid,
    Stale,
    Faulted,
};

struct EncoderSample {
    SensorState state;
    int32_t delta_pulses;
    float speed_mmps;
    uint32_t sample_time_ms;
};
```

Required behavior:

- when the subsystem is enabled, initialize only channels present in the
  reviewed hardware map;
- never configure floating GPIOs for disabled channels;
- report disabled explicitly;
- report driver errors explicitly;
- calculate an interval delta by reading and clearing the counter, or by storing
  and subtracting the previous count with wrap handling;
- use measured elapsed time, not an assumed constant;
- reconcile 2x versus 4x decoding before defining pulses per revolution;
- move installed-channel mask, PPR, direction, wheel circumference, and
  plausibility limits into a reviewed hardware-map/calibration definition;
- detect stale samples and implausible jumps;
- preserve a valid stationary sample as valid zero;
- keep channel health independent even though user-facing enable/disable applies
  to the encoder subsystem as a group.

### 2. Encoder initialization and sampling

In RT startup:

- log the resolved channel configuration;
- initialize enabled channels before dependent tasks start;
- fail or mark unavailable according to whether a configured channel is
  telemetry-only or required for control;
- never enter active PID when the selected source is unavailable.

In the 100 Hz control path:

- obtain one timestamped sample from the selected source;
- publish samples for telemetry independently of PID;
- update freshness and health;
- supply a typed feedback result to the speed controller.

### 3. Speed controller API

Replace `update_shadow_pid(desired, measured, dt, output)` with an API that
separates calculation from authority.

Illustrative result:

```cpp
struct SpeedControlResult {
    int32_t commanded_mmps;
    int16_t pid_correction_mmps;
    bool feedback_valid;
    bool correction_applied;
};
```

Required behavior:

| PID state | Required behavior |
|---|---|
| Disabled | Reset PID state; open-loop command unchanged; correction zero |
| Shadow | Require valid feedback; calculate correction; open-loop command unchanged |
| Active | Require valid feedback; calculate and apply bounded correction before publishing the setpoint |

PID must not infer feedback validity from `measured_mmps == 0`.

### 4. Correct control-loop ordering

Current ordering writes `g_setpoint_q` before active PID correction. Change the
flow to:

```text
resolve command and safety
        -> select feedback
        -> run configured speed-control strategy
        -> enforce final speed/gear bounds
        -> publish telemetry
        -> write final setpoint to g_setpoint_q
        -> CAN TX encodes that final setpoint
```

Tests must prove the exact value encoded into `RT_DRIVE_CMD` matches the final
authorized output.

### 5. Feedback fault behavior

For shadow PID, feedback failure disables/reset shadow calculation and leaves
the validated open-loop command unchanged while reporting the fault.

For active PID, feedback failure must immediately remove PID authority and
execute an approved safe response. Automatic powered open-loop fallback is not
allowed until separately reviewed and tested.

### 6. Output policy

Feature configuration does not replace output isolation. All actuator CAN sends
must eventually pass through a common output policy so controller-only/HIL
tests can prove that physical actuation is inhibited.

RT currently sends through several paths, including direct driver calls. A
wrapper around only `send_can_low()` is insufficient. Refactor all of these:

- `RT_DRIVE_CMD`;
- `RT_BRAKE_CMD`;
- steering request;
- direct SEB request;
- gateway transmission;
- periodic reports and heartbeats.

Classify messages by output purpose and enforce policy at the final CAN-driver
boundary.

## Required SYS Changes

SYS does not need encoder enable or PID enable settings.

SYS changes are limited to truthful capability consumption and test safety:

- do not interpret missing optional RT encoder capability as a sensor fault;
- do not claim encoder-based supervision when unavailable;
- keep MTR supervision separate from RT local encoder capability;
- never disable ESTOP or command freshness based on RT feature configuration;
- report configuration mismatch when RT/SYS expect incompatible system
  behavior;
- eventually consume a configuration/capability hash for diagnostics;
- provide a real output inhibit for controller-only/HIL builds, including SEB
  CAN output;

The existing SEB owner arbitration between RT and SYS requires a separate
design and test effort. Encoder/PID feature flags must not silently modify SEB
ownership.

## Required Unit-Policy Behavior

Every unit policy must control initialization, accepted input, freshness,
dependent behavior, output permission prerequisites, diagnostics, and tests.
It must not be implemented as a single timeout bypass.

### Host / Jetson

| Policy | Required behavior |
|---|---|
| Disabled | RT does not require Host heartbeat, rejects Host drive/brake authority, and cannot enter Host-controlled Auto output |
| Required physical | RT requires valid Host heartbeat and fresh commands according to the production contract |
| Simulated | Same protocol validation/freshness as physical, but artifact is test-only and receives from a deterministic test station |

### SYS

| Policy | Required RT behavior |
|---|---|
| Disabled | No SYS heartbeat dependency; SYS-dependent normal vehicle authority unavailable; direct brake test behavior only when separately authorized |
| Required physical | Normal mode, heartbeat, status, and brake-authority contract enforced |
| Simulated | Test station supplies SYS mode/heartbeat/status with production codec and timing rules |

### HMI

| Policy | Required behavior |
|---|---|
| Disabled | HMI frames do not grant authority and no HMI freshness claim is made |
| Required physical | Counter, freshness, enum, source bus, and forwarding behavior validated |
| Simulated | Test station generates the same contract; test-only artifact |

### MTR

| Policy | Required behavior |
|---|---|
| Disabled | RT suppresses drive output, SYS does not run MTR freshness/EGAS/ACK checks, and motor capability is unavailable |
| Required physical | RT/SYS require valid MTR readiness, feedback, command timeout, fault, and ESTOP behavior |
| Simulated | A behavioral MTR model receives drive commands and emits valid/faulted feedback; no physical motor output |

MTR is currently hardware-incomplete. Vehicle artifacts must not advertise
physical MTR capability until its HAL, CAN, DAC, gear, and direct ESTOP gates
pass.

### EPS-C / SES

| Policy | Required behavior |
|---|---|
| Disabled | RT suppresses steering requests and reports autonomous steering unavailable; disabling EPS-C must not implicitly unlock drive |
| Required physical | RT requires boot synchronization, fresh valid status, limits, following-error supervision, and fault handling |
| Simulated | EPS-C model implements status, checksum/counter, alignment, command following, faults, and command loss |

### SEB

| Policy | Required behavior |
|---|---|
| Disabled | RT and SYS suppress all SEB requests and report braking unavailable; no brake-ready claim |
| Required physical | Boot synchronization, status freshness, command authority, following error, fault, and command-loss behavior enforced |
| Simulated | SEB model implements stroke/pressure behavior, status, counters/checksum, faults, and communication loss |

SEB policy does not decide whether RT or SYS owns the command. The separate
brake-authority state must guarantee one sender at a time.

### PWT and DC-DC

PWT is a standalone powertrain node, not an RT/SYS peer in the current topology.

| Unit/policy | Required behavior |
|---|---|
| PWT disabled | No PWT capability or heartbeat claim |
| PWT required physical | PWT firmware and 250 kbit/s bus tested independently |
| PWT simulated | Powertrain model used only by test tooling |
| DC-DC disabled | PWT suppresses manufacturer command; external bench supply policy documented |
| DC-DC required physical | PWT transmits the generated manufacturer contract and verifies bus/TX behavior |
| DC-DC simulated | Test model accepts commands and reports expected power behavior without physical power switching |

### Traction motor controller

The traction motor controller is physical plant behind MTR. Its policy belongs
to the MTR/powertrain test manifest rather than RT/SYS CAN receive logic.

| Policy | Required behavior |
|---|---|
| Disabled | MTR physical DAC/gear output inhibited |
| Required physical | Current-limited constrained rig, measured DAC/gear behavior, zero-output and ESTOP verified |
| Simulated | MTR output is captured by a plant model; no physical output |

### Output-policy rule

For all units:

```text
unit enabled or simulated
    does not imply
actuator output authorized
```

Output authorization requires both a compatible unit policy and an explicit
output allowlist. Any conflict resolves to output inhibited.

## Manifest and Artifact Identity

### Build manifest

Every target build must produce a machine-readable manifest containing at
least:

```json
{
  "ecu": "RT",
  "firmware_version": "...",
  "git_commit": "...",
  "git_dirty": false,
  "platformio_environment": "vehicle",
  "units": {
    "host": "required_physical",
    "sys": "required_physical",
    "hmi": "disabled",
    "mtr": "disabled",
    "eps_c": "required_physical",
    "seb": "required_physical",
    "pwt": "disabled",
    "dcdc": "disabled",
    "motor_controller": "disabled"
  },
  "output_allowlist": ["rt_steering", "rt_brake_to_sys"],
  "encoder_subsystem": false,
  "installed_encoder_mask": "0x01",
  "speed_feedback_source": "none",
  "pid_mode": "disabled",
  "protocol_hash": "...",
  "network_contract_hash": "...",
  "configuration_hash": "...",
  "firmware_sha256": "..."
}
```

The configuration hash covers resolved feature values and relevant resolved
ESP-IDF settings. It is distinct from the CAN protocol hash.

### Startup reporting

RT must print an unambiguous startup block:

```text
RT configuration:
  Host: required physical
  SYS: required physical
  HMI: disabled
  MTR: disabled
  EPS-C: required physical
  SEB: required physical
  PWT/DC-DC: disabled (separate subsystem)
  output allowlist: steering, brake-to-SYS
  encoder subsystem: disabled
  installed encoder mask: rear motor
  speed feedback: none
  PID: disabled
  configuration hash: ...
  protocol hash: ...
```

SYS must report its own build configuration and the configuration identity it
expects from RT where applicable.

### CAN reporting

The initial implementation uses serial/startup output and the artifact manifest.
Do not overload an existing safety field with configuration bits.

If CAN reporting is required:

1. Define a diagnostic capability message in canonical CAN YAML.
2. Generate typed codecs for it.
3. Report effective compile-time state and runtime sensor health separately.
4. Include protocol/configuration identity or a defined truncated identifier.
5. Add golden vectors and cross-language tests.
6. Keep the message read-only; it does not command feature transitions.

## Software Test Plan

### Configuration validation tests

Add compile-success tests for every supported combination and compile-failure
tests for every forbidden combination.

Minimum feature matrix:

| Encoder subsystem | Feedback | PID | Expected build |
|---:|---:|---:|---|
| 0 | None | Disabled | Pass |
| 0 | MTR | Disabled | Pass |
| 1 | None | Disabled | Pass |
| 1 | RT rear encoder | Disabled | Pass |
| 1 | RT rear encoder | Shadow | Pass |
| 1 | RT rear encoder | Active | Pass only after active feature is accepted |
| 0 | RT rear encoder | Any | Fail |
| Any | None | Shadow | Fail |
| Any | None | Active | Fail |
| Any | MTR | Active | Fail until MTR physical feedback is accepted |
| Any | Calculated | Active | Pass for SIL/testing only |
| Any invalid value | Any | Any | Fail |

Minimum kinematics resolver matrix:

| Kinematics resolver (`ETRIKE_RT_KINEMATICS_RESOLVER`) | Expected build | Expected resolved type (`rt::ActiveResolver`) |
|---:|---|---|
| `0` (Bicycle) | Pass | `rt::PhysicsModel` |
| `1` (Direct) | Pass | `rt::DirectResolver` |
| Any invalid value | Fail compilation | N/A |

Minimum unit-policy matrix for each external unit:

| Unit policy | Output permission | Expected result |
|---|---|---|
| Disabled | Inhibited | Pass; no dependency, no command, capability unavailable |
| Disabled | Allowed | Fail configuration validation |
| Required physical | Inhibited | Pass for passive/controller tests |
| Required physical | Allowed | Pass only for compatible release or isolated physical procedure |
| Simulated | Inhibited | Pass for receive/passive simulation |
| Simulated | Allowed to test bus | Pass only in test artifact on isolated bus |
| Simulated | Any vehicle release | Fail configuration validation |
| Invalid value | Any | Fail configuration validation |

Cross-unit validation must reject current-topology conflicts, including PWT as
an RT/SYS low-bus gateway, SYS motor ownership, SEB output with SEB disabled,
steering output with EPS-C disabled, and drive output with MTR disabled.

### Build-configuration unit tests

Tests must verify:

- each numeric value resolves to the correct typed state;
- readable state names are correct;
- all disabled defaults are safe;
- unknown values fail compilation;
- active PID dependency checks cannot be bypassed by macro ordering;
- `0` values are treated as disabled when macros are defined;
- every unit policy resolves to disabled, required physical, or simulated;
- each output allowlist entry has a compatible unit policy;
- RT and SYS resolve the same canonical unit policies and configuration hash;
- native and target manifests resolve to the same feature values.

### Unit-policy behavior tests

Run the following contract for Host, HMI, MTR, EPS-C, SEB, PWT, DC-DC, and the
traction motor controller wherever applicable:

- disabled at boot creates no missing-unit fault;
- disabled unit cannot provide mode, command, readiness, or actuator authority;
- disabled unit receives no command output;
- required physical unit must become ready within its declared startup window;
- required physical unit that is never seen, becomes stale, freezes, faults, or
  disconnects produces its defined response;
- required physical reconnect does not restore stale authority;
- simulated unit uses production codec, timing, counter, freshness, and fault
  rules;
- simulated policy is rejected from vehicle release;
- wrong-bus and unexpected-unit traffic cannot grant authority;
- output remains inhibited unless the explicit output allowlist permits it;
- RT/SYS disagreeing unit policies produce a deterministic configuration error;
- diagnostics report configured policy separately from detected runtime state.

### Kinematics resolver unit tests

Tests must verify that the Compile-Time Type Aliasing is fully deterministic and mathematical calculations are robust under all situations:

- **Type Enforcement**: Both `rt::PhysicsModel` and `rt::DirectResolver` conform to the exact same implicit signature (e.g., `resolve(const DriveCmd&, ResolvedSetpoint&)`) without runtime inheritance (vtables).
- **Zero-Command Clamping**: Pure zero input (`speed_mmps = 0`, `yaw_rate_mrad_s = 0`) must deterministically produce pure zero actuator outputs for both traction and steering.
- **Saturation and Bounds**: Mathematical outputs exceeding actuator mechanical limits (e.g., `±45°` steering, `V_max` motor speed) are safely clamped without wrapping, overflowing, or panicking.
- **Singularity Handling**: The Bicycle kinematics resolver must safely handle `yaw_rate_mrad_s = 0` (infinite turning radius straight-line travel) without division-by-zero exceptions.
- **Direction Reversal**: Reverse speed requests with positive/negative yaw correctly resolve the left/right motor speed differentials and wheel-slip constraints.
- **Compile-time Isolation**: Native tests compiled with `ETRIKE_RT_KINEMATICS_RESOLVER=1` must cleanly fail if they attempt to access stateful decay properties of `rt::PhysicsModel` that do not exist in `rt::DirectResolver`.
- **Latency Guarantee**: Execution time of the resolver remains strictly bounded and deterministic within the 10ms (100Hz) control loop budget.

### Encoder unit tests

Use a host-testable encoder core separated from ESP-IDF PCNT calls.

Required cases:

- disabled channel reports `Disabled`, not valid zero;
- enabled channel initializes once;
- disabled channel never initializes its GPIO/PCNT unit;
- subsystem-disabled configuration initializes no channels;
- subsystem-enabled configuration initializes exactly the installed hardware-map
  channels;
- positive and negative pulse deltas preserve direction;
- zero pulse delta is valid stationary data;
- read error reports `Faulted`;
- no sample within freshness limit reports `Stale`;
- counter wrap is handled;
- elapsed time zero/negative is rejected;
- PPR and decode multiplier produce expected speed;
- reversed direction calibration changes sign once;
- implausible jump is rejected;
- reset clears previous-count and timestamp state;
- reinitialization cannot produce a large synthetic delta.

### PCNT target tests

On an RT board with no actuator connected:

- disabled configuration leaves all encoder GPIOs/PCNT units unconfigured;
- subsystem-disabled configuration initializes no channel;
- subsystem-enabled configuration initializes every installed hardware-map
  channel and no TBD/uninstalled channel;
- known signal-generator pulses produce expected counts and direction;
- maximum expected input rate does not overflow between samples;
- glitch filter rejects pulses below the approved width;
- PCNT read failures become visible faults;
- reset and power cycle return to a known state.

### PID unit tests

Extend [`rt-esp32/test/test_pid/test_pid.cpp`](../rt-esp32/test/test_pid/test_pid.cpp)
to cover:

- disabled PID always returns zero correction;
- disabled PID resets integral, derivative, previous setpoint, and first-call
  state;
- shadow PID calculates correction but cannot change the commanded setpoint;
- active PID applies correction before final bounds;
- valid zero-speed feedback remains valid;
- unavailable, stale, and faulted feedback are handled distinctly;
- `dt <= 0` is rejected safely;
- output saturation and anti-windup;
- setpoint changes reset the intended state;
- derivative filtering;
- direction mismatch;
- feedback loss while shadow;
- feedback loss while active;
- mode transition reset and bumpless first active output;
- gain/configuration changes cannot occur while active;
- placeholder gains are not accepted as production gains.

### RT control integration tests

Refactor the control calculation into host-testable production code rather than
copying `main.cpp` logic into tests.

Required cases:

- open-loop/no-encoder complete command path;
- encoder enabled but unused does not alter the command;
- encoder telemetry does not alter the command;
- shadow PID does not alter `g_setpoint_q` or encoded `RT_DRIVE_CMD`;
- active PID alters the final queue value and encoded frame exactly once;
- final clamp applies after correction;
- Manual suppresses actuator commands according to current policy;
- ESTOP and safety zeroing override PID;
- stale Host command overrides PID;
- steering readiness lockout still produces zero/N drive output;
- PID cannot restore a command cleared by safety;
- feedback failure during active PID produces the approved safe response;
- reset does not restore previous PID or encoder state.

### RT/SYS integration tests

Required cases:

- SYS accepts normal RT open-loop operation with encoder capability absent;
- SYS does not report encoder supervision as healthy when unavailable;
- RT and SYS configuration identity mismatch is diagnostic and blocks only the
  behavior explicitly defined by policy;
- MTR supervision remains independent from RT PCNT configuration;
- RT open-loop/no-encoder mode preserves ESTOP, heartbeat, mode, brake, and
  steering behavior;
- RT PID state does not alter SEB ownership or SYS mode authority;
- RT reset and reconnect cannot restore stale drive authority;
- no actuator CAN output occurs in controller-only output-inhibited builds.

### Simulation tests

Simulation configuration must add:

- all unit policies: Host, SYS, HMI, MTR, EPS-C, SEB, PWT, DC-DC, and motor
  controller;
- explicit physical/simulated/disabled state for each unit;
- RT encoder-subsystem state and installed channel map;
- selected speed-feedback source;
- PID disabled/shadow/active state;
- valid, stale, frozen, faulted, and noisy feedback;
- ECU presence and disconnect/reconnect;
- output-inhibit policy.

Minimum scenarios:

- complete system, RT encoder subsystem disabled, PID disabled;
- each unit disabled while all unrelated units remain healthy;
- each unit simulated with production codec/timing validation;
- forbidden unit/output combinations rejected;
- EPS-C-only, SEB-only, MTR-only, HMI-only, and PWT/DC-DC isolated setups;
- rear encoder telemetry with open-loop control;
- shadow PID under nominal speed response;
- shadow PID with frozen/zero/noisy feedback;
- active PID request with invalid configuration rejected;
- active limited PID with feedback loss;
- RT/SYS/MTR disconnect and reconnect in each accepted configuration;
- ESTOP and stale command during every PID state;
- reset during nonzero PID state;
- configuration mismatch between RT model, SYS model, and test catalog.

### Property and invariant tests

For generated event sequences, assert:

- PID never has output authority when configured disabled;
- shadow PID never changes physical or CAN actuator output;
- active PID never operates without fresh valid approved feedback;
- disabled encoders never become runtime dependencies;
- enabled failed encoders never become valid zero;
- safety zeroing cannot be reversed by PID in the same or later control cycle;
- output bounds hold after all corrections;
- reset/reconnect does not restore prior PID authority or command;
- ordinary CAN frames cannot change build configuration;
- disabled units never become readiness dependencies or output destinations;
- simulated units are impossible in a vehicle release;
- no non-allowlisted unit receives an actuator command;
- configuration and firmware hashes remain stable for the tested artifact.

## CI Plan

### Pull-request gate

Every relevant change must run:

- configuration schema/static-assert tests;
- RT native tests;
- SYS native tests;
- generated CAN verification;
- open-loop/no-encoder target builds;
- affected encoder/PID unit and integration tests;
- static analysis and formatting checks.

### Configuration matrix gate

CI must build every supported committed configuration and verify forbidden
configurations fail.

Initial supported matrix:

| Configuration | Unit setup | Encoder subsystem | Feedback | PID |
|---|---|---|---|---|
| Controller-only | All external units simulated/disabled; outputs inhibited | Disabled | None | Disabled |
| Open-loop baseline | Approved physical unit set | Disabled | None | Disabled |
| MTR telemetry baseline | MTR physical/simulated | Disabled | MTR | Disabled |
| Encoder checkout | Actuators disabled | Enabled | None | Disabled |
| Encoder supervision | Motor rig only | Enabled | RT rear | Disabled |
| PID shadow | Motor rig only | Enabled | RT rear | Shadow |

Active PID remains excluded from approved vehicle builds until its complete
software, HIL, and hardware acceptance criteria pass.

### Nightly gate

Run:

- all supported build combinations;
- sanitizer builds for host-testable code;
- generated state-machine/property sequences;
- encoder/PID fault injection;
- deterministic replay corpus;
- long-duration simulated open-loop and shadow-PID soak;
- manifest determinism and artifact-hash checks.

### Release-candidate gate

A release candidate requires:

- clean generated artifacts;
- exact git identity and dirty-state record;
- resolved PlatformIO and ESP-IDF configuration;
- feature manifest and configuration hash;
- protocol and network-contract hashes;
- complete test report for the selected configuration;
- exact target firmware SHA-256;
- controller/HIL evidence against that firmware;
- approval to connect the next physical unit.

## Build, Test, and Flash Workflow

### 1. Select configuration

Change only the controlled PlatformIO feature values or select an approved
preset. Do not edit control code to enable a test.

### 2. Validate and test

Run the native, simulation, target-build, and applicable controller/HIL gates
using the same resolved configuration.

Native tests cannot execute the ESP32 binary directly. The evidence chain is:

```text
same git commit
  + same canonical feature configuration
  + same generated CAN contract
  + host/native/SIL tests
  + target build
  + exact target binary hash
```

### 3. Freeze artifacts

Store:

```text
firmware.bin
firmware.elf
build-manifest.json
software-test-report.json
controller-hil-report.json
```

### 4. Flash without rebuilding

The approved firmware binary is flashed directly. The flashing tool must:

1. Read the approved manifest.
2. Calculate the selected binary SHA-256.
3. Reject a mismatch.
4. Record board identity where available.
5. Flash the existing binary without rebuilding source.
6. Record flash time, operator, binary hash, and configuration hash.

`pio run -t upload` may trigger a rebuild and therefore is not sufficient as the
final evidence mechanism unless the resulting hash is revalidated. Add a
project flashing script that flashes an already approved artifact.

### 5. Verify startup identity

Before connecting actuator power, capture startup output and verify:

- ECU identity;
- firmware version;
- expected feature values;
- configuration hash;
- protocol hash;
- no unexpected bypass;
- encoder subsystem and installed-channel map match the approved configuration;
- expected feedback source and PID state are correct.

### 6. Execute physical procedure

Only the unit listed by the approved procedure is powered. Unrelated actuator
power and command wiring remain physically disconnected.

### 7. Preserve evidence

Record raw CAN, serial logs, measurements, configuration, firmware hash,
procedure revision, operator, result, and anomalies.

## Hardware Test Plan

### Gate 1: RT and SYS controller-only tests

Connected:

- RT board and then SYS board, first separately and then together;
- CAN analyzer/test station;
- no EPS-C, SEB, MTR, PWT, DC-DC, motor controller, or powered encoder.

Verify:

- open-loop/no-encoder firmware boots without encoder initialization;
- manifest and startup values match;
- no floating encoder counts are reported;
- expected CAN traffic and rates;
- invalid CAN does not change configuration;
- reset, watchdog, command timeout, and bus-off behavior;
- output-inhibited controller test prevents all actuator commands.

Repeat unit-policy tests with each applicable peer disabled and simulated. Prove
that disabled peers create no false readiness, timeout, or output, and that
simulated peers cannot appear in a vehicle-release manifest.

### Gate 2: Host and HMI interface tests

Connected one at a time:

- RT plus physical Host/Jetson, all actuator outputs inhibited;
- RT/SYS plus physical HMI, all actuator outputs inhibited;
- CAN analyzer on each active bus.

Verify:

- correct bus, IDs, DLCs, counters, rates, and freshness;
- malformed, stale, duplicate, reordered, and replayed commands are rejected;
- Host/HMI disabled policy grants no authority;
- Host/HMI simulated policy behaves identically at the protocol boundary;
- disconnect/reconnect does not restore stale Auto authority;
- no actuator command escapes the output inhibit.

### Gate 3: EPS-C steering unit

Connected:

- RT;
- EPS-C and mechanically constrained steering rack;
- CAN analyzer/test station;
- no SEB, MTR, motor controller, PWT, or DC-DC actuator path.

Configuration:

- EPS-C required physical;
- steering output allowed;
- all unrelated actuator outputs inhibited;
- Host/SYS simulated only as required by the procedure.

Verify boot synchronization, center, direction, angle scaling, bounds, slew,
following error, status freshness, checksum/counter behavior, ESTOP, command
loss, feedback loss, disconnect, reconnect, reset, and power cycle.

### Gate 4: SEB brake unit

Execute separate physical tests for:

- SEB with the test station directly;
- SYS plus SEB as normal/manual/ESTOP authority;
- RT plus SEB as Auto/takeover authority;
- RT plus SYS plus SEB for owner handoff only after the separate paths pass.

Configuration permits only SEB/brake output. Verify alignment, stroke, pressure,
release, hold, following error, status freshness, checksum/counter, command loss,
CAN loss, ESTOP, reset, reconnect, and no dual sender.

### Gate 5: MTR and traction motor controller

Execute progressively:

1. MTR controller only with DAC/gear outputs disconnected.
2. MTR with dummy loads and measured DAC/gear signals.
3. MTR with the physical motor controller on a current-limited unloaded or
   dynamometer rig.
4. RT/SYS with MTR, all other actuator units disconnected.

Verify MTR HAL initialization, command timeout, mode, DAC bounds, gear
interlocks, feedback truthfulness, direct ESTOP, CAN loss, bus-off, reset, and
SYS supervision. This gate remains blocked while MTR hardware initialization
and direct ESTOP are incomplete.

### Gate 6: PWT and DC-DC

Connected:

- PWT on the isolated 250 kbit/s powertrain bus;
- DC-DC or a non-power-switching protocol simulator;
- no physical connection between 250 kbit/s powertrain CAN and 500 kbit/s low
  CAN.

Verify the extended manufacturer command, DLC, reserved constants, 100 ms rate,
enabled/disabled policy, TX failure, bus-off, watchdog, reset, and power-cycle
behavior. Do not test or claim low-to-powertrain forwarding; it is not
implemented by the current PWT hardware.

### Gate 7: Encoder electrical checkout

Connected:

- RT board;
- the installed encoder subsystem, initially one physical channel at a time;
- signal generator or manually rotated encoder;
- no physical actuator.

For each channel listed in the approved hardware map:

1. Begin with the encoder subsystem disabled and prove no PCNT/GPIO is
   initialized by the encoder code.
2. Flash the tested subsystem-enabled, PID-disabled artifact whose hardware map
   contains the channel under test.
3. Verify startup identity.
4. Apply known pulse counts and frequencies.
5. Verify count, direction, scale, zero speed, and timestamps.
6. Disconnect A, disconnect B, short as permitted by the protected rig, and
   inject noise.
7. Verify stale/faulted reporting.
8. Reset and power cycle.
9. Save evidence before moving to the next channel.

### Gate 8: Complete open-loop system without encoders

Connected progressively:

- RT and SYS controllers first, no actuators;
- MTR/motor subsystem on a constrained unloaded rig;
- EPS-C separately;
- SEB separately;
- Host and HMI separately;
- PWT/DC-DC as a separate powertrain subsystem;
- never all unaccepted units at once.

Verify that all required functions work with the RT encoder subsystem disabled
and PID disabled:

- Manual, Auto, and Estop transitions;
- open-loop motor command mapping;
- speed and gear bounds;
- stale Host command response;
- RT/SYS heartbeat loss;
- MTR timeout and safe output;
- steering limits and ESTOP response;
- brake authority and takeover;
- reset, disconnect, reconnect, and power sequencing;
- diagnostics truthfully report no encoder/PID capability.

This gate establishes the supported no-encoder baseline.

### Gate 9: Encoder telemetry during open-loop operation

Connected:

- RT;
- rear encoder;
- constrained motor rig;
- required controller/test station only.

Configuration:

- encoder subsystem enabled with rear encoder in the installed hardware map;
- RT rear encoder selected for telemetry/supervision;
- PID disabled.

Verify:

- enabling the encoder does not change the open-loop command;
- direction matches commanded direction;
- speed scaling across the approved range;
- zero-speed validity;
- noise and dropouts;
- feedback loss behavior;
- ESTOP and command timeout remain independent;
- telemetry rate and freshness;
- temperature and long-duration count stability.

### Gate 10: Shadow PID

Configuration:

- encoder subsystem enabled with rear encoder validated;
- RT rear encoder selected;
- PID shadow.

Verify:

- open-loop command is byte-for-byte identical to the PID-disabled baseline for
  the same input sequence;
- PID telemetry responds to setpoint and measured speed;
- PID saturation, anti-windup, and reset behavior;
- feedback loss resets/disables shadow calculation;
- gains cannot alter actuation;
- reset and reconnect do not retain controller state;
- long-duration telemetry remains stable.

### Gate 11: Active PID on constrained rig

This gate is blocked until Gates 1-10 pass and gains/limits are reviewed.

Required protections:

- independent emergency power disconnect;
- current-limited supply;
- unloaded motor or controlled dynamometer;
- physical deadman;
- reduced command and PID correction limits;
- feedback freshness and plausibility;
- direction verification;
- immediate safe response on feedback loss;
- no automatic powered open-loop fallback;
- bounded integral and bumpless activation.

Verify incrementally:

- zero command;
- small positive command;
- command removal;
- small negative/reverse command where mechanically permitted;
- step and ramp response;
- disturbance response;
- saturation and anti-windup;
- sensor dropout, frozen value, reversed sign, and implausible jump;
- ESTOP, deadman release, command timeout, RT reset, MTR reset, and CAN loss;
- thermal and soak behavior.

Active PID is not approved for vehicle movement by passing a bench rig alone.

## Stop Conditions

Stop testing and remove power if any of the following occurs:

- unexpected actuator motion;
- wrong encoder direction;
- disabled encoder subsystem initializes PCNT/GPIO or reports valid data;
- an uninstalled hardware-map channel initializes or reports valid data;
- enabled failed channel reports valid zero;
- PID-disabled or shadow PID changes actuator output;
- PID runs without its approved feedback source;
- output exceeds value, slew, or duration bounds;
- ESTOP, timeout, watchdog, or reset fails to produce the defined safe behavior;
- configuration or firmware hash differs from the approved report;
- an ordinary CAN frame changes feature state;
- a non-tested unit or output becomes active;
- evidence capture is unavailable;
- any test is failed, blocked, aborted, or incomplete.

## Required Evidence per Configuration

Each approved configuration must retain:

- configuration source and resolved values;
- configuration hash;
- git commit and dirty state;
- PlatformIO environment;
- resolved ESP-IDF settings relevant to timing and safety;
- protocol and network-contract hashes;
- firmware ELF and binary;
- firmware SHA-256;
- native, simulation, target-build, and controller/HIL results;
- physical wiring and connected-unit manifest;
- raw CAN and serial captures;
- measured encoder counts, speed, timing, and PID values where applicable;
- pass/fail criteria and result;
- deviations, unavailable measurements, and known limitations.

## Implementation Work Packages

Work packages are dependency ordered. Complete each exit gate before starting
the next package.

### WP1: Build configuration foundation

- Add the canonical configuration schema and deterministic compiler.
- Generate `build_config.h`, PlatformIO values, test parameters, and the
  normalized manifest input.
- Add explicit unit policies, output permissions, encoder-subsystem state,
  feedback source, and PID state to target and native builds.
- Add compile-time validation.
- Print resolved configuration at boot.
- Add configuration unit tests.

Exit: target and native builds resolve identical unit, output, encoder, feedback,
and PID configuration; open-loop/no-encoder/PID-disabled behavior is explicit.

### WP2: Unit and output policy integration

- Replace broad bypass behavior with explicit per-unit policy.
- Enforce disabled, required-physical, and simulated semantics in production
  core logic.
- Add the central output-policy boundary for RT, SYS, MTR, and PWT output
  classes.
- Keep vehicle mode separate from build configuration.
- Prove disabled units are neither required nor commanded.
- Prove unit presence does not grant output without an allowlist.

Exit: every relevant RT/SYS path consumes typed configuration; all disabled
paths fail closed and all actuator output reaches one testable policy boundary.

### WP3: Encoder and speed-control implementation

- Add one encoder-subsystem feature switch and a reviewed installed-channel
  hardware map.
- Initialize all and only installed channels when the subsystem is enabled.
- Replace zero/no-op stubs with typed state.
- Correct delta, wrap, timing, direction, and decode multiplier.
- Add calibration and health.
- Add explicit PID disabled/shadow/active behavior.
- Add feedback source selection.
- Correct control-loop ordering.
- Add typed control result and feedback-fault response.

Exit: encoder-disabled/PID-disabled is a complete supported path; shadow PID
cannot affect output; active PID cannot build or run without approved feedback.

### WP4: Complete software-level verification

- Add configuration success/failure tests for all supported and forbidden
  combinations.
- Add unit-policy and output-allowlist tests for every unit.
- Add encoder-core and PID tests.
- Refactor RT/SYS calculations into host-testable production modules rather
  than copied test logic.
- Add complete RT control-output tests through encoded CAN frames.
- Add RT/SYS ownership, timeout, reset, reconnect, and safety-invariant tests.
- Replace placeholder bypass/reset/integration tests.
- Run static analysis, sanitizers, generated-codec tests, and deterministic
  fault/state sequences.

Exit: the complete host/native software matrix passes with zero placeholders and
proves the open-loop no-encoder baseline plus every declared configuration
invariant. No physical CAN adapter is required or permitted for this gate.

### WP5: Manifest, artifacts, and CI

- Generate build manifest and configuration hash.
- Add all supported configurations to CI.
- Add forbidden-configuration compile tests.
- Record exact target artifact hashes.
- Add approved-artifact flashing script.

Exit: a software-tested artifact can be identified and flashed without rebuild.

### WP6: Pure software/SIL integration

- Add every unit policy and output permission to software simulation.
- Add feedback fault models.
- Run unit-disabled, unit-simulated, isolated-unit, open-loop, and shadow
  matrices.
- Add configuration mismatch diagnostics.
- Use virtual buses only; no CANalyst-II physical TX.
- Cross-check simulation results against host-tested production invariants and
  generated CAN vectors.

Exit: all pure software and SIL scenarios pass without a physical adapter. The
simulation is not allowed to redefine firmware truth or hide missing production
coverage.

### WP7: Control UI controller tests and HIL

- Implement the future Control UI FastAPI backend foundation.
- Add generated Python codecs, virtual and CANalyst-II adapters, bounded routing,
  session state, Bench TX gate, leases, and source ownership.
- Add synthetic peers only after their production contracts have passed WP4/WP6.
- Add output-inhibited real RT/SYS controller tests.
- Add closed-loop behavioral plant models and fault scenarios.
- Record exact firmware/configuration hashes and complete evidence.

Exit: all software/controller gates pass before physical encoder or actuator
testing; HIL runs on an isolated CAN bench with physical actuators disconnected.

### WP8: Isolated physical hardware acceptance

- Execute Host and HMI interface tests.
- Execute EPS-C-only steering tests.
- Execute SEB-only brake tests for SYS and RT authority separately.
- Execute MTR and traction-motor-controller tests progressively.
- Execute PWT/DC-DC tests on the separate 250 kbit/s bus.
- Execute encoder electrical checkout.
- Prove complete no-encoder open-loop system.
- Prove encoder telemetry does not alter output.
- Prove shadow PID isolation.
- Execute active PID constrained-rig tests only after approval.

Exit: each configuration has its own reviewed evidence and explicit approval
scope.

## Completion Criteria

This work is complete when:

- Host, HMI, MTR, EPS-C, SEB, PWT, DC-DC, and motor-controller policies are
  explicit, validated, tested, and reported;
- disabled, required-physical, and simulated unit policies have distinct tested
  behavior;
- unit presence never grants output without a compatible output allowlist;
- the RT encoder subsystem can be enabled or disabled as one feature;
- the complete system passes with the RT encoder subsystem disabled and PID
  disabled;
- the installed encoder channel map is fixed by reviewed hardware configuration,
  not user-facing per-channel switches;
- disabled, valid-zero, stale, unavailable, and faulted are distinct;
- PID disabled, shadow, and active are explicit and tested;
- active PID cannot build or run without approved feedback;
- PID correction is applied before the final transmitted setpoint;
- SYS does not claim or own RT encoder/PID behavior;
- no ordinary CAN command can change build configuration;
- startup and diagnostics expose effective configuration;
- CI builds every supported configuration and rejects forbidden combinations;
- exact tested binaries are flashed without rebuild;
- hardware tests connect and accept one unit at a time;
- final vehicle testing uses the exact approved release artifact.

## Related Documents

- [`rt-sys-configuration-implementation-work-plan.md`](rt-sys-configuration-implementation-work-plan.md)
- [`commissioning-test-profiles.md`](commissioning-test-profiles.md)
- [`validation/rt-sys-pre-vehicle-validation.md`](validation/rt-sys-pre-vehicle-validation.md)
- [`hil-safety-test-plan.md`](hil-safety-test-plan.md)
- [`can-bench-test.md`](can-bench-test.md)
- [`integration-test-procedure.md`](integration-test-procedure.md)
- [`pid-speed-control.md`](pid-speed-control.md)
- [`../rt-esp32/platformio.ini`](../rt-esp32/platformio.ini)
- [`../sys-esp32/platformio.ini`](../sys-esp32/platformio.ini)
