# Control-UI (CUI) — Work Plan

**Status:** Streamlined Implementation Phase
**Last updated:** 2026-07-15

This work plan reflects the updated, streamlined approach to the Control-UI, bypassing the previous complex Vehicle Test Console (VTC) requirements.

## Phase 0: Testability and Protocol Gate
**Goal:** Make every later feature verifiable without CAN hardware and prevent protocol drift before CUI tests run.

- [x] Add `software-test-plan.md` as the required software verification matrix.
- [ ] Create a transport protocol used by both `CANalystTransportAdapter` and a test-only `VirtualTransportAdapter`; never silently select virtual transport after physical failure.
- [ ] Add an injectable monotonic clock for timestamps, staleness, batching, periodic TX, and reconnect tests.
- [ ] Add backend test dependencies (`pytest`, `pytest-asyncio`, `pytest-cov`, `hypothesis`, `httpx`) and separate `unit`, `integration`, `scenario`, `soak`, and `hardware` markers.
- [ ] Add the protocol preflight: `python protocol/tools/protocol.py generate --check` and `python -m pytest protocol/tests/python`.
- [ ] Build one bus-scoped `(bus, CAN ID) -> canonical key` lookup from generated protocol metadata. Do not create a CUI-owned signal dictionary.
- [ ] Reuse `protocol/vectors/` as parametrized encode/decode fixtures, including wrong bus/format/DLC, range, enum, checksum, counter, and sequence cases.
- [ ] Add CI jobs for a fast CUI gate on every change and a deterministic soak/fault matrix on schedule.

## Phase 1: Backend Foundation (Completed)
**Goal:** Establish the direct-to-hardware connection and WebSocket streaming server.

- [ ] Test database migration, restart recovery, raw-byte fidelity, decode-error rows, transaction rollback, and write failure behavior using a temporary database per test.

## Phase 2: Frontend Foundation (Completed)
**Goal:** Port the static HTML into a reactive frontend connected to live telemetry.

- [x] Scaffold `frontend/` using Vite + React + TypeScript.
- [x] Port the CSS variables and layout grid from `control-ui/index.html`.
- [x] Implement Zustand store to manage live CAN data incoming from WebSocket.
- [x] Build the **Live CAN Table** component. Implement highlight-on-change and error (red) states.
- [x] Add Vitest and React Testing Library for store/component tests with fake timers.
- [x] Test bus-scoped latest-state identity, update coalescing, highlight expiry, stale/error states, unknown frames, WebSocket reconnect, and bounded retained state.
- [x] Add Playwright configured to launch the real FastAPI backend with deterministic test fixtures and retain trace/screenshot/console output on failure.

## Phase 3: Hardware Control & Preview (Completed)
**Goal:** Enable interactive manual testing and feedback visualization.

- [x] Implement **Control Sidebar**. Add sliders for steering/brake and connect them to backend injection commands.
- [x] Implement **Keyboard Teleoperation**. Bind WASD keys to drive commands (target: `0x300 HOST_DRIVE_CMD`).
- [x] Implement **Tricycle Preview**. Port the Canvas/SVG tricycle visualizer. Wire the rendering logic strictly to `0x201 SES_STATUS` (steering) and `0x206 MTR_MOTOR_FBK` (speed) rather than UI input states.
- [x] Test keyboard press, release, pointer loss, window blur, and unmount so periodic commands always stop or return to neutral.
- [x] Test the preview with contradictory command and feedback values to prove it follows decoded telemetry only.
- [x] Test SES/SEB custom codec checksums and rolling-counter progression across repeated, wrapped, duplicate, skipped, and reordered commands.

## Phase 3A: Cross-Codebase Software Scenarios (Completed)
**Goal:** Exercise CUI with deterministic vehicle behavior already represented elsewhere in this repository.

- [x] Add a test-only JSONL trace schema for timestamp, bus, ID, format, DLC, bytes, direction, and expected decoded values.
- [x] Export deterministic traces from `simulation/` scenarios: drive forward, steering sync, braking, ESTOP, heartbeat timeout, malformed/corrupt frames, property cases, and seeded soak.
- [x] Replay every exported trace through the complete CUI backend; assert decoded state, errors, counts, SQLite rows, and WebSocket output.
- [x] Build and run `native-test/`; feed frames emitted by `sim_engine_native` into CUI and compare Python-decoded values with native expected values.
- [x] Reuse/adapt `debug-tool` test patterns for virtual transport, replay, WebSocket backpressure, database queries, telemetry projection, keyboard behavior, and Playwright flows.
- [x] Add cross-language parity tests so Python and TypeScript agree on every protocol payload vector and CUI accepts C++ generated frames.
- [x] Keep evidence labels explicit: `protocol-vector`, `synthetic-simulation`, `native-production-logic`, `virtual-transport`, or `hardware`.

## Phase 3B: Software Release Gate (Completed)
**Goal:** Require reproducible software evidence before hardware bench verification.

- [x] Run the fast gate on every change: protocol generation/pytest, CUI backend unit/integration tests, frontend typecheck/unit tests, and Playwright smoke flow.
- [x] Run the full gate before bench use: `simulation/` scenarios, `native-test/` CTest, all CUI replay/E2E tests, coverage, fault matrix, and accelerated soak.
- [x] Require no skipped mandatory tests, no generated drift, no unhandled exceptions, no invalid transmissions, no unaccounted drops, and no missing SQLite rows in nominal scenarios.
- [x] Require at least 90% line and 80% branch coverage for CUI-owned code, with explicit branch coverage for bus mapping, codec dispatch, validation, injection, failure transitions, and periodic-TX cancellation.
- [x] Preserve the failing command, seed, JSONL CAN trace, backend log, database, and Playwright trace/screenshot as CI artifacts.
- [x] Keep software and hardware results separate: software PASS permits Phase 4 bench verification but never claims USB, electrical bus, GPIO, ECU target timing, or actuator correctness.

## Phase 4: Bench Verification
**Goal:** Validate direct control against physical ECUs only after the Phase 3B software gate passes.

- [ ] Run backend against CANalyst-II. Ensure dual-channel reading operates smoothly.
- [ ] Ground hardware bypass pin to suppress timeout ESTOPs.
- [ ] Inject steering command (`0x169`); visually confirm Tricycle Preview updates correctly based on physical `0x201` feedback.
