# Debug Tool Architecture Analysis & Critique

This document provides an in-depth analysis of the `debug-tool` architecture (v0.4.0-alpha). While the platform is transitioning to a robust multi-mode bench-test environment, it currently suffers from severe architectural bottlenecks, conflicting state paradigms, and technical debt that critically impact performance and maintainability.

---

## 1. Node.js Event Loop Blocking & Database Bottlenecks

The backend relies on `better-sqlite3` (a synchronous SQLite driver) to persist CAN frames for the `/api/can/frames` pipeline and recordings. This introduces critical event loop blocking:

- **Synchronous Pruning (BUG-19):** At high frame rates (e.g., dual CAN buses under load can exceed 2000 FPS), synchronous database operations block the single Node.js thread. While the architecture doc notes `MAX_FRAMES` pruning as a bottleneck, the `runMaintenance()` loop runs every 5 seconds and synchronously executes `DELETE` statements on thousands of rows. During this time, the backend cannot accept WebSocket connections, process REST requests, or ingest new frames.
- **Recording Overhead:** The `attachToActiveRecordings` function runs on *every single frame insert*. It loops through active recording IDs and runs `INSERT OR IGNORE` synchronously. This couples the high-frequency ingestion path directly to I/O latency.
- **Latest Frame Querying:** The `latestById()` function runs a `SELECT * FROM can_frames ORDER BY ts_real DESC LIMIT 5000` query. This is a very inefficient way to find the most recent frame per ID.

**Recommendation:** Move SQLite operations to a worker thread (using `worker_threads`) or use an asynchronous SQLite driver to prevent blocking the Fastify event loop. Introduce a dedicated `latest_frames` table with `ON CONFLICT(bus, can_id) DO UPDATE` to track the latest state efficiently.

---

## 2. Conflicting Emulation Engines (The "Modes" Mess)

As identified previously, the architecture is torn between two paradigms:

- **The Old Frontend Emulator:** `ui/src/components/Emulator.svelte` runs naive `setInterval` loops directly in the browser. It tracks its own `$softwareSimEnabled` state and generates mock CAN payloads manually (e.g., `simSpeed`, `simGear`).
- **The New Backend Engine:** `backend/src/sim/engine.ts` is a proper orchestrator ticking at 100Hz with true ECU behavioral models (e.g., Tricycle kinematics in `sim/physics`). It relies on a highly configurable `WorkModeConfig`.

**The Issue:** The UI dropdown in `Topbar.svelte` simply posts hardcoded `MODE_DEFAULTS` to the backend. It offers no way to actually configure the backend's `simulatedEcus` or `bypasses`. If a user selects "Full Sim" and also clicks "Emulate Missing" in the frontend, the browser and the backend will both spam the bus with conflicting frames.

**Recommendation:** Delete the client-side frame generation in `Emulator.svelte`. Repurpose it as a pure configuration UI that manages the backend's `WorkModeConfig` via `/api/mode`. 

---

## 3. Frontend Reactivity & Performance Sinkholes

The Svelte 5 frontend is currently built in a way that maximizes CPU overhead during high-frequency telemetry streaming:

- **Array Slicing Reactivity:** In `ui/src/stores/can.ts`, the frame buffer is updated using `[...current, frame].slice(-1000)`. Doing a full array spread and slice on every incoming message at 1000 FPS is incredibly expensive and triggers massive garbage collection spikes. 
- **Tab Rendering (Hidden vs. Unmounted):** According to `root-issues.md`, all 10 tabs are permanently rendered in the DOM and toggled using `display: none` rather than Svelte's `{#if}` blocks. This means when a frame arrives, the DOM is still silently updating the `CanMonitor` table even if the user is looking at the `Dashboard`. This causes severe UI jank.

**Recommendation:** 
1. Replace the `[...current]` array spread in the store with a pre-allocated Ring Buffer (circular array).
2. Use Svelte `{#key}` or proper conditional rendering `{#if}` for tabs. If state must be preserved across tabs (like the terminal history), extract that state to a global store, but unmount the heavy DOM elements when they are not visible.

---

## 4. Technical Debt: The CAN Catalog Duplication

The architecture document admits a major sync warning: the CAN message catalog (`can.ts` in the backend and `can-decoder.ts` in the frontend) is hand-maintained and duplicated.

- **Divergence from Firmware:** The true source of truth is `shared/can/can_low.yaml` and `can_high.yaml`. By manually maintaining the TypeScript types, the debug tool will inevitably decode frames incorrectly when the firmware changes. 
- **Code Duplication:** The exact same decoding logic exists in both the UI and the Backend. 

**Recommendation:** Execute the planned "Phase 0" fix immediately: write a script (`shared/can/generate_can_index.py`) that reads the YAML and generates a single `can-index.ts` file in a shared workspace package (`@etrike/debug-shared`) that both the frontend and backend can import.

---

## 5. Spaghetti Code Anti-Patterns

A review of the codebase reveals several instances of "spaghetti code" that make debugging and extending the tool highly problematic:

- **Backend Fastify Object Mutation:** In `backend/src/index.ts`, global state singletons are force-injected into the Fastify app object by casting it to `any` (e.g., `(app as any).__simEngine = simEngine;`). Furthermore, the routing logic relies on a JavaScript `Proxy` object (`bridgeProxy`) to intercept and hot-swap active hardware bridges mid-flight. This tightly couples the HTTP routes to mutable global state in an unpredictable manner.
- **Frontend Global Event Listeners:** In `ui/src/App.svelte`, the UI binds global `window.addEventListener("keydown", ...)` listeners that directly mutate a `heldKeys` set using hardcoded strings (`["w","s","a","d","b"].includes(k)`). This input is then polled by a 50Hz `requestAnimationFrame` game loop buried inside `Controller.svelte`. The UI input layer is completely tangled between multiple unrelated files.
- **Monolithic Decoder Switches:** In `ui/src/lib/can-decoder.ts` and `backend/src/types/can.ts`, the `decodeFrame` and `encodePayload` functions are massive 600+ line switch statements that perform raw, manual bitwise shifting inline (e.g., `((bytes[6] ?? 0) >> 4) & 0x0f`). This is extremely brittle and unmaintainable compared to a data-driven parser generated from the CAN schema.

**Recommendation:** 
1. Use a proper Dependency Injection (DI) pattern or Fastify decorators instead of mutating the `app` instance with `any`.
2. Extract the keyboard polling loop into a unified `InputController` service.
3. Replace the monolithic switch statements with a declarative schema parser built from the CAN YAML files.

---

## 6. Summary

The v0.4.0-alpha architecture has excellent conceptual foundations (the FrameRouter, the virtual bus, the IPC adapter for native C++ models). However, its execution is hindered by legacy code (`Emulator.svelte`), poorly optimized data structures (synchronous SQLite, Array spreading), spaghetti code (global mutations and monolithic bitwise switches), and a lack of shared tooling (the hand-coded CAN catalog). Addressing these areas will transform the tool from a fragile prototype into a stable bench-test platform.
