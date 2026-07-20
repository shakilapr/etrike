# RT/SYS Configuration Implementation Work Plan

## Status

This is the dependency-ordered implementation plan for optional-unit policy,
output permissions, encoder/PID configuration, software verification, future
Control UI controller/HIL testing, and one-unit-at-a-time physical acceptance.

The design requirements are defined in
[`rt-sys-feature-configuration-and-test-plan.md`](rt-sys-feature-configuration-and-test-plan.md).
This document defines execution order and completion evidence.

No phase in this plan is complete merely because code compiles. A phase is
complete only when its implementation, tests, generated artifacts,
documentation, cleanup, and exit gate all pass.

## Non-Negotiable Phase Rule

Phases are sequential.

- Do not start Phase N+1 until Phase N passes its exit gate.
- If a later phase exposes an earlier defect, return to the owning phase, fix it,
  and rerun every dependent gate.
- Do not use Control UI synthetic peers, CANalyst-II physical transmission, HIL,
  or physical actuators to compensate for missing host/native tests.
- Do not connect a physical actuator before the applicable controller/HIL phase
  passes against the exact firmware artifact.
- Do not silently waive a failed, blocked, aborted, incomplete, or inconclusive
  result.
- Do not keep temporary bypasses after their replacement phase passes.
- Do not change a tested configuration or rebuild a tested binary before
  flashing without invalidating its evidence.

## Target End State

At completion:

- every firmware artifact has an explicit deployment profile;
- Host, HMI, SYS, MTR, EPS-C, SEB, PWT, DC-DC, and motor-controller policies are
  explicit where relevant;
- each external unit is `disabled`, `required physical`, or `simulated`;
- each actuator output class has an independent `0/1` permission;
- unit presence never authorizes output by itself;
- RT encoders are enabled or disabled as one subsystem;
- installed encoder channels come from a reviewed hardware map;
- speed feedback is explicitly `none`, `MTR report`, or `RT rear encoder`;
- PID is explicitly `disabled`, `shadow`, or `active`;
- the complete open-loop system works with encoders and PID disabled;
- broad run-mode bypasses no longer control unrelated safety behavior;
- native, SIL, controller, HIL, and physical evidence are traceable to exact
  configuration and firmware hashes;
- ordinary CAN traffic cannot change immutable build configuration;
- final release testing uses all required physical units and no synthetic safety
  feedback.

## System Inventory

| Unit/capability | Current role | Current readiness | Owning phases |
|---|---|---|---|
| Host / Jetson | High-CAN command source | Bridge exists; contract gaps remain | 1, 5, 13, 15-16, 19, 27 |
| RT | Kinematics, steering, drive/brake commands, gateway | Implemented with configuration/output gaps | 1-18, 24-27 |
| SYS | Mode, ESTOP, normal SEB authority, body I/O, MTR monitor | Implemented with configuration/output gaps | 1-18, 24-27 |
| HMI | Mode/power request source | Protocol exists; authority/freshness incomplete | 1, 6, 13, 15-16, 19, 27 |
| MTR | Motor DAC/gear actuator ECU | HAL and direct ESTOP incomplete | 1, 7, 13, 15-18, 22, 24-27 |
| EPS-C / SES | Steering actuator and feedback | Physical acceptance incomplete | 1, 5, 13, 15-18, 20, 24, 27 |
| SEB | Brake actuator and feedback | Physical loss/ownership acceptance incomplete | 1, 5-6, 13, 15-18, 21, 24, 27 |
| PWT | Standalone 250 kbit/s powertrain node | DC-DC command implemented; not a gateway | 1, 7, 13, 15-18, 23, 27 |
| DC-DC | Powertrain CAN actuator | Direct PWT contract; physical acceptance incomplete | 1, 7, 13, 15-18, 23, 27 |
| Traction motor controller | Analog/gear plant behind MTR | Physical characteristics/telemetry incomplete | 1, 7, 13, 15-18, 22, 24-27 |
| RT encoder subsystem | Optional local PCNT sensors | Driver disconnected and incorrect in places | 1-3, 11-15, 19, 25-27 |
| RT PID | Optional speed-control strategy | Shadow implicit; active output ordering broken | 1-3, 12-15, 25-27 |
| SYS body I/O | Inputs, lights, bulbs, relay | Implemented; bench policy incomplete | 1, 6, 8, 13, 17, 19, 27 |

## Permission-Controlled Output Classes

Every actuator or forwarding path must map to exactly one permission class:

| Output class | Producer | Destination |
|---|---|---|
| `RT_STEERING` | RT | EPS-C command |
| `RT_DRIVE` | RT | MTR drive command |
| `RT_BRAKE_TO_SYS` | RT | SYS brake request |
| `RT_BRAKE_DIRECT` | RT | SEB direct request |
| `RT_GATEWAY` | RT | High/low forwarding |
| `SYS_BRAKE` | SYS | SEB command |
| `SYS_BODY` | SYS | Lamps, indicators, mode bulbs, 12 V relay |
| `MTR_MOTOR` | MTR | DAC and gear outputs |
| `PWT_DCDC` | PWT | DC-DC manufacturer command |

## Phase Summary

| Phase | Name | Hardware allowed |
|---:|---|---|
| 0 | Baseline and decision freeze | None |
| 1 | Canonical configuration schema | None |
| 2 | Deterministic configuration compiler | None |
| 3 | PlatformIO/build integration | None |
| 4 | Manifest and startup identity | None |
| 5 | RT unit-policy integration | None |
| 6 | SYS unit-policy and brake-authority integration | None |
| 7 | MTR/PWT policy and topology integration | None |
| 8 | Central output-policy enforcement | None |
| 9 | Broad bypass retirement | None |
| 10 | Open-loop no-encoder baseline | None |
| 11 | Encoder subsystem software implementation | None |
| 12 | Speed feedback and PID strategy | None |
| 13 | Complete host/native verification | None |
| 14 | CI, artifact approval, and flash gate | None |
| 15 | Pure software/SIL integration | Virtual buses only |
| 16 | Control UI backend foundation | Virtual adapter only |
| 17 | Real-controller tests | RT/SYS boards; no actuators |
| 18 | Closed-loop HIL | RT/SYS boards; no actuators |
| 19 | Host, HMI, SYS body I/O, and encoder electrical tests | Low-energy interfaces only |
| 20 | EPS-C physical integration | EPS-C only |
| 21 | SEB physical integration | SEB only |
| 22 | MTR and traction motor integration | Constrained motor rig only |
| 23 | PWT and DC-DC integration | Isolated powertrain bus only |
| 24 | Complete open-loop subsystem integration | Accepted units progressively |
| 25 | Encoder telemetry and shadow PID | Constrained motor rig |
| 26 | Active PID commissioning | Constrained dynamometer/unloaded rig |
| 27 | Constrained full-stack and release validation | Complete accepted system |

## Phase 0 - Baseline and Decision Freeze

### Objective

Create a truthful baseline before configuration code changes.

### Work

- Record git commit, dirty state, tool versions, PlatformIO versions, ESP-IDF
  versions, Python/Node versions, and current generated CAN hashes.
- Build RT, SYS, MTR, and PWT current target environments.
- Run all currently registered RT, SYS, native-test, simulation, and generator
  checks.
- Record failing, placeholder, unregistered, skipped, hardware-only, and stale
  tests separately.
- Confirm current high/low/powertrain topology.
- Confirm PWT is standalone and not an RT/SYS low-bus peer.
- Confirm MTR owns future motor actuation and SYS motor ownership remains retired.
- Confirm encoder configuration is one subsystem switch plus fixed hardware map.
- Confirm build configuration cannot be changed by ordinary CAN.
- Confirm Control UI/HIL/physical work remains blocked through Phase 15.
- Create a decision record for unresolved speed-feedback ownership and active-PID
  fault response.

### Tests and evidence

- Baseline build report.
- Baseline test report with actual registered test count.
- Placeholder/unregistered test inventory.
- Current firmware artifact hashes.
- Current topology and ownership record.
- Current known-defect list.

### Exit gate

- Baseline is reproducible twice from the recorded source state.
- Every known failure has an owner phase.
- No unresolved decision blocks Phase 1 schema design.

## Phase 1 - Canonical Configuration Schema

### Objective

Define one typed configuration authority before adding macros or branches.

### Work

- Add a versioned canonical configuration schema under `config/system/` or an
  approved shared location.
- Define deployment profile: vehicle, controller-test, HIL, SIL/native.
- Define `UnitPolicy`: disabled, required physical, simulated.
- Define all unit policies from the inventory table.
- Define all output permission bits from the output-class table.
- Define encoder subsystem enabled/disabled.
- Define installed encoder hardware-map identity and calibration identity.
- Define speed-feedback source.
- Define PID state.
- Define expected bus topology and bitrate.
- Define firmware/hardware revision fields.
- Define test-only synthetic feedback restrictions.
- Define schema versioning and migration policy.
- Define safe defaults for tools that require defaults.
- Require vehicle configurations to set every safety-relevant value explicitly.

### Validation rules

- Reject unknown keys and unknown enum values.
- Reject simulated units in vehicle profile.
- Reject output enabled for a disabled unit.
- Reject active PID without approved feedback.
- Reject RT encoder feedback while encoder subsystem/hardware map is disabled.
- Reject SYS motor ownership.
- Reject PWT configured as current low-to-powertrain gateway.
- Reject DC-DC as directly controlled by RT/SYS in the current topology.
- Reject multiple physical actuator classes in isolated-unit configurations.
- Reject duplicate or conflicting unit ownership.

### Tests and evidence

- Valid fixture for every supported configuration.
- Invalid fixture for every validation rule.
- Schema version compatibility tests.
- Deterministic canonical normalization test.

### Exit gate

- All supported configurations validate.
- All forbidden configurations fail with stable actionable errors.
- No firmware code yet contains a second configuration authority.

## Phase 2 - Deterministic Configuration Compiler

### Objective

Generate typed build artifacts from the canonical configuration.

### Work

- Implement a read-only check mode and deterministic generation mode.
- Generate RT typed configuration header.
- Generate SYS typed configuration header.
- Generate MTR/PWT subsets where relevant.
- Generate a normalized JSON manifest without firmware binary hash.
- Generate test parameters for host/native and SIL.
- Generate TypeScript/Python configuration contracts for future tooling.
- Generate readable names for startup diagnostics.
- Add compile-time assertions for local invariants.
- Exclude wall-clock timestamps from generated content.
- Hash normalized semantic configuration, not source formatting/comments.

### Tests and evidence

- Two generation runs are byte-identical.
- Formatting/comment-only changes do not change semantic hash.
- Semantic changes do change semantic hash.
- Check mode is read-only and detects drift.
- Generated headers compile independently.
- Cross-language generated values are identical.

### Exit gate

- One canonical input produces all required deterministic artifacts.
- CI can verify generated drift without rewriting files.

## Phase 3 - PlatformIO and Build Integration

### Objective

Make every firmware build consume explicit generated configuration.

### Work

- Add generator/check hooks to RT, SYS, MTR, and PWT as applicable.
- Remove vehicle selection by undefined macro/default omission.
- Include generated configuration in target and native builds.
- Add explicit build environments or generated environment overlays for every
  supported committed configuration.
- Resolve and record effective ESP-IDF tick rate, stack sizes, CPU frequency,
  TWAI settings, and test defines.
- Make SYS target and native configuration consistent except platform HAL.
- Make RT target and native configuration consistent except platform HAL.
- Prohibit `TESTING` from changing production safety decisions.
- Restrict conditional compilation to platform drivers and compiled
  capabilities, not control policy.
- Add static assertions for forbidden local combinations.

### Tests and evidence

- Build every supported RT/SYS configuration.
- Build MTR/PWT with their relevant generated subset.
- Compile-failure tests for forbidden combinations.
- Resolved compiler-definition audit.
- Resolved `sdkconfig` audit.
- Header self-containment checks.

### Exit gate

- Every artifact reports an explicit configuration input.
- No target silently inherits simulation/test behavior.
- Target/native resolved semantic configuration matches.

## Phase 4 - Manifest and Startup Identity

### Objective

Make effective configuration observable and traceable.

### Work

- Complete build manifest with ECU, version, git identity, dirty state,
  environment, hardware revision, unit policies, output permissions, encoder
  state/map, feedback source, PID state, protocol hashes, resolved critical SDK
  values, and configuration hash.
- Add firmware binary SHA-256 after linking.
- Embed configuration/protocol identity in firmware.
- Print startup identity before dependent tasks start.
- Add a machine-readable local diagnostic response.
- Reserve CAN capability reporting for a generated read-only diagnostic contract;
  do not overload existing bits.
- Make mismatch diagnosis explicit between RT, SYS, test tooling, and catalogs.

### Tests and evidence

- Manifest schema and deterministic ordering tests.
- Embedded value equals manifest tests.
- Startup text golden test.
- Dirty/clean state tests.
- Firmware hash verification test.
- Protocol/configuration hash mismatch tests.

### Exit gate

- A captured startup report identifies the exact artifact and configuration.
- Artifact files and manifest hashes agree.

## Phase 5 - RT Unit-Policy Integration

### Objective

Replace RT missing-peer assumptions with explicit unit behavior.

### Work

- Implement Host disabled/physical/simulated policy.
- Implement SYS disabled/physical/simulated policy.
- Implement HMI disabled/physical/simulated policy for forwarding/authority.
- Implement MTR disabled/physical/simulated policy.
- Implement EPS-C disabled/physical/simulated policy.
- Implement SEB disabled/physical/simulated policy.
- Track configured policy separately from runtime state.
- Add never-seen startup deadlines for required units.
- Add fresh, stale, frozen, faulted, reconnecting, and recovered runtime states.
- Ensure disabled units create no timeout/readiness dependency.
- Ensure unexpected/wrong-bus frames cannot grant authority.
- Ensure reconnect does not restore stale Auto output.
- Keep unit policy separate from vehicle mode and output permission.

### Tests and evidence

- Generic policy contract for each RT peer.
- Never-seen, stale, frozen counter, malformed, wrong bus, reset, and reconnect.
- Disabled Host cannot grant command authority.
- Disabled EPS-C does not unlock drive.
- Disabled MTR suppresses motor capability.
- Disabled SEB suppresses direct brake capability.
- Simulated policies rejected in vehicle build.

### Exit gate

- Every RT peer has explicit policy and runtime state.
- Broad RT bypass flags are no longer needed by new code paths.

## Phase 6 - SYS Unit Policy and Brake Authority

### Objective

Make SYS peer behavior explicit and define SEB ownership safely.

### Work

- Implement RT disabled/physical/simulated policy.
- Implement HMI disabled/physical/simulated policy.
- Implement MTR disabled/physical/simulated policy.
- Implement SEB disabled/physical/simulated policy.
- Keep local body I/O capability separate from external-unit policy.
- Add never-seen and stale behavior for required RT/MTR/SEB inputs.
- Define brake owner states: SYS normal/manual/ESTOP, RT Auto, RT takeover,
  transition, fault/ambiguous.
- Define owner acquisition, release, timeout, counter handoff, and reconnect.
- Detect or infer dual-sender conflict where observable.
- Define stale `RT_BRAKE_CMD` behavior.
- Define stale RT state behavior used in SYS suppression.
- Define SEB fault latch and recovery semantics.
- Preserve physical ESTOP and mandatory safety behavior under every policy.

### Tests and evidence

- Generic policy contract for each SYS peer.
- Mode/ESTOP transitions with each peer disabled, present, stale, and reconnecting.
- SYS/RT SEB owner state-machine exhaustive transition tests.
- No dual sender in any valid sequence.
- Brake command cannot release unexpectedly during handoff/reconnect.
- SEB disabled means no `VCU_SEB_REQ` output and no ready claim.
- MTR disabled means no EGAS/ACK readiness claim and motor unavailable.

### Exit gate

- SEB ownership has one tested authority at every state.
- SYS unit policies no longer depend on broad bypass globals.

## Phase 7 - MTR/PWT Policy and Topology Integration

### Objective

Prevent RT/SYS configuration from claiming unimplemented MTR/PWT behavior.

### Work

- Preserve compile rejection of SYS motor ownership.
- Add MTR build manifest, output permission, and motor-controller policy.
- Define MTR physical readiness/status separate from fault flags.
- Complete software-side command timeout and safe-output policy.
- Define direct ESTOP requirement as unavailable until hardware implementation.
- Add PWT/DC-DC unit policy and output permission.
- Keep PWT standalone on 250 kbit/s CAN.
- Remove or flag stale low-to-powertrain gateway assumptions in active docs/tests.
- Prevent PWT/DC-DC configuration from affecting RT/SYS low bus.
- Define traction motor controller as plant behind MTR, not an RT/SYS CAN peer.

### Tests and evidence

- MTR disabled/simulated/required policy tests.
- MTR motor output permission tests with fake HAL.
- PWT/DC-DC disabled/simulated/required policy tests.
- PWT extended-ID/DLC/constant-field vectors.
- Build rejects PWT gateway topology on current hardware.
- Build rejects physical MTR readiness while required HAL capability is absent.

### Exit gate

- Manifests state MTR/PWT capabilities truthfully.
- No software test claims unavailable physical readiness.

## Phase 8 - Central Output Policy

### Objective

Make every actuator or forwarding output pass one enforceable final policy.

### Work

- Inventory every direct CAN driver call in RT, SYS, MTR, and PWT.
- Route RT steering, drive, brake-to-SYS, direct brake, and gateway sends through
  permission-classified output handling.
- Route diagnostics, status, and heartbeat traffic through the same centralized
  send boundary, but classify them separately from actuator permissions.
- Route SYS SEB and body outputs through classified output handling.
- Route MTR DAC/gear and PWT DC-DC output through classified output handling.
- Keep safety-safe output behavior explicit; do not silently drop a required
  safe command without a defined policy.
- Enforce unit-policy compatibility and output permission.
- Count denied, failed, expired, and dropped outputs.
- Expose output-policy state and denial reason.
- Add controller-test global physical-output inhibit.
- Ensure a brief forbidden pulse cannot occur during startup/reset/transition.

### Tests and evidence

- Every output class allowed/denied tests.
- Every direct driver path covered.
- Output inspected on every simulated control cycle.
- Startup/reset/ESTOP/reconnect transient tests.
- No non-allowlisted actuator frame or GPIO transition.
- Denial counters and diagnostics tested.

### Exit gate

- Repository search finds no actuator send/write bypassing output policy.
- Controller-test configuration proves zero actuator output.

## Phase 9 - Broad Bypass Retirement

### Objective

Remove ambiguous `SYSTEM_RUN_MODE` and grouped bypass behavior after replacement.

### Work

- Map each existing run-mode behavior to deployment profile, unit policy, output
  permission, or test-only HAL.
- Remove grouped setting of EPS, SEB, and MTR bypasses.
- Remove unused bypass globals and dead consumers.
- Replace GPIO35 broad override with a narrowly defined physical test interlock
  only if still required.
- Separate CAN controller listen-only behavior from peer/output policy.
- Remove `TESTING` branches that replace production safety decisions.
- Preserve host/native HAL substitution without changing control semantics.
- Update startup messages and documentation.
- Add negative search/audit in CI for retired symbols.

### Tests and evidence

- Migration behavior tests for every replaced path.
- Vehicle artifact contains no bypass capability.
- Controller-test artifact cannot actuate without output permission.
- Retired-symbol repository audit.

### Exit gate

- No production behavior depends on the broad run mode or grouped bypasses.
- Retired compatibility code is deleted.

## Phase 10 - Open-Loop No-Encoder Baseline

### Objective

Establish the first complete supported configuration.

### Configuration

- Encoder subsystem disabled.
- Speed feedback none or explicitly telemetry-only MTR report.
- PID disabled.
- Unit policies selected for the software setup.
- All safety requirements enabled.

### Work

- Prevent encoder GPIO/PCNT initialization.
- Remove encoder readiness/freshness dependency.
- Reset and skip PID calculation completely.
- Keep bounded open-loop drive mapping.
- Keep mode, ESTOP, timeout, steering, brake, CAN, and watchdog behavior.
- Report encoder/PID capability unavailable.
- Ensure SYS accepts declared open-loop operation without claiming encoder
  supervision.

### Tests and evidence

- Complete RT command path with no encoder access.
- Complete RT/SYS software interaction.
- Manual/Auto/Estop and recovery.
- Host/RT/SYS/MTR timeout combinations.
- Steering and brake safety unaffected.
- Reset/reconnect and stale-command rejection.
- No encoder/PID code affects output or timing.

### Exit gate

- Open-loop/no-encoder native matrix passes.
- This configuration becomes the reference baseline for later comparisons.

## Phase 11 - Encoder Subsystem Software Implementation

### Objective

Implement correct optional acquisition without actuator hardware.

### Work

- Add one subsystem enable/disable setting.
- Add reviewed installed-channel hardware map and calibration identity.
- Correct front-wheel GPIO documentation mismatch.
- Initialize only installed channels.
- Replace zero/no-op stubs with typed disabled/initializing/valid/stale/faulted
  states.
- Read interval delta with clear-or-wrap-safe previous-count handling.
- Use measured elapsed time.
- Resolve 2x versus 4x decoding and pulses-per-revolution.
- Add direction, PPR, geometry, glitch filter, plausible rate, freshness, and
  reset behavior.
- Keep channel health independent while enable/disable remains subsystem-level.
- Separate telemetry from feedback authority.

### Tests and evidence

- Disabled subsystem touches no PCNT/GPIO.
- Enabled subsystem initializes installed channels only.
- Positive/negative/zero delta.
- Wrap, read error, stale sample, noise, implausible jump, reset, reinit.
- Calibration vectors for every installed channel.
- Valid stationary zero distinct from unavailable/faulted.
- Host-testable encoder core plus compile-only target driver checks.

### Exit gate

- Encoder acquisition is software-correct and still has no actuator authority.
- Physical electrical testing remains blocked until Phase 19.

## Phase 12 - Speed Feedback and PID Strategy

### Objective

Make PID authority explicit and correct the control dataflow.

### Work

- Add feedback-source abstraction: none, MTR report, RT rear encoder.
- Represent availability, validity, freshness, timestamp, and source.
- Add PID disabled, shadow, active states.
- Disabled PID resets and performs no calculation.
- Shadow PID calculates telemetry and cannot alter commands.
- Active PID requires approved fresh RT encoder feedback.
- Prohibit active PID on current MTR command-echo feedback.
- Correct ordering so speed strategy executes before final setpoint queue write.
- Apply final clamp after correction.
- Define feedback-loss safe response.
- Prohibit automatic powered open-loop fallback unless separately approved.
- Add bumpless activation and complete state reset.
- Move gains/limits into versioned configuration with placeholder/approved state.

### Tests and evidence

- Disabled PID output and state always zero/reset.
- Shadow command byte-for-byte equals Phase 10 baseline.
- Active correction appears in final queue/frame exactly once.
- Safety zeroing dominates PID.
- Valid zero, unavailable, stale, frozen, reversed, noisy feedback.
- Anti-windup, derivative filter, setpoint reset, saturation, `dt` errors.
- Feedback loss during shadow and active.
- Gain/configuration mutation blocked while active.

### Exit gate

- PID semantics are complete in software.
- Active PID remains disabled in approved physical/vehicle configurations.

## Phase 13 - Complete Host/Native Verification

### Objective

Close all software-level coverage before simulation or hardware tooling.

### Work

- Refactor production calculations/state machines out of monolithic `main.cpp`
  where needed for direct host testing.
- Compile production modules rather than copying logic into tests.
- Replace placeholder RT/SYS tests.
- Register useful unregistered native tests or delete obsolete copies.
- Add full configuration matrix tests.
- Add generic unit-policy contract tests.
- Add output-policy cycle-by-cycle tests.
- Add RT/SYS brake ownership tests.
- Add codec golden vectors and invalid-frame tests.
- Add deterministic virtual-clock timeout/state tests.
- Add property/state-machine generated sequences.
- Add concurrency ownership tests for shared mutable state.
- Add queue-full, allocation-failure, reset-default, storage-failure, and task
  creation failure behavior where host-testable.
- Run static analysis, sanitizers, header checks, and formatting.
- Measure line/branch and decision-condition coverage for safety decisions.
- Add mutation tests for critical comparisons and timeouts where practical.

### Exit gate

- Every supported configuration passes native tests.
- Every forbidden configuration fails before execution.
- No placeholder test remains in the required gate.
- No copied production algorithm is accepted as primary coverage.
- Software report lists zero unexplained failures/skips.

## Phase 14 - CI, Artifact Approval, and Flash Gate

### Objective

Automate reproducible approval of exact firmware artifacts.

### Work

- Add fast PR, safety-sensitive PR, nightly, and release-candidate gates.
- Build every supported committed configuration.
- Verify forbidden configurations fail.
- Verify generated files are current and worktree remains clean.
- Produce firmware ELF/bin, manifest, maps, logs, and test report.
- Compute and verify firmware/configuration/protocol hashes.
- Add approved-artifact registry or evidence directory convention.
- Implement flash-existing-artifact script with hash verification.
- Prevent final physical workflow from rebuilding implicitly.
- Record board identity, operator, timestamp, and artifact on flash.
- Define evidence expiry after source/config/toolchain changes.

### Exit gate

- One command produces a complete software evidence bundle.
- A separate command flashes only an approved existing artifact.
- Hash mismatch blocks flashing.

## Phase 15 - Pure Software/SIL Integration

### Objective

Exercise complete software scenarios only after host/native logic is closed.

### Work

- Make simulation consume canonical configuration and generated codecs.
- Select units as disabled or simulated; no unit is implicitly instantiated.
- Add all unit policies and output permissions.
- Add Host, HMI, RT, SYS, MTR, EPS-C, SEB, PWT/DC-DC models as required.
- Add simple physical plant models without duplicating RT/SYS decisions.
- Add valid counters/checksums/rates/startup/loss behavior.
- Add fault injection for missing, stale, frozen, corrupt, wrong-bus, duplicate,
  delayed, reset, reconnect, and queue pressure.
- Cross-check model outputs against production golden vectors and invariants.
- Label approximations and unavailable evidence.
- Use virtual buses only.

### Tests and evidence

- Unit-disabled matrix.
- Unit-simulated matrix.
- Open-loop no-encoder baseline.
- Encoder telemetry and shadow PID software scenarios.
- Mode/ESTOP/brake ownership and reconnect scenarios.
- Full fault/corruption/boundary matrix.
- Seeded soak and deterministic replay.

### Exit gate

- SIL matrix passes and reproduces from saved seeds.
- Simulation does not hide missing production coverage.
- No physical adapter or physical TX is used.

## Phase 16 - Control UI Backend Foundation

### Objective

Implement future bench tooling only after firmware/software foundations pass.

### Work

- Create FastAPI backend and shared Pydantic/OpenAPI contracts.
- Generate Python protocol codecs/metadata.
- Implement virtual CAN adapter first.
- Implement bounded frame routing, timebase, sequences, state, recording, events,
  and evidence.
- Implement session state, revision, capabilities, Bench TX state, source
  ownership, leases, deadlines, scheduler, and cleanup.
- Implement synthetic peer service against virtual CAN only.
- Add test runner with preconditions, stimuli, assertions, timeout, cleanup, and
  verdict.
- Add adapter abstraction and fake adapter contract tests.
- Port CANalyst characterization vectors without enabling hardware TX.
- Keep React a client; backend owns all timing/state.

### Tests and evidence

- API/schema compatibility.
- Virtual session lifecycle.
- Source conflict and lease expiry.
- Scheduler counters/checksums/jitter behavior.
- Stop All and shutdown leak checks.
- Recording completeness and queue-overflow inconclusive verdict.
- Synthetic peers restricted to virtual adapter.

### Exit gate

- Complete virtual Control UI backend tests pass.
- Physical adapter TX remains unavailable/disarmed.

## Phase 17 - Real-Controller Tests

### Objective

Validate real RT/SYS boards without physical actuators.

### Work

- Implement characterized `python-can` CANalyst-II adapter.
- Verify and persist channel mapping, bitrate, adapter identity, timestamp quality,
  and limitations.
- Add physical monitor mode with Bench TX disabled.
- Add finite Bench TX with physical interlock/confirmation as required.
- Use output-inhibited firmware artifacts.
- Test RT alone with scripted/synthetic peers.
- Test SYS alone with scripted/synthetic peers and protected GPIO fixtures.
- Test RT+SYS together with missing units synthetic.
- Measure CAN rates, startup, driver behavior, watchdog, reset, bus load, adapter
  disconnect, and controller reconnect.
- Keep EPS-C, SEB, MTR outputs, motor controller, PWT, and DC-DC disconnected.

### Exit gate

- RT-only, SYS-only, and RT+SYS controller reports pass.
- No actuator command escapes output inhibit.
- Exact firmware and configuration hashes are recorded.

## Phase 18 - Closed-Loop HIL

### Objective

Run real RT/SYS firmware against stateful external peers and plant models.

### Work

- Substitute real controllers selectively in the SIL topology.
- Simulate Host/HMI/MTR/EPS-C/SEB only when those units are absent.
- Receive real commands and generate stateful feedback.
- Maintain production timing, counters, checksums, startup, and loss behavior.
- Add closed-loop speed, steering, and brake plant response.
- Add every required fault sequence.
- Enforce one source per bus/ID and stop on physical/synthetic conflict.
- Record scheduler jitter and adapter evidence limitations.
- Use oscilloscope/logic analyzer for timing the USB adapter cannot prove.

### Exit gate

- RT HIL, SYS HIL, and RT+SYS HIL pass.
- Physical actuators remain disconnected.
- Applicable physical-unit phase is authorized only for exact passing artifacts.

## Phase 19 - Host, HMI, Body I/O, and Encoder Electrical Tests

### Objective

Validate low-energy interfaces before actuator units.

### Work

- Test physical Host/Jetson protocol with all actuator outputs inhibited.
- Test physical HMI protocol and counter/freshness behavior.
- Test SYS ESTOP, mode, start, brake lever, and light switches using isolated
  fixtures.
- Test SYS lamps/bulbs/relay with protected dummy loads.
- Test encoder subsystem disabled state electrically.
- Enable encoder subsystem with approved hardware map and one physical channel at
  a time.
- Use signal generator/manual rotation to verify count, direction, scaling,
  filter, maximum rate, disconnect, stale, fault, reset, and power cycle.

### Exit gate

- Low-energy physical interface evidence passes.
- No actuator power has been applied.

## Phase 20 - EPS-C Physical Integration

### Objective

Accept steering component and RT/EPS-C interaction in isolation.

### Work

- Direct EPS-C component test with test station.
- RT plus EPS-C with Host/SYS/MTR simulated only as required.
- Enable `RT_STEERING` output only.
- Mechanically constrain rack and establish exclusion zone.
- Verify boot/alignment, center, direction, scale, bounds, slew, following error,
  status freshness, counter/checksum, errors, command loss, feedback loss,
  ESTOP, reset, reconnect, and power cycle.
- Confirm no synthetic EPS-C feedback while physical EPS-C is connected.

### Exit gate

- EPS-C component and RT integration reports pass.
- Steering output remains prohibited in unrelated physical configurations.

## Phase 21 - SEB Physical Integration

### Objective

Accept brake component and both command authorities safely.

### Work

- Direct SEB component test on guarded hydraulic rig.
- SYS plus SEB test for Manual/ESTOP authority.
- RT plus SEB test for Auto/takeover authority.
- RT+SYS+SEB owner handoff only after separate paths pass.
- Enable only the applicable brake output class per test.
- Measure stroke and pressure independently.
- Verify alignment, release, hold, command loss, status loss, following error,
  checksum/counter, fault levels, ESTOP, reset, reconnect, and no dual sender.
- Determine and document SEB CAN-loss hold/release behavior.

### Exit gate

- Brake component, each controller path, and owner handoff pass.
- Unexpected release or ambiguous authority blocks progression.

## Phase 22 - MTR and Traction Motor Integration

### Objective

Complete and accept motor actuation progressively.

### Work

- Complete MTR clock/GPIO/I2C/ADC/CAN HAL and direct ESTOP.
- Test MTR with DAC/gear outputs disconnected.
- Test DAC and gear outputs with dummy loads and measurement equipment.
- Test MTR plus physical motor controller on current-limited unloaded/dynamometer
  rig.
- Add RT/SYS with EPS-C/SEB/PWT disconnected.
- Enable `MTR_MOTOR` only after dummy-load acceptance.
- Verify Manual and Auto mapping, gear interlocks, command timeout, zero output,
  direct ESTOP, feedback truthfulness, CAN loss, bus-off, reset, reconnect,
  thermal behavior, and SYS supervision.
- Do not call command echo physical speed.

### Exit gate

- MTR hardware and motor-controller reports pass.
- Motor capability can be marked required physical only after direct ESTOP and
  safe-output evidence pass.

## Phase 23 - PWT and DC-DC Integration

### Objective

Accept the standalone powertrain node and DC-DC command path.

### Work

- Use isolated 250 kbit/s powertrain CAN.
- Test PWT alone with DC-DC simulator.
- Test PWT plus physical DC-DC under controlled load/power procedures.
- Enable `PWT_DCDC` only for this setup.
- Verify extended ID, DLC, constants, rate, enable/disable, TX failure, bus-off,
  watchdog, reset, reconnect, and power cycle.
- Keep 500 kbit/s low CAN physically separate.
- Do not claim low-to-powertrain forwarding or PWT heartbeat.

### Exit gate

- PWT/DC-DC evidence passes for standalone topology.
- Any future gateway requirement returns to Phase 1 with new hardware/topology.

## Phase 24 - Complete Open-Loop Subsystem Integration

### Objective

Integrate accepted units while encoders and PID remain disabled.

### Work

- Flash approved open-loop/no-encoder artifacts.
- Add accepted units progressively, one subsystem at a time.
- Verify all physical/synthetic policies match the manifest.
- Run Manual/Auto/Estop, forward/reverse, steering, brake, gear, Host/HMI,
  heartbeat loss, command loss, reset, reconnect, power sequencing, and bus-off.
- Verify RT/SYS/SEB ownership and MTR supervision under real feedback.
- Keep vehicle lifted or mechanically constrained.
- Keep PWT powertrain bus topology separate as designed.

### Exit gate

- Complete required open-loop physical behavior passes with encoders/PID disabled.
- This becomes the physical reference baseline.

## Phase 25 - Encoder Telemetry and Shadow PID

### Objective

Add feedback observation without changing the accepted open-loop command.

### Work

- Enable approved encoder subsystem/hardware map.
- Keep PID disabled; compare command and physical speed.
- Validate direction, scale, freshness, noise, dropout, thermal/soak behavior.
- Enable shadow PID only after telemetry passes.
- Replay identical command sequences against Phase 24 baseline.
- Prove emitted drive command is byte-for-byte unchanged.
- Validate PID telemetry, saturation, reset, anti-windup, and feedback faults.

### Exit gate

- Encoder telemetry is accepted.
- Shadow PID cannot affect actuation.
- Phase 24 behavior remains unchanged.

## Phase 26 - Active PID Commissioning

### Objective

Commission limited closed-loop control on a constrained rig.

### Preconditions

- Phases 0-25 pass.
- Approved physical encoder feedback.
- Reviewed gains, correction bounds, freshness, plausibility, and safe response.
- Independent emergency disconnect, deadman, current limiting, and dynamometer or
  unloaded constrained setup.

### Work

- Start with zero command and zero correction authority.
- Enable limited correction incrementally.
- Verify bumpless activation, ramps, small steps, command removal, disturbance,
  saturation, anti-windup, and direction.
- Inject dropout, frozen value, reversed sign, noise, implausible jump, CAN loss,
  command loss, reset, and ESTOP.
- Verify no automatic powered open-loop fallback.
- Perform thermal and duration soak.
- Tune only through versioned reviewed configuration.

### Exit gate

- Limited active PID rig evidence passes.
- Vehicle movement remains blocked until Phase 27 approval.

## Phase 27 - Constrained Full-Stack and Release Validation

### Objective

Validate the exact release artifact and complete physical topology.

### Work

- Build and approve exact release artifacts.
- Require all production units physical; synthetic safety feedback disabled.
- Verify startup manifests/configuration/protocol hashes across system.
- Perform constrained dry run with wheels lifted/secured.
- Repeat critical mode, ESTOP, timeout, reset, reconnect, power sequence, bus-off,
  ownership, output-bound, and command-freshness tests.
- Verify no commissioning/test output permission remains.
- Verify Control UI is passive by default and any test stimulus is finite,
  explicit, and recorded.
- Review all evidence, deviations, unavailable capabilities, and residual risks.
- Proceed to closed-area walking-speed testing only under a separate approved
  vehicle procedure.

### Exit gate

- Release report identifies exact firmware/configuration/hardware.
- No synthetic required-unit feedback is active.
- All earlier phase evidence remains valid for the release source state.
- Safety review authorizes the next operational test stage.

## Cross-Cutting Software Test Matrix

Every applicable phase must preserve these invariants:

| Area | Required invariant |
|---|---|
| Configuration | Unknown or forbidden values fail closed |
| Unit policy | Disabled unit is not required and cannot grant authority |
| Runtime health | Required unit missing/stale/frozen/faulted is not treated disabled |
| Simulation | Simulated policy cannot appear in vehicle release |
| Output | Unit presence alone never authorizes output |
| Output | Non-allowlisted output never pulses, including startup/reset |
| Mode | Manual/Auto/Estop remains separate from build/test configuration |
| ESTOP | ESTOP dominates commands, PID, reconnect, and ownership transitions |
| Freshness | Stale data cannot retain or restore authority |
| Encoder | Disabled subsystem is not initialized or read |
| Encoder | Valid stationary zero differs from unavailable/faulted |
| PID | Disabled PID calculates/applies nothing and resets state |
| PID | Shadow PID cannot alter output |
| PID | Active PID requires fresh approved feedback |
| Brake | Exactly one SEB command owner at a time |
| Reset | Reset/reconnect never restores stale command/session/lease |
| CAN | Wrong bus, DLC, checksum, counter, enum, or range cannot grant authority |
| Artifact | Test report configuration/hash matches flashed binary |
| Evidence | Hidden loss/drop makes formal result inconclusive, not pass |

## Unit Policy Coverage Checklist

Run for each applicable unit:

- disabled from boot;
- required physical and never seen;
- required physical healthy;
- required physical malformed;
- required physical stale;
- required physical frozen counter;
- required physical faulted;
- required physical disconnected;
- required physical reconnecting;
- required physical recovered after stability window;
- simulated healthy;
- simulated malformed/faulted;
- simulated policy rejected in vehicle release;
- unexpected traffic while disabled;
- wrong-bus traffic;
- source conflict;
- output inhibited;
- output allowed only with compatible policy;
- reset during each state.

## Fault Coverage Checklist

- bad DLC;
- standard/extended mismatch;
- wrong bus;
- unknown ID;
- invalid enum;
- below/above range;
- checksum failure;
- counter duplicate, gap, rollback, wrap, and frozen;
- missing first frame;
- stale after healthy operation;
- delayed/reordered/duplicate frame;
- queue full/drop;
- CAN TX failure;
- bus-off/recovery;
- adapter disconnect/reconnect;
- task stall;
- watchdog reset;
- brownout/power cycle;
- storage/evidence failure;
- configuration mismatch;
- protocol hash mismatch;
- unit source conflict;
- output-policy denial;
- encoder read error/stale/noise/reverse;
- PID feedback loss/saturation/state reset;
- SEB dual-sender/authority ambiguity;
- MTR command timeout/direct ESTOP;
- PWT/DC-DC command loss.

## Evidence Bundle Checklist

Every formal gate records:

- phase and procedure revision;
- requirement/test identifiers;
- operator and date where applicable;
- git commit and dirty state;
- toolchain/dependency versions;
- ECU/hardware/adapter identity;
- canonical configuration and hash;
- protocol/network-contract hashes;
- firmware ELF/bin and SHA-256;
- build logs;
- test logs and counts;
- raw CAN capture;
- serial/diagnostic capture;
- scope/logic/physical measurements where required;
- queue/drop/timing/jitter evidence;
- physical/simulated/disabled unit manifest;
- output allowlist;
- expected and actual results;
- Pass, Fail, Blocked, Aborted, Incomplete, or Inconclusive verdict;
- anomalies, deviations, unavailable evidence, and residual risks.

## Cleanup Rule

Every phase must:

- identify replaced files, flags, globals, tests, docs, and generated artifacts;
- migrate all required consumers before deletion;
- remove superseded paths in the same phase unless a named compatibility window
  has an owner and deletion phase;
- search for stale symbols and documentation;
- avoid adding new behavior to a legacy path scheduled for removal;
- keep generated and runtime output out of source control unless it is an
  approved deterministic artifact/fixture;
- leave required builds and tests green twice where nondeterminism is a concern.

## Stop Conditions

Stop work on the current or later phase when:

- configuration semantics are ambiguous;
- output can bypass policy;
- a required unit can be falsely reported healthy;
- a disabled unit still creates a hidden dependency;
- a test exercises copied logic rather than production code;
- generated artifacts drift;
- exact artifact identity cannot be established;
- simulator behavior contradicts production codec/state rules;
- controller/HIL output is not physically isolated;
- unexpected actuator motion occurs;
- ESTOP, timeout, reset, or reconnect behavior is unsafe or unproven;
- evidence is incomplete or silently dropped;
- physical wiring/topology differs from the approved manifest.

## Final Definition of Done

The project is done with this plan only when:

- Phases 0-27 pass in order;
- all completion criteria in the feature configuration plan pass;
- all required unit, output, configuration, and fault matrix rows have evidence;
- no required test is placeholder, copied, unregistered, skipped, or stale;
- release firmware is explicit, deterministic, hash-identified, and free of
  simulation/bypass capability;
- Control UI physical TX is finite, session-owned, source-conflict-safe, and
  passive by default;
- physical units have been accepted individually before combined operation;
- the complete open-loop no-encoder configuration is accepted independently of
  optional encoder/PID work;
- active PID remains optional and cannot block or invalidate the accepted
  open-loop product configuration;
- final release evidence is reviewed and approved before any road/occupied
  vehicle testing.
