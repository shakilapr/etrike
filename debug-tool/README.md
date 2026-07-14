# E-Trike Debug Tool

Browser-based tooling for monitoring, decoding, recording, replaying, simulating, and deliberately injecting E-Trike CAN traffic.

## Documentation

- [Architecture](debug-tool-architecture.md) — maintained current/target design and safety boundaries
- [Work plan](work-plan.md) — dependency-ordered implementation phases and acceptance gates
- [CANalyst-II setup](CANALYST-II-SETUP.md) — Windows driver, dependency, and hardware setup

The CAN protocol authority is outside this directory:

- `../protocol/contracts/can_high.yaml`
- `../protocol/contracts/can_low.yaml`

## Components

- `backend/` — Fastify API, WebSocket stream, transports, simulation, SQLite worker, recordings
- `ui/` — Svelte/Vite user interface
- `shared/` — shared CAN catalog, codec, and types
- `debug-esp32/` — optional ESP32 debug bridge firmware
- `e2e/` and `ui/tests/e2e/` — currently overlapping Playwright suites; consolidation is tracked in the work plan
- `simulator/` — legacy MQTT simulator pending migration to the backend simulation engine

Legacy components are identified honestly here because they still exist. Do not build new workflows on the MQTT simulator.

## Quick start

Install dependencies from this directory if they are not already present:

```powershell
npm install
```

Start the backend:

```powershell
cd backend
npm run dev
```

In a second terminal, start the UI:

```powershell
cd ui
npm run dev
```

Open `http://127.0.0.1:5173`. The backend defaults to `http://127.0.0.1:3000` unless local configuration overrides it.

For CANalyst-II use, complete [CANALYST-II-SETUP.md](CANALYST-II-SETUP.md) before starting the backend with the adapter transport enabled.

## Verification

Until the planned root verification command is added, run each workspace explicitly.

Shared:

```powershell
cd shared
npm run build
```

Backend:

```powershell
cd backend
npm run check
npm test
npm run build
```

UI:

```powershell
cd ui
npm run check
npm test
npm run build
```

At the 2026-07-10 documentation baseline, backend and UI unit tests pass, but UI checking has two known errors in `PipelineView.svelte`. Phase 0 of the work plan requires a fully green baseline before architectural refactoring.

Hardware tests are opt-in. Never run injection or HIL tests against a vehicle or powered bench without the required physical safety controls.

## Operating boundaries

- The backend is the authority for work mode, routing, simulation, injection policy, and recording.
- YAML range validation validates protocol values; it is not a substitute for firmware safety logic or a physical interlock.
- Physical transmission must remain disabled unless an operator deliberately arms it for a controlled bench setup.
- The debug tool is engineering support software, not a certified vehicle safety mechanism.
