# Debug Tool Work Plan

**Purpose:** Incrementally bring the existing debug tool to `debug-tool-architecture.md` without a rewrite.

## Non-negotiable phase rule

Phases are sequential. Do not start phase N+1 until all code, tests, documentation, and the exit gate for phase N pass. If a later phase exposes a regression, return to the phase that owns the broken contract, fix it, and rerun every gate from there forward.

Each phase should be a small reviewable change. Preserve unrelated user work. Add regression tests with correctness fixes. Hardware tests remain opt-in and run only on a controlled bench.

## Baseline observed on 2026-07-10

- Backend Vitest: 201 tests pass.
- UI Vitest: 129 tests pass.
- UI `svelte-check`: two errors in `PipelineView.svelte`.
- The shared decoder loads YAML, but protocol definitions and packing remain duplicated.
- Modes are mutable configurations, not a serialized state machine.
- Failed physical commands may fall back implicitly to simulation.
- Some simulation routes bypass the frame router.
- Write and WebSocket frame buffers have no hard capacity or overload metrics.
- There is no command ownership or lease mechanism.
- Timestamps mix wall/device clocks and seconds/milliseconds; equal timestamps have no sequence.
- Transport faults are not routed and recorded as first-class events.
- SQLite writes already use a queue and worker; do not discard that boundary without evidence.
- MQTT and the standalone simulator remain legacy paths.

## Phase 1 — Restore a green software baseline

### Work

- Fix both `PipelineView.svelte` type errors deliberately; do not suppress them.
- Identify canonical shared/backend/UI check, build, and test commands.
- Add one root software-verification command that runs them in dependency order.
- Capture current failures in tests before fixing any behavioral regression discovered here.

### Tests and exit gate

- Shared build passes.
- Backend typecheck, build, and all tests pass.
- UI typecheck, build, and all tests pass.
- The root verification command returns zero twice consecutively.
- Commit/merge only after the full gate is green.

## Phase 2 — Consolidate the Playwright baseline

### Work

- Inventory both Playwright suites and map unique scenarios.
- Select one maintained directory/configuration.
- Replace brittle structural selectors with scoped roles or stable `data-testid` values.
- Migrate unique useful cases, then remove the duplicate suite.
- Keep hardware scenarios tagged and excluded from software-only runs.

### Tests and exit gate

- Primary workflows cover startup, tab navigation, monitor, injector validation, mode display, recording controls, and disconnect state.
- Software-only E2E passes twice without retries hiding failures.
- A test intentionally using a stale selector fails, proving the suite is actually executing.
- Phase 1 gate remains green.

## Phase 3 — Specify and test CAN bit semantics

### Work

- Document Motorola and Intel bit numbering used by YAML and generated artifacts.
- Build golden vectors from the repository generator/DBC/firmware-compatible source.
- Fix signed encoding to use BigInt throughout, including 32-bit negative values.
- Normalize enum option representation.

### Tests and exit gate

- Golden decode vectors exist for every YAML message.
- Injectable messages have encode/decode round trips.
- Signed 32-bit min, max, `-1`, and zero pass.
- Cross-byte, non-byte-aligned, both-endian, factor, offset, and enum cases pass.
- Earlier phase gates remain green.

## Phase 4 — Make codec validation fail closed

### Work

- Reject unknown-message encoding, invalid bus/ID/DLC, non-finite values, range violations, invalid enums, bit-width overflow, and unrepresentable scaled values.
- Validate signal overlap during generation/load; represent intentional multiplexing explicitly.
- Return typed actionable codec errors rather than empty payloads or silent masks.
- Define checksum and rolling-counter extension hooks.

### Tests and exit gate

- Boundary and one-beyond-boundary tests exist for every signal kind.
- Unknown and malformed inputs cannot produce a transmit-ready frame.
- Fuzz/property tests never crash, hang, or silently wrap invalid values.
- Steering and brake checksum fixtures pass.
- Earlier phase gates remain green.

## Phase 5 — Generate normalized and typed protocol artifacts

### Work

- Extend the CAN generator to emit normalized runtime metadata and typed TypeScript message/signal constants.
- Include bus, numeric/formatted ID, DLC, sender, cycle, enums, and checksum/counter metadata.
- Make generation deterministic and add `--check`/stale-output verification.
- Record a protocol artifact hash for recording metadata.

### Tests and exit gate

- Two generator runs produce byte-identical output.
- Check mode fails after intentional generated-file drift and passes after regeneration.
- Generated artifacts typecheck in shared, backend, and UI.
- YAML schema and generated catalog consistency tests pass.
- Earlier phase gates remain green.

## Phase 6 — Migrate protocol-generic consumers

### Work

- Move dictionary, generic injector, message cards, signal tables, and shared injection templates to generated/runtime metadata.
- Migrate consumers of `ui/src/lib/can-index.ts` before deleting it.
- Remove obsolete generated catalogs only after import search is clean.

### Tests and exit gate

- Dictionary renders all generated messages and fields on both buses.
- Generic injection encodes representative boolean, enum, signed, scaled, and checksum messages.
- No production generic screen imports a hand-maintained catalog.
- Deleted files have no imports, scripts, or documented regeneration path remaining.
- Earlier phase gates remain green.

## Phase 7 — Migrate semantic protocol consumers

### Work

- Replace raw IDs and signal-key strings in telemetry, faults, Topbar, Controller, UnitTest, pipeline correlation, API commands, and ECU models with generated semantic constants.
- Keep raw IDs only in wire fixtures, adapter parsing, and tests intentionally asserting protocol bytes.
- Generate or centralize fault metadata rather than maintaining duplicate bit arrays.

### Tests and exit gate

- Renaming a required YAML semantic key causes the relevant consumer build/test to fail.
- `rg` finds no unexplained raw CAN IDs in production semantic workflows.
- Telemetry, fault, correlation, controller, and command regression tests pass.
- Earlier phase gates remain green.

## Phase 8 — Introduce session timebase and sequence contracts

### Work

- Add `SessionTimebase`, `TimebaseMapper`, canonical microsecond timestamps, and monotonic session sequences.
- Map host, adapter, simulation, and replay clocks without moving canonical time backward.
- Serialize bigint values as decimal strings at JSON boundaries and store safe integers in SQLite.
- Retain migration readers for existing timestamps until stored data is handled explicitly.

### Tests and exit gate

- Seconds/milliseconds/microseconds cannot be confused by API types or validation.
- Equal timestamps order by sequence deterministically.
- Adapter timestamp reset creates an event and a new mapping segment without time reversal.
- JSON and SQLite round trips preserve large timestamps/sequences exactly.
- Earlier phase gates remain green.

## Phase 9 — Introduce immutable raw frames and transport events

### Work

- Implement `CanDataFrame`, `DecodedMessage`, and `RoutedFrame` separation.
- Enforce standard/extended ID, remote-frame, DLC, and payload invariants.
- Add typed bus-off, recovery, overflow, adapter disconnect, error-frame, and timestamp-reset events.
- Publish adapter event/timestamp capabilities.

### Tests and exit gate

- Raw frame bytes cannot be mutated by decoding or downstream consumers.
- Re-decoding a recorded raw frame with the same protocol artifact yields the same interpretation.
- Remote, standard, extended, invalid DLC, and invalid payload combinations are covered.
- Every supported fake adapter event reaches diagnostics and recording paths.
- Earlier phase gates remain green.

## Phase 10 — Build the operational state machine

### Work

- Implement serialized `offline`, `monitor`, `simulation`, and `replay` transitions with revision numbers.
- Make physical arming an orthogonal `disarmed/arming/armed` state.
- Convert existing `full-sim`, `bench`, and related labels to profiles or migration aliases.
- On every transition, disarm first, revoke leases, stop periodic jobs/producers, and clear source queues.
- On partial failure, land in `offline/disarmed`.

### Tests and exit gate

- Table-driven tests cover every allowed and rejected transition.
- Concurrent transition requests serialize or return conflict without mixed state.
- Disconnect, bus-off, interlock loss, shutdown, and transition immediately disarm.
- Reconnect never restores arming.
- Failure injection at each transition step ends `offline/disarmed` with no active timers.
- Earlier phase gates remain green.

## Phase 11 — Implement routing matrix enforcement

### Work

- Make all physical, simulation, replay, user, and test producers enter one router.
- Encode destination permissions from the architecture routing matrix.
- Assign source instance and exactly one sequence per accepted observation.
- Prevent physical echo, simulation feedback loops, replay-to-hardware, and test access to production transports.
- Remove direct store/WebSocket calls from simulation routes.

### Tests and exit gate

- A table-driven test covers every source/destination matrix cell.
- Physical RX cannot produce physical TX.
- Simulation and replay cannot reach physical TX under any profile.
- Each input is processed once and produces one audit disposition.
- Loop detection tests cover explicit and accidental model edges.
- Earlier phase gates remain green.

## Phase 12 — Bound every queue and expose overload metrics

### Work

- Implement shared queue metrics: depth, capacity, high-water mark, accepted, dropped, rejected, and oldest age.
- Bound UI latest state, monitor history, live history, recording, physical TX, replay, simulation-step, WebSocket-client, and transport-RX boundaries.
- Implement the architecture's distinct overload policy for each boundary.
- Add safe configuration defaults, maximums, and status API output.

### Tests and exit gate

- Tiny-capacity tests force every queue into overload and assert its exact policy.
- Recording never silently drops; it backpressures or becomes visibly incomplete.
- Physical commands expire/reject rather than waiting indefinitely.
- Replay pauses and simulation step fails/pauses deterministically.
- Slow WebSocket clients cannot grow backend memory without bound.
- Earlier phase gates remain green.

## Phase 13 — Implement identities and control leases

### Work

- Assign authenticated connection/session owner identities.
- Implement atomic acquire, renew, release, expiry, and revocation for steering, motor, brake, and scoped periodic resources.
- Bind leases to operational-state revision and conceal lease secrets from status output.
- Revoke on disconnect, transition, disarm, adapter/interlock failure, and heartbeat expiry.
- Keep ESTOP independent of conflicting leases.

### Tests and exit gate

- Two owners cannot acquire the same resource concurrently.
- The wrong owner/token cannot renew, command, or release a lease.
- Expiry and every revocation trigger stop affected periodic/actuator output.
- REST and WebSocket disconnect identity behavior is covered.
- ESTOP succeeds while another client owns all actuator leases.
- Earlier phase gates remain green.

## Phase 14 — Centralize injection policy and physical arm

### Work

- Route UI, REST, periodic, simulation-control, tests, and future automation through one injection service.
- Separate decoded-signal injection from explicitly authorized raw-byte injection.
- Enforce owner, mode, route, lease, codec, checksum/counter, rate, freshness, arm, adapter health, and interlock checks.
- Remove implicit hardware-to-simulation fallback; callers select an allowed destination.
- Make physical arm short-lived, operator-confirmed, heartbeat-renewed, and visibly reported.

### Tests and exit gate

- A policy decision-table test covers source, mode, destination, lease, arm, and adapter/interlock combinations.
- Startup and reconnect are disarmed.
- Stale commands and counters are rejected.
- No endpoint or periodic path bypasses policy.
- Hardware-send failure reports failure and never injects into simulation implicitly.
- Earlier phase gates remain green.

## Phase 15 — Complete transport-manager isolation

### Work

- Put serial, CANalyst-II, and disabled/test adapters behind `ActiveTransportManager`.
- Make adapter lifecycle, capabilities, identity, timestamps, events, and physical TX gate consistent.
- Ensure only configured listeners claim a bus and prevent ambiguous multi-adapter transmit ownership.
- Update `CANALYST-II-SETUP.md` only if verified commands or behavior change.

### Tests and exit gate

- Fake serial and CANalyst adapters pass the same transport contract suite.
- Connect/disconnect/reconnect, bus-off/recovery, timestamp reset, RX overflow, and send failure pass.
- Switching adapters leaves no handlers, timers, leases, or arm state behind.
- Hardware smoke test passes on the controlled CANalyst-II bench when available; absence of hardware does not block software CI.
- Earlier phase gates remain green.

## Phase 16 — Consolidate deterministic simulation and remove MQTT legacy

### Work

- Migrate standalone simulator scenarios to backend models and tests.
- Make models use generated constants, the shared codec, router, and injected simulation clock.
- Define a minimal deterministic plant interface; keep `TrikeViz` presentation-only.
- Document fidelity and omissions per model/profile.
- Remove standalone simulator, Aedes bridge, MQTT dependencies/scripts/topics only after parity.

### Tests and exit gate

- Required simulation, bench-profile, hybrid-observation, monitor, and unit-test workflows pass without MQTT.
- Repeating a scenario produces identical ordered frames/state.
- Model outputs are codec-valid and no model manually duplicates ordinary YAML packing.
- Fresh workspace install/build contains no obsolete MQTT dependencies.
- Earlier phase gates remain green.

## Phase 17 — Implement safe replay

### Work

- Implement recording open, pause, seek, speed, stop, and stable equal-time ordering.
- Make replay downstream-aware: pause on pressure and resume without reordering.
- Preserve original provenance as metadata while current source remains `replay`.
- Keep derived re-recording explicit and off by default.
- Enforce replay isolation from simulation and physical TX.

### Tests and exit gate

- Golden recording replays in exact `(timestampUs, sequence)` order.
- Pause/resume/seek/speed and downstream-pressure cases pass under a fake clock.
- Replay cannot arm or transmit physically even through malformed/API-forged requests.
- Derived recording clearly identifies replay provenance and new session timebase.
- Earlier phase gates remain green.

## Phase 18 — Harden live history, recording, and export

### Work

- Define live-history retention in time and bytes.
- Benchmark the existing SQLite worker/queue before altering its architecture.
- Make recording integrity, overload, recovery, source/event capture, and protocol hash explicit.
- Add ASC export with compatibility fixtures; defer BLF unless a maintained library is selected.
- Bound/index correlation queries and retention maintenance.

### Tests and exit gate

- Live-history memory remains bounded for a 30-minute stress scenario.
- Recording contains expected frames/events in order or is marked incomplete with reason.
- Start/stop/delete/restart recovery, worker failure, disk full, pruning, and active-session retention pass.
- Exported ASC is accepted by the chosen independent reader and round-trips representative frames/events where format permits.
- Earlier phase gates remain green.

## Phase 19 — Bound and simplify the frontend

### Work

- Mount only the active heavy tab; keep cross-tab state in bounded stores.
- Coalesce latest-by-ID telemetry and cap/virtualize monitor history.
- Display mode, physical arm, leases, queue health, event loss, and recording integrity.
- Centralize keyboard/gamepad behavior in a typed `InputController`.
- Separate commanded from measured values; integrate `TrikeViz` math only for presentation.
- Profile before introducing Web Workers; do not require SharedArrayBuffer/Atomics.

### Tests and exit gate

- Inactive heavy tabs have no component timers/render work.
- Thirty-minute UI stress test has bounded heap and responsive controls at the declared workload.
- Slow/disconnected state, arm expiry, lease conflict, queue loss, bus-off, and incomplete recording are visible and actionable.
- Input tests cover repeat, focus, blur, Tab, Escape, ESTOP gesture, disconnect, and teardown.
- Earlier phase gates remain green.

## Phase 20 — Performance qualification, cleanup, and release

### Work

- Define reproducible physical-rate and accelerated-simulation workloads with frame sizes, buses, bitrate/multiplier, clients, and recording state.
- Measure CPU, heap, event-loop delay, all queues, drops/coalescing, UI cadence, and recording integrity.
- Optimize JSON batching/filtering first. Design a versioned binary batch protocol only if agreed targets still fail.
- Run static analysis and remove dead files/exports/dependencies only after reference and workflow checks.
- Add CI gates for generation, checks/builds, unit/integration, software E2E, and performance smoke thresholds.
- Update README, architecture, work plan, hardware setup, and release notes.

### Tests and exit gate

- All phase 1–19 gates pass from a clean checkout.
- Fresh install and documented quick start work.
- Benchmarked targets pass without unbounded memory or silent recording loss.
- If binary transport exists, wire spec, negotiation, golden vectors, malformed-input, and mixed-version tests pass; otherwise JSON remains documented.
- Static-analysis findings are resolved or justified.
- Software CI is green and controlled hardware verification results are recorded separately.

## Deferred extensions

Native firmware-in-the-loop, high-fidelity dynamics, CANalyzer automation, MCP/AI control, RL/gRPC integration, an alternate compiled CLI, BLF without a maintained library, and CAN FD are separate projects. None may bypass the state machine, routing matrix, queue contracts, leases, injection policy, timebase, transport manager, or recording-integrity rules.
