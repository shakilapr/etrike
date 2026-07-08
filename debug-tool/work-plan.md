# Debug Tool Work Plan

Source analysis: `debug-tool-analysis.md`
Bug tracker: `bugs.md`

This plan focuses on making the debug tool responsive, deterministic, and maintainable. The current priority is not adding more UI surface; it is removing the architectural causes of lag, conflicting frame sources, and CAN catalog drift.

---

## Current Diagnosis

The tool has good foundations: dual-bus transport, `FrameRouter`, backend simulation models, WebSocket batching, Playwright E2E, and generated `ui/src/lib/can-index.ts`.

The remaining problems are structural:

1. The backend still performs synchronous SQLite work on the Fastify event loop.
2. The old browser-side emulator can generate frames that conflict with backend simulation.
3. The frontend keeps heavy tabs mounted and updates hidden DOM during telemetry bursts.
4. CAN definitions are not fully centralized because backend and UI still keep hand-coded catalogs and decoder switches.
5. Input handling is spread across `App.svelte` and `Controller.svelte`, which makes keyboard behavior hard to reason about.

---

## Phase 0 - Stabilize and Measure

Goal: prove the UI compiles, interaction paths work, and performance measurements are repeatable.

Tasks:

1. Keep `debug-tool/e2e/tests/interaction-audit.spec.ts` as a required regression test for:
   - mode selector POSTing `/api/mode`
   - Controller `Start`
   - `W`, `A`, `B`, `Tab`, and `Esc` keyboard commands
2. Add a performance audit Playwright test that records:
   - initial page interactive time
   - tab switch latency
   - keyboard command visible-state latency
   - console errors and page errors
3. Add one command for developers to run all non-hardware checks:
   - backend `npm run check && npm test`
   - UI `npm run check && npm test`
   - E2E `npm test`
   - CAN generator `python shared/can/generate_can_index.py --check`

Acceptance:

- UI `svelte-check` passes.
- Full non-hardware Playwright suite passes.
- Interaction audit has no timeouts and no console/page errors.

---

## Phase 1 - Stop Frontend Jank

Goal: prevent hidden UI and frame-buffer churn from blocking controls.

Tasks:

1. Replace hidden-tab rendering in `App.svelte` with conditional rendering for heavy views:
   - mount only the active tab for `CanMonitor`, `Dashboard`, `Stats`, `PipelineView`, `Emulator`, and `Controller`
   - keep state that must survive tab changes in stores
2. Preserve required tab state in stores:
   - monitor filters
   - injector selected bus and selected ID
   - terminal log
   - emulator/work-mode form state
3. Replace frame array spread/slice hot paths in `ui/src/stores/can.ts` with a bounded append strategy that minimizes allocations.
4. Make all frame-consuming components subscribe only to data they actually need.

Acceptance:

- Tab switch Playwright checks complete under 1 second on the dev machine.
- Controller keyboard audit passes while CAN frames are streaming.
- No hidden tab does expensive DOM rendering when inactive.

---

## Phase 2 - Remove Conflicting Emulation Paths

Goal: one owner decides which ECU emits each frame.

Tasks:

1. Convert `Emulator.svelte` from a frame generator into a backend work-mode/configuration UI.
2. Remove browser `setInterval` CAN generation from `Emulator.svelte`.
3. Route all simulated/emulated frames through backend `SimulationEngine` and `FrameRouter`.
4. Expose explicit UI controls for:
   - work mode
   - simulated ECUs
   - bypass flags
   - inject-emulated-to-physical toggle
5. Make conflicting sources impossible:
   - one `(bus, id)` may have only one active source unless explicitly overridden
   - UI should show conflicts instead of sending duplicate frames

Acceptance:

- Full Sim mode starts all selected ECU models from the backend only.
- Emulator mode does not send frames from browser timers.
- Hybrid mode can show which IDs are physical, emulated, or simulated.

---

## Phase 3 - Move SQLite off the Hot Path

Goal: CAN ingestion and WebSocket response must not block on synchronous database work.

Tasks:

1. Add a write queue for frame persistence.
2. Move heavy SQLite operations to either:
   - a worker thread using `worker_threads`, or
   - an async database layer with equivalent isolation
3. Batch recording attachments instead of inserting one recording link per frame synchronously.
4. Add a `latest_frames` table keyed by `(bus, can_id)`:
   - update on insert with `ON CONFLICT(bus, can_id) DO UPDATE`
   - serve `/api/can/latest` from this table
5. Keep maintenance pruning periodic and bounded:
   - delete in chunks
   - never run unbounded `DELETE` loops in the request/ingest path

Acceptance:

- At high simulated frame rate, REST `/api/status` still responds promptly.
- WebSocket clients keep receiving batches while maintenance runs.
- Recordings still preserve referenced frames.

---

## Phase 4 - Make YAML the CAN Source of Truth

Goal: eliminate hand-maintained CAN catalog conflicts.

Authoritative files:

- `shared/can/can_high.yaml`
- `shared/can/can_low.yaml`

Generated files:

- `debug-tool/ui/src/lib/can-index.ts`
- future backend/shared generated index

Rules:

1. Do not hand-edit generated CAN index files.
2. Any protocol change starts in `can_high.yaml` or `can_low.yaml`.
3. Run `python shared/can/generate_can_index.py` after YAML edits.
4. CI must run `python shared/can/generate_can_index.py --check`.
5. Backend and UI must import the same generated schema or generated package.

Tasks:

1. Extend `shared/can/generate_can_index.py` or add a sibling generator so backend can consume the same generated catalog.
2. Replace backend `CAN_MESSAGES` with generated definitions.
3. Replace UI hand-coded `CAN_MESSAGES` in `can-decoder.ts` with generated definitions from `can-index.ts`.
4. Replace monolithic manual decoder/encoder switches with generated or data-driven encode/decode helpers.
5. Update simulator/test profiles to derive DLC, signal layout, and IDs from YAML-generated definitions.

Acceptance:

- Backend, UI, simulator, and tests agree on ID, bus, DLC, byte order, and signals.
- `--check` fails if generated output is stale.
- No bug can be caused by editing only one of backend/UI CAN catalogs.

---

## Phase 5 - Clean Backend Architecture

Goal: make routes depend on explicit services instead of mutable `app as any` globals.

Tasks:

1. Introduce an `AppServices` object containing:
   - store
   - stream hub
   - active bridge manager
   - frame router
   - simulation engine
   - timers
2. Pass services into route registration functions explicitly, or use typed Fastify decorators.
3. Replace bridge `Proxy` hot-swap with an `ActiveBridgeManager`.
4. Keep all transport switching in one service.

Acceptance:

- No route needs `(app as any).__...`.
- Transport switching can be tested without booting the full server.
- Shutdown owns and closes all timers/processes deterministically.

---

## Phase 6 - Unify Keyboard and Controller Input

Goal: keyboard commands should behave predictably and be testable outside Svelte components.

Tasks:

1. Extract keyboard state handling from `App.svelte` into an `InputController` store/service.
2. Define typed actions:
   - drive forward/reverse
   - steer left/right
   - brake
   - zero all
   - bus toggle
   - ESTOP confirm/send
3. Let `Controller.svelte` consume typed input state rather than raw key strings.
4. Add unit tests for repeated keydown, blur clearing, Tab bus toggle, and Space double-press ESTOP.
5. Keep Playwright interaction audit as the end-to-end check.

Acceptance:

- Holding `W`, `A`, `B` updates visible command state and emitted command values.
- Repeated keydown does not cause runaway updates.
- Losing browser focus clears held input.

---

## Phase 7 - Feature Gap: Feedback and Odometer

Goal: controller and dashboard show commanded values and actual feedback.

Tasks:

1. Subscribe `Controller.svelte` to `telemetry`.
2. Display actual steering angle and brake pressure beside command targets.
3. Add `odometer_m` or `distance_m` to backend simulation state.
4. Integrate distance from speed over simulation ticks.
5. Expose odometer through `/api/sim/state` and display it in Controller/Dashboard.

Acceptance:

- Operator can compare commanded speed/steer/brake with actual feedback.
- Full Sim mode shows distance moved.

---

## Verification Matrix

Run these before considering the plan complete:

```powershell
cd debug-tool\backend
npm run check
npm test

cd ..\ui
npm run check
npm test

cd ..\e2e
npm test

cd ..\..
python shared\can\generate_can_index.py --check
```

Hardware-only checks:

- MCP2515 high-bus Playwright tests require hardware and may be skipped locally.
- Native CAN transceiver tests require original RT/SYS/MTR build environment and should be run before hardware release builds.

---

## Execution Order

1. Phase 0: stabilize and measure.
2. Phase 1: fix frontend jank.
3. Phase 2: remove browser frame generation.
4. Phase 4: make YAML the shared CAN source of truth.
5. Phase 3: move SQLite off hot path.
6. Phase 5: backend service cleanup.
7. Phase 6: keyboard/controller input cleanup.
8. Phase 7: telemetry feedback and odometer.

Phase 4 can run in parallel with Phase 1/2 because YAML generation is mostly isolated.
