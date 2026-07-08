# Debug Tool - Active Bugs and Gaps

> Severity: P0 = blocks reliable operation, P1 = wrong behavior or severe maintainability risk, P2 = lower-risk cleanup/usability.
> Source: `debug-tool-analysis.md`
> Fix plan: `work-plan.md`

---

## P0 - Critical

### ARCH-01: UI can become unresponsive under telemetry load

**Files:** `ui/src/App.svelte`, `ui/src/stores/can.ts`, frame-consuming UI components

**Symptom:** The UI feels slow or frozen, tab switches lag, and keyboard commands can appear delayed when frames are streaming.

**Root cause:** Heavy tabs remain mounted while hidden, and frame updates still propagate through inactive DOM. The frame store also uses array spread/slice patterns that allocate heavily under high frame rates.

**Fix direction:** Follow `work-plan.md` Phase 1. Conditionally mount heavy tabs, move persistent tab state into stores, and replace hot-path array churn with a bounded low-allocation frame buffer.

### ARCH-02: Browser emulator can conflict with backend simulation

**Files:** `ui/src/components/Emulator.svelte`, `backend/src/sim/engine.ts`, `backend/src/sim/router.ts`

**Symptom:** Selecting a work mode and also using the Emulator UI can produce duplicate or conflicting CAN frames for the same `(bus, id)`.

**Root cause:** `Emulator.svelte` still owns browser-side frame generation while the backend also owns simulation/work-mode routing.

**Fix direction:** Follow `work-plan.md` Phase 2. Remove client-side frame timers, convert Emulator into a backend work-mode/config UI, and route all simulated/emulated frames through `SimulationEngine` and `FrameRouter`.

### ARCH-03: Synchronous SQLite can block the backend event loop

**Files:** `backend/src/db/queries.ts`, `backend/src/api/can.ts`, recording code

**Symptom:** Under high frame rate or maintenance/pruning load, REST/WebSocket responses can stall because SQLite work runs synchronously in the Node.js event loop.

**Root cause:** `better-sqlite3` operations, maintenance deletes, recording attachment writes, and latest-frame queries happen synchronously in backend runtime paths.

**Fix direction:** Follow `work-plan.md` Phase 3. Move database writes/heavy maintenance off the event loop, batch recording links, chunk pruning, and introduce `latest_frames`.

---

## P1 - Wrong Behavior / Maintainability Risks

### ARCH-04: CAN catalog is not fully single-source-of-truth

**Files:** `shared/can/can_high.yaml`, `shared/can/can_low.yaml`, `shared/can/generate_can_index.py`, `backend/src/types/can.ts`, `ui/src/lib/can-decoder.ts`, `ui/src/lib/can-index.ts`

**Symptom:** Backend, UI, simulator, and firmware can drift on bus, ID, DLC, field layout, or decoder behavior.

**Root cause:** The YAML files are the real source of truth, and `ui/src/lib/can-index.ts` is generated, but backend/UI still contain hand-maintained catalogs and monolithic decoder/encoder switches.

**Fix direction:** Follow `work-plan.md` Phase 4. Treat only `shared/can/can_high.yaml` and `shared/can/can_low.yaml` as authoritative. Generate shared definitions for both backend and UI, run `python shared/can/generate_can_index.py --check` in CI, and delete hand-maintained catalog duplication.

### ARCH-05: Backend routes depend on mutable globals

**Files:** `backend/src/index.ts`, backend route modules

**Symptom:** Route behavior depends on mutable state attached to Fastify with `(app as any).__...`, and bridge hot-swap is hidden behind a JavaScript `Proxy`.

**Root cause:** Services are not modeled explicitly; runtime state is injected into the app object and accessed dynamically.

**Fix direction:** Follow `work-plan.md` Phase 5. Introduce typed service objects or Fastify decorators and replace bridge proxying with an `ActiveBridgeManager`.

### ARCH-06: Keyboard/controller input is split across unrelated files

**Files:** `ui/src/App.svelte`, `ui/src/components/Controller.svelte`, `ui/src/stores/keyboard.ts`

**Symptom:** Keyboard commands are hard to reason about and have previously failed or lagged because raw browser events, held-key state, and controller polling are split between global listeners and component loops.

**Root cause:** Input handling is not a cohesive typed service; commands are raw strings and multiple components share responsibility.

**Fix direction:** Follow `work-plan.md` Phase 6. Extract an `InputController` store/service with typed actions and unit tests for repeated keydown, blur clearing, Tab, Escape, and Space double-press ESTOP.

---

## P2 - Feature Gaps / Usability

### GAP-01: Controller UI lacks actual feedback and odometer

**Files:** `ui/src/components/Controller.svelte`, `backend/src/sim/engine.ts`, `ui/src/stores/telemetry.ts`

**Symptom:** The Controller UI shows commanded target speed, steering, and brake, but not actual feedback from SES/SEB/MTR. It also does not show distance moved.

**Root cause:** Controller renders its own command variables instead of telemetry feedback, and the simulation state does not expose integrated distance.

**Fix direction:** Follow `work-plan.md` Phase 7. Display actual feedback beside command targets, add `odometer_m` or `distance_m` to simulation state, and expose it via `/api/sim/state`.

### GAP-02: Improved Trike Kinematic Model is saved but not integrated

**Files:** `c:\projects\etrike\tem\improved_trike_kinematic.md`, `ui/src/components/TrikeViz.svelte`

**Symptom:** The improved model exists as a saved artifact but is not fully integrated into the debug UI.

**Fix direction:** Review the artifact and either integrate it into `TrikeViz.svelte` or archive it as superseded.

---

## Required Regression Tests

Before closing any architecture item:

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

Hardware-only tests remain separate and require the native CAN/transceiver setup.
