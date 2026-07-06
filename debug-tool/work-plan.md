# Debug Tool — Work Plan (v0.4.0-alpha)

> Separated from `debug-tool-architecture.md` per 2026-07-06.
> This document tracks **what needs building**. For architecture decisions, see `debug-tool-architecture.md`.
> For known bugs, see `bugs.md`.

---

## 1. Starting Point

### What exists today

| Layer | Status | Notes |
|-------|--------|-------|
| Transport bridges | ✅ 4 modes (Serial, CANalyst-II, MQTT, Disabled) | Auto-detection is fragile (3s block, no hot-swap) |
| CAN protocol | ✅ 37 messages decoded | Two hand-maintained copies (backend + UI), no YAML→TS generator |
| Database | ✅ SQLite, 50000 frame cap | Stats stored with no TTL (BUG-01) |
| WebSocket | ✅ Fan-out with per-client bus/ID filters | Filter race on reconnect (BUG-08) |
| UI — Monitor | ✅ Live frame stream with category grouping | Tab switch destroys state (BUG-04) |
| UI — Emulator | ⚠️ Static data, no feedback loop | No behavioral model — sends identical data every tick |
| UI — Controller | ✅ WASD keyboard driving at 50 Hz | Works for both high and low bus |
| UI — Injector | ✅ Manual + periodic injection with templates | ESTOP guard, DLC validation |
| UI — Pipeline | ⚠️ Polls REST every 2s, 200-frame correlation window | Correlation is approximate |
| UI — Dashboard | ✅ Telemetry from latest frames, health bar, ECU presence | Presence detection is stale (BUG-02) |
| UI — Terminal | ✅ Error log viewer with copy | No command input, read-only |
| Simulator | ⚠️ Standalone MQTT publisher, 38 CAN IDs | Wrong DLCs (BUG-12), sine-wave only, no behavioral model |
| Simulation models | ⚠️ TypeScript ECU models in `simulation/src/ecus/`, 355 tests | Vitest-only, not wired to debug tool |
| Native C++ tests | ⚠️ 14 test exes (CMake + g++), 7 passing | Compiles firmware source directly; 7 stale (DLL issue) |
| E2E tests | ⚠️ 2 Playwright specs | Needs backend running, partial coverage |

### Critical bugs blocking progress

See `bugs.md` for full details. These must be fixed before or during Phase 1:

| Bug | Impact on work plan |
|-----|-------------------|
| BUG-01 (stale stats/FPS) | Blocks any work that relies on bus health indication |
| BUG-02 (stale ECU presence) | Blocks Hybrid mode — can't trust presence detection |
| BUG-04 (tab data loss) | Blocks Emulator usability — can't keep emulation running while checking Monitor |
| BUG-06 (3s startup block) | Slows every dev cycle |
| BUG-12 (simulator wrong DLCs) | Blocks Full Simulation — frames would fail ECU validation |

---

## 2. Key Architectural Decision: Two-Track Model Strategy

The six ECUs split into two categories requiring different approaches:

### Our ECUs (RT, SYS, MTR): Native C++ Compilation

The firmware source code IS available for all three. The logic modules are host-compilable via HAL shadow layers (ESP-IDF stubs for RT/SYS, STM32 HAL stubs for MTR). Models can be compiled to `.dll`/`.so`/WASM and loaded by the Node.js backend. This gives **bit-identical behavior** to the real firmware.

| ECU | Platform | Source | Native tests |
|-----|----------|--------|-------------|
| RT | ESP32-S3 | `rt-esp32/src/` | 7 exes passing (physics, safety, dispatch, router, heartbeat, watchdog) |
| SYS | ESP32-S3 | `sys-esp32/src/` | 2 exes passing (safety monitor, mode manager) |
| MTR | STM32F103C8 | `mtr-stm32/src/` | 2 exes passing (throttle math, gear control + conflict detection) |

CAN protocol for all three is defined in `shared/can/can_low.yaml` + `shared/can/can_high.yaml`.

### Third-Party ECUs (EPS-C, SEB): CAN-Level Behavioral Models

We do NOT have source code for purchased/vendor ECUs and never will. The **manufacturer CSV documents** plus YAML protocol definitions are the specification. Models implement the documented CAN behavior — what frames the ECU sends, at what rates, in response to which commands.

The TypeScript models in `simulation/src/ecus/epsc.ts` and `seb.ts` are the path here.

---

## 3. Phase 0 — Foundation Fixes

**Goal:** Fix bugs that block all further work. No new features.

**Duration estimate:** 2-3 days

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 0.1 | Fix BUG-04: Replace `{#if}` tab switching with CSS display toggle | — | Small |
| 0.2 | Fix BUG-01: Add stats staleness — `getStats()` returns zeros if >5s stale | — | Small |
| 0.3 | Fix BUG-02: Add time-based staleness to `ecuPresence` — ignore frames >3s old | — | Small |
| 0.4 | Fix BUG-06: Start `app.listen()` before transport auto-detection | — | Small |
| 0.5 | Fix BUG-12: Correct DLC values in `simulator/src/can-generator.ts` to match YAML | — | Small |
| 0.6 | Fix BUG-03: BusDetector — add 10s staleness timeout, reset on silence | — | Small |
| 0.7 | Fix BUG-08: Send WS filter before `onState(true)` callback | — | Trivial |
| 0.8 | Fix BUG-09: Add `stats_updated_at` to `/api/status` response | — | Trivial |
| 0.9 | Fix BUG-11: Add `/api/system/switch-transport` endpoint with auto-detection | — | Medium |
| 0.10 | Add `dotenv` for `.env` loading (BUG-17) | — | Trivial |
| 0.11 | Fix BUG-19: Batch database pruning or move to periodic timer instead of per-insert | — | Medium |
| 0.12 | Fix BUG-20: Add manual retention limit for stopped recordings to prevent WAL memory leak | — | Medium |

**Verification:** All 12 fixes pass existing tests. E2E tests pass. Manual check: unplug CAN bus → Topbar shows "silent" within 5 seconds.

---

## 4. Phase 1 — Unified Frame Pipeline & Transport Layer

**Goal:** One frame pipeline. Auto-detecting, hot-swappable transports. Software-only injection with dynamic data.

**Duration estimate:** 1-2 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 1.1 | Build `FrameRouter` class — per-(bus, id) source table, collision detection, duplicate suppression | 0.1 | Medium |
| 1.2 | Route all bridge frames through FrameRouter instead of directly to Store+Hub | 1.1 | Medium |
| 1.3 | Add `injectToPhysical` flag to FrameRouter — when set, emulated frames go to bridge as injection | 1.1 | Small |
| 1.4 | Transport auto-detection rewrite: non-blocking probe → ranked preference → `disabled` fallback | 0.4, 0.9 | Medium |
| 1.5 | Transport hot-swap: `/api/system/switch-transport` re-runs detection, swaps bridge at runtime | 0.9 | Medium |
| 1.6 | Dynamic periodic injection — support `generator` callback or server-side counter state | — | Medium |
| 1.7 | Add `lastFrameAt` timestamp to all bridges; broadcast degraded status after 3s silence | 0.2 | Small |
| 1.8 | Per-ID frame rate computed from DB query (last N seconds) instead of bridge-asserted stats | 0.2 | Small |

**Verification:** All 4 transport modes start without blocking. Switching transports at runtime works. Stats reflect actual frame arrival, not last bridge message.

---

## 5. Phase 2a — Our ECU Models (RT, SYS, MTR): Native C++ via IPC

**Goal:** Compile RT, SYS, and MTR firmware logic to a standalone executable. Communicate via stdin/stdout JSON-Lines — the same IPC pattern as the existing CANalyst-II Python bridge. Bit-identical behavior to real firmware.

**Why IPC:** The firmware has a clean two-layer architecture — logic modules (pure C++ math, state machines, CAN frame construction) are fully host-compilable; only I/O modules (SPI, TWAI, ADC, GPIO) need stubs. Rather than wrestling with napi or WASM build systems, the IPC approach compiles to a plain `.exe` and communicates over stdin/stdout JSON-Lines. Zero Node.js build complexity. Cross-platform with a simple recompile. For 50-100 Hz model tick rates, IPC latency (~1ms per frame) is negligible.

**Duration estimate:** 2-3 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 2a.1 | Define `EcuModel` interface contract in `backend/src/sim/ecu-model.ts` | 1.1 | Small |
| 2a.2 | Define IPC protocol — JSON-Lines schema for frames in, frames out, config, state queries | — | Small |
| 2a.3 | Build `VirtualCanBus` in `backend/src/sim/bus/` — dual-channel, frame latency, bus-off injection | 1.1 | Medium |
| 2a.4 | Build `VirtualClock` — wall-clock or accelerated time for scenario playback | — | Small |
| 2a.5 | Fix stale native C++ tests — recompile with `-static` flag (fix libgcc DLL dependency) | — | Small |
| 2a.6 | Build `sim-engine-native` CMake target — RT + SYS + MTR logic modules with HAL stubs + JSON-Lines main loop → single `.exe` | 2a.2, 2a.5 | Large |
| 2a.7 | Build `IpcEngineAdapter` in backend — spawns native process, implements `EcuModel` interface over stdin/stdout | 2a.1, 2a.2, 2a.6 | Medium |
| 2a.8 | TypeScript fallback models — extract from `simulation/src/ecus/rt.ts`, `sys.ts`, `mtr.ts` into `backend/src/sim/ecus/` | 2a.1 | Small |

**IPC architecture:**

```
SimulationEngine (Node.js)
  ├── IpcEngineAdapter (spawns sim-engine-native.exe)
  │     stdin──→ {"type":"frame","bus":"high","id":"0x300","data":[...]}
  │     stdin──→ {"type":"config","bypass_epsc_sync":true}
  │     ←──stdout {"type":"frame","bus":"low","id":"0x204","data":[...]}
  │     ←──stdout {"type":"state","ecu":"rt","mode":"AUTO","safety":"Normal"}
  │
  └── VirtualCanBus ← frames from IPC + TS models + physical bridge
```

**Two-layer firmware architecture (why this works):**

```
LOGIC (host-compilable)              I/O (ESP32/STM32 only)
─────────────────────────            ────────────────────────
physics_model.h/.cpp                 main.cpp (FreeRTOS tasks)
steering_control.h (state machine)   can_driver_mcp2515.cpp (SPI)
safety_monitor.h (checks)            can_driver_twai.cpp (ESP-IDF)
mode_manager.h/.cpp (state machine)  mcp4725_dac.h (I2C)
can_dispatch.h (routing)             throttle_input.h::read_raw() (ADC)
heartbeat.h (timing)                 gear_control.h::GPIO helpers
watchdog.h (staleness)
throttle_input.h::tick() (math)
gear_control.h::set_mosfets() (logic)
```

**Verification:** RT model responds to 0x300 drive commands identically to real RT. SYS safety monitor triggers ESTOP on heartbeat timeout with same timing. MTR throttle maps ADC→speed with correct dead zone. MTR gear control detects conflicts and fails safe to N. Native test suite passes all 11 currently-passing tests plus 7 currently-stale tests.

---

## 6. Phase 2b — Third-Party ECU Models (EPS-C, SEB): CAN-Level Behavioral Models

**Goal:** High-fidelity CAN-level models for the two vendor ECUs, validated against real hardware captures. Same `EcuModel` interface as our ECUs.

**Duration estimate:** 2-3 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 2b.1 | Record real EPS-C CAN traffic — connect to CANalyst-II, run steering sweep scenarios (0°→±90°→0°), capture all frames | Hardware access | Small |
| 2b.2 | Record real SEB CAN traffic — brake pressure ramp (0→10 MPa), capture all frames | Hardware access | Small |
| 2b.3 | Build capture→replay tool — feed recorded command frames into TypeScript model, diff output against recorded responses | 2b.1-2b.2 | Medium |
| 2b.4 | Enhance EPS-C model — add rate-limited angle tracking (1st-order lag), per-fault-bit injection, torque feedback from steering load | 2a.1 | Medium |
| 2b.5 | Enhance SEB model — add 1st-order hydraulic response, stroke↔pressure coupling, per-fault-bit injection | 2a.1 | Medium |
| 2b.6 | Validate both models against hardware captures — measure timing accuracy, value accuracy, fault behavior | 2b.3-2b.5 | Medium |
| 2b.7 | Extract models into `backend/src/sim/ecus/` behind `EcuModel` interface | 2a.1, 2b.6 | Small |

**Verification:** Replay recorded command frames into model. Model output matches recorded hardware responses within 5% for angle/pressure values, within 2ms for frame timing, and sets correct fault bits for each error condition.

---

## 7. Phase 2c — Simulation Engine Integration

**Goal:** `SimulationEngine` orchestrator that loads any mix of ECU models (IPC native process or TypeScript class) behind the same `EcuModel` interface.

**Duration estimate:** 1-2 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 2c.1 | Build `SimulationEngine` — manages ECU instances, routes frames on virtual CAN bus, ticks models, couples to physics | 2a.1, 2a.3, 2a.4 | Large |
| 2c.2 | REST API: `/api/sim/engine/start`, `/api/sim/engine/stop`, `/api/sim/engine/state` | 2c.1 | Medium |
| 2c.3 | WebSocket channel for simulation state — ECU mode transitions, fault flags, physics telemetry | 2c.1 | Small |
| 2c.4 | Run existing 355 simulation tests against SimulationEngine with TypeScript models (all 6 ECUs) | 2c.1 | Medium |
| 2c.5 | Run same tests with IPC native models (RT/SYS/MTR) + TypeScript models (EPS-C/SEB/HOST) in same engine | 2a.7, 2b.7, 2c.1 | Medium |

**Verification:** Full Simulation mode: keyboard → HOST model → RT model (IPC) → MTR model (IPC) → physics updates → EPS-C model reports angle. All 6 ECUs active. Scenario replay produces identical results across IPC and TypeScript backends.

---

## 8. Phase 3 — Work Mode Orchestration

**Goal:** User-selectable work modes. Hybrid mode with per-ID routing. Bypass flags in UI.

**Duration estimate:** 2-3 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 3.1 | `WorkModeConfig` persistence — save/load/reload configurations as JSON files | — | Small |
| 3.2 | Work mode selector in Topbar — dropdown: Full Sim, Emulator, Hybrid, Bench, Monitor | 3.1 | Medium |
| 3.3 | **Full Simulation mode:** All 6 ECU models, virtual CAN bus, keyboard → Host → RT → actuators → physics loop | 2.5, 3.2 | Medium |
| 3.4 | **Part-by-Part Emulation mode:** Auto-detect ECUs via heartbeat → emulate missing ones with behavioral models → show feedback | 2.5, 0.3, 3.2 | Large |
| 3.5 | **Hybrid mode:** Real CAN bus + emulated ECUs filling gaps. FrameRouter source table auto-populated from ECU presence. `injectEmulatedToPhysical` toggle. | 1.3, 2.5, 3.2 | Large |
| 3.6 | **Bench Test mode:** Real CAN bus, no emulation. Bypass flags exposed in UI. Per-ECU "what's blocked" diagnostic (e.g., "Steering stuck in LISTEN_SYNC — EPS-C absent"). | 3.2 | Medium |
| 3.7 | **Monitor Only mode:** Passive decode + display. No injection, no emulation. Bus health dashboard. | 3.2 | Small |
| 3.8 | Runtime bypass flag control — send config frames to real ECUs or set flags on emulated models | 3.5, 3.6 | Medium |

**Verification:** Each mode starts with one click. Hybrid mode: unplug EPS-C → auto-starts EPS-C emulation → steering pipeline works. Bench mode: shows "EPS-C absent — steering blocked at LISTEN_SYNC."

---

## 9. Phase 4 — Rich UI & Diagnostics

**Goal:** Polished emulator interface, fault injection, scenario runner, diagnostics.

**Duration estimate:** 2-3 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 4.1 | ECU topology diagram — visual graph showing real (green), emulated (blue), missing (gray) ECUs with CAN bus connections | 3.4 | Medium |
| 4.2 | Live signal display in Emulator — per-signal current values, rolling counters, freshness indicators | 3.4 | Small |
| 4.3 | Per-ECU fault injection panel — trigger specific faults: heartbeat timeout, sensor failure, bus-off, frozen counter, checksum corruption | 2.5 | Medium |
| 4.4 | Scenario runner in UI — select scenario (drive-forward, estop-flow, mode-transition, heartbeat-timeout), run, observe ECU responses | 2.5 | Large |
| 4.5 | CAN health dashboard — per-bus TEC/REC gauges, bus-off detection, frame rate anomalies, error frame count | 1.8 | Small |
| 4.6 | "Missing ECU" diagnostic panel — shows dependency chain and what's blocked (e.g., "RT needs EPS-C 0x201 for steering sync → currently absent → bypass available") | 3.4 | Medium |
| 4.7 | CAN Monitor card redesign — per-message visual cards with signal boxes, byte layout tooltips, freshness bars (per `tem/debug-tool-update-plan.md`) | 0.1 | Large |
| 4.8 | YAML→TypeScript CAN catalog generator — `shared/can/generate_can_index.py` → single `can-index.ts`, directly uses shared YAMLs to eliminate dual hand-maintenance and fix simulator DLC mismatches | — | Medium |

**Verification:** Emulator shows live-updating signal values. Fault injection triggers visible ESTOP in dashboard. Scenario runner replays drive-forward with correct ECU state transitions.

---

## 10. Phase 5 — Hardening & Test Coverage

**Goal:** Production-quality reliability. Comprehensive test coverage. Documentation.

**Duration estimate:** 1-2 weeks

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 5.1 | Fix remaining P1/P2 bugs from `bugs.md` | — | Medium |
| 5.2 | Expand E2E tests — cover all 5 work modes, transport switching, emulator start/stop | 3.x, 4.x | Medium |
| 5.3 | Backend API tests — all `/api/sim/*`, `/api/system/*`, `/api/can/*` endpoints | — | Medium |
| 5.4 | Recompile stale native C++ tests with `-static` flag (fix libgcc DLL dependency) | — | Small |
| 5.5 | CI pipeline — run native C++ tests + backend tests + UI tests + E2E on commit | — | Medium |
| 5.6 | Performance baseline — measure WebSocket latency, frame throughput, UI render time at 500 fps | — | Small |
| 5.7 | Update `run.md`, `CANALYST-II-SETUP.md`, architecture docs for v0.4.0 | — | Small |

---

## 11. Component Tracking

| Component | Phase | Status |
|-----------|-------|--------|
| `FrameRouter` | 1.1 | Not started |
| `SimulationEngine` | 2c.1 | Not started |
| `sim-engine-native` (IPC executable) | 2a.6 | Not started — CMake target for RT+SYS+MTR logic → single `.exe` |
| `IpcEngineAdapter` (Node.js side) | 2a.7 | Not started — spawns native process, stdin/stdout JSON-Lines |
| ECU models — RT, SYS, MTR (TypeScript fallback) | 2a.8 | ⚠️ In `simulation/src/ecus/` (vitest-only) |
| ECU models — EPS-C, SEB (TypeScript) | 2b.4-2b.7 | ⚠️ In `simulation/src/ecus/` — needs validation against hardware |
| Hardware captures (EPS-C, SEB) | 2b.1-2b.2 | ❌ Not recorded yet — needs CANalyst-II + real units |
| Capture→replay validation tool | 2b.4 | Not started |
| `VirtualCanBus` | 2a.2 | ⚠️ In `simulation/src/bus/` (vitest-only) |
| `VirtualClock` | 2a.3 | Not started |
| Physics (tricycle) | 2.x | ⚠️ In `simulation/src/physics/` (vitest-only); C++ version in `native-test/` |
| Emulator UI | 4.1-4.2 | ⚠️ Static data only |
| Tab persistence | 0.1 | Not started |
| Work mode selector | 3.2 | Not started |
| Bypass flag control | 3.8 | Not started |
| Scenario runner | 4.4 | ⚠️ In `simulation/src/scenarios/` (vitest-only) |
| Fault injection UI | 4.3 | ⚠️ In `simulation/src/harness/` (vitest-only) |
| CAN catalog generator | 4.8 | Not started |
| Legacy simulator (`simulator/`) | — | ⚠️ Deprecated — replaced by SimulationEngine |

---

## 12. Dependency Graph

```
Phase 0 (foundation fixes)
  └─→ Phase 1 (frame pipeline, transport)
        └─→ Phase 2a (RT/SYS/MTR native C++ models)  ──┐
        └─→ Phase 2b (EPS-C/SEB CAN-level models)    ──┤
              └─→ Phase 2c (SimulationEngine)          ──┤
                    └─→ Phase 3 (work modes)             │
                          └─→ Phase 4 (rich UI)           │
                                └─→ Phase 5 (hardening)
```

**Can be parallelized:**
- Phase 2a (our ECUs, C++) and Phase 2b (third-party ECUs, TypeScript) are independent — different codebases, different validation strategies
- Phase 4.7 (CAN Monitor card redesign) is independent — can start any time after 0.1
- Phase 4.8 (YAML→TS generator) is independent — can start immediately
- Phase 0 bug fixes are all independent of each other
- Phase 2b.1-2b.2 (hardware captures) can happen any time hardware is available — not blocked by any code work

---

## 13. Risks

| Risk | Mitigation |
|------|-----------|
| IPC child process crashes or hangs | TypeScript fallback models always available; the `IpcEngineAdapter` detects process exit and can restart or fall back |
| IPC JSON-Lines parsing overhead at high frame rates | 50-100 Hz tick rate = ~10ms per tick; JSON serialize/deserialize is ~0.1ms; overhead is negligible |
| Third-party ECU models diverge from real hardware behavior | Capture→replay validation (Phase 2b.3) gates each model before it enters SimulationEngine |
| No access to real EPS-C/SEB hardware for captures | Rely on manufacturer CSV protocol documents as specification; TypeScript models implement the documented contract; validate later when hardware available |
| SimulationEngine can't keep up at real-time (200+ fps) | Virtual clock supports slowdown; TypeScript path is single-threaded but adequate at 50 Hz model tick rates; IPC native path is faster for compute-heavy models |
| Phase 3 complexity — Hybrid mode has many edge cases | Incremental: Full Sim first (no hardware), then Emulator (no hardware), then Hybrid (both) |
| YAML→TS generator diverges from hand-maintained copies | Generator becomes the single source; hand-maintained copies deleted after generator is validated |
| IPC models and TypeScript models diverge in behavior | Shared test suite (Phase 2c.4) runs identical scenarios against both backends |
