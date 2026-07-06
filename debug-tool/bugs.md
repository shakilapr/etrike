# Debug Tool — Known Bugs

> Generated 2026-07-06 from full codebase audit.
> Severity: **P0** = data loss / safety confusion, **P1** = wrong behavior, **P2** = cosmetic / edge case.

---

## P0 — Critical

### BUG-01: Disconnected bus still shows FPS and "active" in UI

**Severity:** P0  
**Files:** `backend/src/types/can.ts:316-318`, `backend/src/db/queries.ts:130-144`, `ui/src/components/Topbar.svelte:16-19`

**Symptom:** Unplug the physical CAN bus. The Topbar still shows "High CAN: 120 fps" and the health dot stays green. The Statistics tab still shows bus load %. The data is frozen from the last stats message before disconnect — it never clears.

**Root cause:** Stats are stored in SQLite via `DebugStore.setStats()` and served via `getStats()` with zero staleness checking. `BusStats.active` is a stored boolean set by the bridge's last stats message — it's never derived from "are frames actually arriving?". The bridge stops sending stats when the bus disconnects. The DB holds the last values forever.

**How to reproduce:**
1. Start debug tool with CAN hardware connected
2. Observe FPS in Topbar
3. Unplug CAN bus or power off the ECU
4. Topbar still shows the old FPS, health stays green

**Fix direction:** Add a `received_at REAL` column to `runtime_state`, set on every `setStats()`. In `getStats()`, return `defaultStats()` (all zeros) if `Date.now()/1000 - received_at > 5`.

---

### BUG-02: Disconnected ECU shows "ready" in health bar

**Severity:** P0  
**Files:** `ui/src/stores/telemetry.ts:70-76`, `ui/src/stores/can.ts:9,23-29`

**Symptom:** Power off the RT controller. The Topbar ECU health section still shows "RT: ready" with a green dot. The user thinks the ECU is present and healthy, but it's been dead for minutes.

**Root cause:** `ecuPresence` derives from `latestById`, which is a derived store from the `frames` ring buffer. It keeps the LAST frame per `(bus, id)` key. When an ECU disconnects, its last heartbeat frame stays in `latestById` until the ring buffer rotates it out (which can be minutes). There's no time-based staleness check.

**How to reproduce:**
1. Start debug tool with RT connected (0x7FD heartbeats visible)
2. Topbar shows "RT: ready" 
3. Power off RT
4. Topbar still shows "RT: ready" for up to several minutes

**Fix direction:** Add a `ts` field check in `ecuPresence` — only consider frames fresher than 3 seconds. Note: WebSocket frames only have `ts` in milliseconds, while SQLite frames have `ts_real` in seconds. Handle timestamps consistently.

---

### BUG-03: BusDetector locks to a bus and never unlocks

**Severity:** P1  
**Files:** `backend/src/types/can.ts:470-513`

**Symptom:** The bus auto-detection locks onto "high" after seeing 3 high-bus-only CAN IDs. If you then physically swap the connection to the low bus, all frames are still labeled "high" in the UI. The `bus_detection` status shows `confidence: "high"` permanently.

**Root cause:** `BusDetector.locked` is set once and never cleared. There's no timeout, no reset mechanism, and no re-evaluation. The only way to reset is to restart the backend or call `BusDetector.reset()`.

**Fix direction:** Add a staleness timeout — if no frames matching the locked bus arrive for 10s, reset detection. Or make `locked` a soft preference rather than a hard lock, re-evaluated continuously.

---

### BUG-04: Tab switch destroys component state

**Severity:** P1  
**Files:** `ui/src/App.svelte:240-260`

**Symptom:** You're in the CAN Monitor tab with a bus filter set. Switch to Injector, then switch back to Monitor — the filter is gone, scroll is lost.

**Root cause:** Tab switching uses Svelte `{#if activeTab === "monitor"}...{:else if...}` blocks. When `activeTab` changes, the old component is destroyed and the new one created from scratch.

**Fix direction:** Replace `{#if}` with CSS `display: none` / `display: block` to keep all components mounted, or move all UI state into stores.

---

### BUG-12: DLC mismatch corrupts injection

**Severity:** P0  
**Files:** `debug-tool/simulator/src/can-generator.ts:25-64`, `backend/src/types/can.ts`

**Symptom:** `CAN_MESSAGES` declares a DLC that conflicts with the actual decode logic (e.g. `0x300` needs 8 bytes based on decode, but might be defined differently). This causes 400 Bad Request on injection or garbage payload generation.

**Root cause:** Hardcoded DLC mismatches between `CAN_MESSAGES`, the simulator `DEFAULT_PROFILE`, and the decode/encode logic.

**Fix direction:** Audit all DLCs in `CAN_MESSAGES` and simulator profiles against the `decodeFrame` logic. Unify to a single source of truth.

---

### BUG-19: Synchronous Database Pruning Bottleneck

**Severity:** P0  
**Files:** `backend/src/db/queries.ts:78-97`, `backend/src/db/queries.ts:240-261`

**Symptom:** When the database reaches `MAX_FRAMES` (50,000), realtime performance drastically degrades. The event loop blocks.

**Root cause:** `pruneFrames()` is called synchronously inside `insertFrame()` on every single CAN frame. It does an expensive SQLite `LEFT JOIN` on `recording_frames` to prune exactly one row per insert.

**Fix direction:** Pruning must be batched (e.g. delete 5000 rows when count reaches 55000) or moved to a periodic timer.

---

### BUG-20: Indefinite Database Growth (Memory Leak in WAL)

**Severity:** P0  
**Files:** `backend/src/db/queries.ts:240-261`

**Symptom:** If a user repeatedly starts and stops recordings, the database file grows infinitely, bypassing `MAX_FRAMES`.

**Root cause:** `pruneFrames()` skips deleting any frames referenced in `recording_frames`. It makes no distinction between active and stopped recordings.

**Fix direction:** Introduce a manual retention limit for stopped recordings or a bulk delete mechanism.

---

### BUG-23: WebSocket Broadcast Flood on Bus Lock-in

**Severity:** P0  
**Files:** `backend/src/serial/reader.ts:181-183`

**Symptom:** Once `BusDetector` successfully detects the bus, the Node.js backend burns CPU and floods the WebSocket, causing severe UI lag.

**Root cause:** `if (this.detectedBus !== prevDetected || this.busDetector.state.confidence === "high")` is evaluated on every frame. Once confidence is high, it stays high permanently, triggering 1000 WebSocket broadcasts per second.

**Fix direction:** Only broadcast when the confidence *transitions* to high: `prevConfidence !== "high" && newConfidence === "high"`.

---

### BUG-31: Python Bridge Busy-Polls at 200Hz

**Severity:** P0  
**Files:** `backend/canalystii_bridge.py:256-287`

**Symptom:** `python.exe` consumes 25–100% of a CPU core while CANalyst-II is connected, even with zero frames.

**Root cause:** The main loop calls `time.sleep(POLL_SECONDS)` where `POLL_SECONDS` is clamped to 5ms (200 Hz). At 200 Hz, it calls `canalystii.receive()` twice per tick, saturating a core.

**Fix direction:** Increase `POLL_SECONDS` dynamically when idle (back off to 50ms), or use event-based reception.

---

### BUG-32: Python Bridge `by_id` Dictionary Memory Leak

**Severity:** P0  
**Files:** `backend/canalystii_bridge.py:200`

**Symptom:** Bridge memory grows continuously. Stats JSON payloads inflate to megabytes per minute.

**Root cause:** `bus_stats["by_id"][can_id_text] = bus_stats["by_id"].get(can_id_text, 0) + 1` accumulates unique IDs forever without pruning.

**Fix direction:** Cap `by_id` to a sliding window or reset it periodically.

---

### BUG-43: `cmd.ts` Error Path Uses `updateLatestInjectionStatus`

**Severity:** P0  
**Files:** `backend/src/api/cmd.ts:56-63`, `100`

**Symptom:** When injection fails concurrently, the wrong row is marked as "error" in the DB.

**Root cause:** The catch block calls `updateLatestInjectionStatus("error")` instead of `updateInjectionByCorrelation(correlationId, "error")`.

**Fix direction:** Replace `updateLatestInjectionStatus("error")` with `updateInjectionByCorrelation(correlationId, "error")` in both send and periodic paths.

---

### BUG-46: `sim.ts` Hub Access Broken

**Severity:** P0  
**Files:** `backend/src/api/sim.ts:33-34`

**Symptom:** Simulator injects frames to DB but they never appear in UI.

**Root cause:** `const hub = (app as any).__hub;` is silently undefined in tests or if initialization order changes. The `if (hub)` guard silently swallows the error.

**Fix direction:** Pass `hub: StreamHub` explicitly to `registerSimRoutes()`.

---

## P1 — Wrong Behavior

### BUG-06: CANalyst-II Auto-Detection Blocks Startup

**Severity:** P1  
**Files:** `backend/src/index.ts:79`, `backend/src/canalyst/bridge.ts:51-77`

**Symptom:** Backend pauses for 3 seconds before listening on port 3000 if no CANalyst-II is present.

**Root cause:** Blocking `await canalyst.waitForConnection(3000)` in `main()`.

**Fix direction:** Start `app.listen()` first, then run detection asynchronously.

---

### BUG-08: WebSocket Reconnect Floods Unfiltered Frames

**Severity:** P1  
**Files:** `ui/src/lib/ws.ts:34-42`

**Symptom:** Brief flash of all frames on reconnect before filter applies.

**Root cause:** UI sets "connected" state before sending the pending filter to the server.

**Fix direction:** Send filter before triggering `onState(true)`.

---

### BUG-09: REST `/api/status` Returns Stale Stats

**Severity:** P1  
**Files:** `backend/src/api/system.ts:29`, `backend/src/db/queries.ts:136-144`

**Symptom:** `/api/status` returns `active: true, fps: 120` hours after disconnect.

**Root cause:** Same as BUG-01.

**Fix direction:** Same as BUG-01 (add timestamp).

---

### BUG-10: `SERIAL_PORT` Default is Windows-Only

**Severity:** P1  
**Files:** `backend/src/config.ts:8`

**Symptom:** Silent failure on Linux/macOS because `COM3` doesn't exist.

**Root cause:** Hardcoded `COM3` default.

**Fix direction:** Platform-detect default or require explicit config.

---

### BUG-11: No Transport Hot-Swap

**Severity:** P1  
**Files:** `backend/src/index.ts:43-97`

**Symptom:** Cannot switch from Serial to CANalyst-II without restarting the backend process.

**Fix direction:** Add `/api/system/switch-transport` endpoint.

---

### BUG-15: Serial Bridge Reconnection Gives Up

**Severity:** P1  
**Files:** `backend/src/serial/reader.ts:101-124`

**Symptom:** After 10 attempts (3 mins), serial bridge permanently stops reconnecting.

**Fix direction:** Switch to slow polling (30s) instead of giving up completely.

---

### BUG-24: `latestById` is O(n) per Frame

**Severity:** P1  
**Files:** `ui/src/stores/can.ts:23-29`

**Symptom:** UI gets sluggish at high FPS.

**Root cause:** `derived` store iterates over all 1000 frames on every single new frame insert.

**Fix direction:** Replace with an incrementally updated writable store.

---

### BUG-25: Pipeline Correlation is O(n²)

**Severity:** P1  
**Files:** `backend/src/api/can.ts:56-87`

**Symptom:** Pipeline tab loads slowly or times out.

**Root cause:** `Array.find()` over arrays up to 2000 elements for each trigger frame.

**Fix direction:** Binary search or pre-indexing.

---

### BUG-28: `clearFrames()` Leaves Stale Counts

**Severity:** P1  
**Files:** `backend/src/db/queries.ts:263-265`

**Symptom:** Recordings show 15,000 frames but replay is empty.

**Root cause:** Deletes from `recording_frames` but does not `UPDATE recordings SET frame_count = 0`.

**Fix direction:** Add the UPDATE.

---

### BUG-33: `faults.ts` State Survives Reconnects

**Severity:** P1  
**Files:** `ui/src/stores/faults.ts:47-63`

**Symptom:** Spurious "fault cleared" logs on WebSocket reconnect.

**Root cause:** Module-level `let` variables and cooldown maps are never reset.

**Fix direction:** Move state inside `initFaultWatcher()` or add reset method.

---

### BUG-35: `sendZeroFrames()` Ignores `selectedBus`

**Severity:** P1 (Safety Risk)  
**Files:** `ui/src/components/Controller.svelte:149-162`

**Symptom:** In HIGH bus mode, pressing Stop sends low-bus frames directly, bypassing the RT controller and causing hardware conflict.

**Root cause:** `sendZeroFrames()` unconditionally sends on `0x204` and `0x169`.

**Fix direction:** Check `selectedBus` before sending bus-specific frames.

---

### BUG-37: `attachToActiveRecordings` Runs 3 SQL Statements Per Frame

**Severity:** P1  
**Files:** `backend/src/db/queries.ts:228-238`

**Symptom:** High DB latency at 1000 FPS.

**Root cause:** `SELECT id FROM recordings WHERE stopped_at IS NULL` runs synchronously on every single frame insert.

**Fix direction:** Cache active recording IDs in memory.

---

### BUG-39: REST Poll Never Suspends With WS

**Severity:** P1  
**Files:** `ui/src/App.svelte:106`

**Symptom:** UI spams `GET /api/status` every 3 seconds forever, even when WebSocket is healthy.

**Fix direction:** Skip fetch if `get(wsConnected) === true`.

---

### BUG-40: `stopRecording()` Returns Inaccurate Count

**Severity:** P1  
**Files:** `backend/src/db/queries.ts:191-194`

**Symptom:** Recording frame count is off-by-one or more in the UI after stopping.

**Root cause:** Returns the stored `frame_count` which misses in-flight DB transactions.

**Fix direction:** Compute count dynamically via `COUNT(*)` in the return query.

---

### BUG-41: Periodic ESTOP Missing Confirmation Gate

**Severity:** P1  
**Files:** `ui/src/components/UnitTest.svelte:83`

**Symptom:** Starting periodic ESTOP skips the `confirmEstop` UI check if switching from another command.

**Fix direction:** Reset `confirmEstop` when changing commands, or enforce check before `startPeriodic`.

---

### BUG-44: `normalizeFrame` Timestamps Break Age Calculation

**Severity:** P1  
**Files:** `backend/src/types/can.ts:262`, `ui/src/components/UnitTest.svelte:173-179`

**Symptom:** Live frames always show "0 ms" age.

**Root cause:** `ts` is in milliseconds. UI treats it as seconds when `ts_real` is absent.

**Fix direction:** Unify timestamp units or fix UI fallback logic.

---

### BUG-45: `findMessage` Fallback Ignores Bus

**Severity:** P1  
**Files:** `backend/src/types/can.ts:233-236`

**Symptom:** Decodes frames with the wrong definitions if a bus is misidentified.

**Root cause:** `CAN_MESSAGES.find((item) => item.id === normalized)` ignores the `bus` parameter.

**Fix direction:** Remove fallback or require bus match.

---

### BUG-47: `broadcast()` Mutates Set During Iteration

**Severity:** P1  
**Files:** `backend/src/ws/stream.ts:86-99`

**Symptom:** Potential race condition skipping WebSocket clients.

**Root cause:** `this.clients.delete(client)` during `for...of` iteration while `close` event handler also deletes.

**Fix direction:** Snapshot `clients` to array before iterating.

---

### BUG-48: Serial Bridge `cmd_ack` Ignores Correlation ID

**Severity:** P1  
**Files:** `backend/src/serial/reader.ts:154-158`

**Symptom:** BUG-21 (injection race) is still present on serial transport.

**Root cause:** Serial `cmd_ack` handler uses `updateLatestInjectionStatus()` instead of correlation ID.

**Fix direction:** Parse and use `message.correlation_id`.

---

## P2 — Cosmetic / Edge Cases

- **BUG-05:** Serial port fails silently (UI shows error, just no backend console log).
- **BUG-16:** CANalyst-II bridge abandoned after detection failure can't be reused.
- **BUG-17:** No `.env` file loading.
- **BUG-18:** Emulator `simMode` toggle resets when switching tabs.
- **BUG-26:** `SerialBridge.start()` called twice on reconnect (Double open error).
- **BUG-29:** `stopRecording()` does not prevent double-stop.
- **BUG-30:** `normalizeBus()` falls through to `"high"` silently.
- **BUG-34:** `normalizeFilter` strips bus from bare IDs.
- **BUG-36:** Controller `tick()` reads `heldNow` via Svelte reactive assignment (stale closure edge case).

---

## Not Bugs (Working as Intended)

- **WebSocket keepalive (30s ping, 60s eviction)**
- **Frame ring buffer (1000 frames) memory bound**
- **`ingestInitialFrames` wiping live data**
- **REST status poll interval 3s fallback**
- **CANalyst-II stdin reading** (Python bridge DOES read stdin)
- **`Array.reverse()` double allocation** (V8 does this in-place)
- **`kbBus.subscribe()()` leak** (Standard Svelte idiom)
- **Frame timestamp drift** (Fallback behavior is intentional)
- **`latestById` update from WS**
