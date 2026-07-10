# Debug Tool Work Plan

**Purpose:** Incrementally bring the existing debug tool to `debug-tool-architecture.md` without a rewrite.

## Non-negotiable phase rule

Phases are sequential. Do not start phase N+1 until all code, tests, documentation, and the exit gate for phase N pass. If a later phase exposes a regression, return to the phase that owns the broken contract, fix it, and rerun every gate from there forward.

Each phase should be a small reviewable change. Preserve unrelated user work. Add regression tests with correctness fixes. Hardware tests remain opt-in and run only on a controlled bench.

## Cleanup rule for every phase

Cleanup is part of implementation, not a final sweep. Every phase must:

1. identify the legacy files, exports, routes, types, configuration, dependencies, tests, generated artifacts, and documentation replaced by that phase;
2. migrate every required consumer before deleting the legacy provider;
3. remove the superseded path in the same phase unless a documented compatibility window is required;
4. search the repository for stale imports, names, environment variables, scripts, and documentation references;
5. run dependency/static analysis relevant to the changed workspaces;
6. prove through tests that the replacement works without the removed path.

Temporary adapters must be named `legacy-*`, have an owner phase and deletion gate, and must not receive new features. Commented-out implementations, duplicate tests, unused screenshots, generated runtime logs, and backup copies are deleted rather than retained in source control.

### Recovered legacy audit candidates

The deleted historical `update.md` named the following cleanup candidates. They are inputs to phase-level static analysis, not proof that deletion is safe:

- `shared/generate.js`, `shared/src/fix.js`;
- `test_record.js`, `test_simulator.js`;
- `ui/debug-browser.mjs`, `ui/screenshot.mjs`;
- `shared/generated/can-catalog.ts`, `shared/generated/can-decode.ts`;
- unused UI simulation API exports such as `simInject`, `simPeriodicStart`, `simPeriodicStop`, and `getSimState` after their callers are migrated;
- redundant direct dependencies on `js-yaml`, `@types/js-yaml`, `mqtt`, and `@testing-library/svelte` in workspaces that no longer import them;
- stale types and exports in `bridge/types.ts`, `ipc-protocol.ts`, and work-mode modules after their replacement contracts land.

Do **not** blindly delete `backend/src/db/worker.ts`: `WorkerClient` actively launches its compiled `worker.js`, so the worker is part of the current non-blocking SQLite boundary. Do **not** blindly delete `e2e/`: its useful scenarios must be migrated into the single maintained Playwright suite in phase 2 before the duplicate location is removed.

### Phase-specific cleanup targets

| Phase | Legacy material removed after replacement passes |
|---:|---|
| 1 | Broken checks, obsolete test commands, stale build scripts, committed runtime/test output |
| 2 | Duplicate Playwright configuration, duplicate E2E cases, obsolete selectors and screenshots |
| 3 | Duplicate bit-extraction helpers and signed-shift workarounds |
| 4 | Silent codec fallbacks, permissive validators, obsolete checksum helpers |
| 5 | Stale generated artifacts and hand-maintained generation scripts superseded by the canonical generator |
| 6 | `ui/src/lib/can-index.ts`, duplicated generic catalogs, unused catalog types |
| 7 | Raw-ID semantic maps, duplicated fault arrays, manual ordinary-message packers |
| 8 | Ambiguous `ts`, `ts_real`, `ts_device` conversions and obsolete timestamp helpers after migration |
| 9 | Mutable legacy frame types, embedded decoded payload fields, fake frame-shaped transport errors |
| 10 | Old `WorkModeConfig` transition behavior, unsafe forwarding flags, stale mode aliases after compatibility migration |
| 11 | Direct producer-to-store/WebSocket paths and source auto-claim behavior that bypass routing rules |
| 12 | Unbounded arrays/queues, unused counters, obsolete retention configuration |
| 13 | Scattered periodic ownership/timer state replaced by the control-session/lease mechanism |
| 14 | Duplicate injection endpoints/policy branches and implicit physical-to-simulation fallback |
| 15 | Bridge proxy globals, duplicated adapter lifecycle code, obsolete transport settings |
| 16 | Standalone simulator workspace, Aedes/MQTT bridge, dependencies, scripts, topics and tests |
| 17 | Ad-hoc timer replay code and replay-specific routing bypasses |
| 18 | Obsolete recording schema/query paths, redundant pruning logic, unsupported export stubs |
| 19 | Hidden mounted heavy components, duplicate input listeners, unbounded UI stores, obsolete styles |
| 20 | Remaining proven dead exports/files/dependencies, benchmark artifacts not intended as fixtures, stale documentation |

For every phase, the exit gate additionally requires:

- no stale reference to anything deleted in that phase;
- no newly orphaned production file or dependency in the affected workspace;
- a clean production build using only the replacement path;
- a short note for anything intentionally retained, including exactly which later phase removes it.

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

## Phase 1 — Restore a Green Software Baseline

### Summary

Implement Phase 1 only, then stop for review. First preserve the current architecture/work-plan
refinements in a docs-only commit. Leave the unrelated generated CAN documentation files
untouched.

### Changes

1. Commit documentation separately
    - Commit only `debug-tool/debug-tool-architecture.md` and `debug-tool/work-plan.md`.
    - Exclude `docs/generated_can_dictionary.md`, `docs/generated_can_documentation.md`, and `shared/can/generate_can_docs.py`.

2. Fix pipeline timestamps
    - In `PipelineView.svelte`, replace both fabricated frame objects with `frameTime({ ts: actualTimestamp })`.
    - Use `chain.trigger.ts` and `step.ts` respectively.
    - Do not change `frameTime`; its seconds/milliseconds normalization is already covered by unit tests.
    - This fixes both TypeScript errors and the behavior bug where timestamps currently use `ts: 0`.

3. Add the canonical root verification command
    - Add root `package.json` scripts under `debug-tool`:
        - `verify:shared`: build shared.
        - `verify:backend`: check, test, then build backend.
        - `verify:ui`: check, test, then build UI.
        - `verify`: run the three workspace verification scripts in dependency order.
    - Use npm workspace commands so the script works on Windows and CI shells.
    - Fail immediately when any command fails.

4. Phase cleanup
    - Confirm build, runtime, test-result, and Playwright-output directories remain ignored and untracked.
    - Do not delete `test_record.js`, `test_simulator.js`, MQTT, E2E, or other later-phase legacy paths during Phase 1.
    - Record the non-fatal `CanMonitor.svelte` Rollup sourcemap annotation warning for Phase 19; do not expand Phase 1 for generated Svelte output.

### Test and Acceptance Gate

Run from `debug-tool`:

1. `npm run verify`
2. `npm run verify` again

Both runs must pass with:

- Shared TypeScript build green.
- Backend typecheck and build green.
- All 201 backend tests green, allowing counts to increase.
- UI `svelte-check` reports zero errors.
- UI production build green.
- All 129 UI tests green, allowing counts to increase.
- Unrelated generated CAN documentation remains unmodified and unstaged.

Commit the Phase 1 implementation separately with a baseline-focused message, then stop before Phase 2.

- The current static-check failure is sufficient regression evidence for the pipeline call-site bug; Phase 2 Playwright coverage will verify rendered pipeline behavior.
- No security/authentication work is included.

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
- Consolidate duplicated backend/UI decoder tests into shared codec tests while retaining UI-specific presentation tests.

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
- Add explicit regression coverage for the `0x169` steering XOR checksum and schema-derived `0x210` safety-state field naming.

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

- Assign backend-generated connection/session owner identities for local UI, REST jobs, tests, and future automation. These are coordination identities, not accounts or authentication.
- Implement atomic acquire, renew, release, expiry, and revocation for steering, motor, brake, and scoped periodic resources.
- Bind leases to operational-state revision and conceal lease secrets from status output.
- Revoke on disconnect, transition, disarm, adapter/interlock failure, and heartbeat expiry.
- Keep Vehicle ESTOP commands and Software Stop independent of conflicting actuator leases.

### Tests and exit gate

- Two owners cannot acquire the same resource concurrently.
- A different connection/session owner or lease ID cannot accidentally renew, command, or release a lease.
- Expiry and every revocation trigger stop affected periodic/actuator output.
- REST and WebSocket disconnect identity behavior is covered.
- Vehicle ESTOP and Software Stop tests succeed while another client owns all actuator leases; neither is labeled as a Physical E-stop.
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
- Retain and test YAML-driven bus detection; remove any remaining hardcoded high/low uniqueness lists.
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
- Audit `tem/improved_trike_kinematic.md`; either integrate the validated display-only calculations with configured dimensions or delete the stale artifact with a documented reason.
- Profile before introducing Web Workers; do not require SharedArrayBuffer/Atomics.

### Tests and exit gate

- Inactive heavy tabs have no component timers/render work.
- Thirty-minute UI stress test has bounded heap and responsive controls at the declared workload.
- Slow/disconnected state, arm expiry, lease conflict, queue loss, bus-off, and incomplete recording are visible and actionable.
- Input tests cover repeat, focus, blur, Tab, Escape, the configured Software Stop or Vehicle ESTOP gesture, disconnect, teardown, and correct labeling distinct from the Physical E-stop.
- Earlier phase gates remain green.

## Phase 20 — Performance qualification and release cleanup

### Work

- Define reproducible physical-rate and accelerated-simulation workloads with frame sizes, buses, bitrate/multiplier, clients, and recording state.
- Measure CPU, heap, event-loop delay, all queues, drops/coalescing, UI cadence, and recording integrity.
- Optimize JSON batching/filtering first. Design a versioned binary batch protocol only if agreed targets still fail.
- Run final repository-wide static analysis and remove only residual dead material not already owned and removed by phases 1–19.
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
