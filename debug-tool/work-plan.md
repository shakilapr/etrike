# Debug Tool v0.4.0 — Remaining Work Plan

> **Status:** Phases 1–4 (backend cleanup, emulator kill, frontend perf, store/input cleanup) are COMPLETE.  
> This document covers everything that remains to bring the tool to production quality.

---

## Phase 5 — Database Performance Overhaul

The single biggest bottleneck. Every CAN frame (1,000–2,500 FPS from hardware) triggers a synchronous, unprepared `INSERT` that blocks the Node.js event loop. During active recordings, each frame also triggers an additional transaction (`attachToActiveRecordings`). Pruning runs row-by-row deletes. This phase eliminates all of it.

### 5A. Cache All Prepared Statements

**File:** `backend/src/db/queries.ts`

**Problem:** Every method calls `this.db.prepare()` on every invocation. `insertFrame()` alone re-parses the INSERT SQL string 2,000+ times per second. `better-sqlite3` has internal statement caching, but the repeated API call overhead and object allocation is measurable on the hot path.

**Changes:**
- Move all `this.db.prepare(...)` calls from method bodies into the `DebugStore` constructor
- Store them as private class fields (e.g., `private readonly stmtInsertFrame: Statement`)
- Reference the cached statements in each method

**Test:**
- Existing `can.test.ts` (711 lines) must still pass — `npm run test:unit`
- Add a micro-benchmark: insert 10,000 frames, compare wall-clock time before/after

---

### 5B. Batch Frame Insertion

**File:** `backend/src/db/queries.ts`

**Problem:** Each bridge calls `insertFrame()` individually per CAN frame. SQLite wraps each call in an implicit transaction (fsync per frame). At 2,000 FPS this means 2,000 fsyncs/second — the main bottleneck.

**Changes:**
- Add `insertFrames(frames: NormalizedFrame[]): void` method that wraps the loop in a single `this.db.transaction()`
- The transaction uses the cached prepared statement from 5A
- `attachToActiveRecordings()` is also batched inside the same transaction (one query for all frames, not one transaction per frame)

**Test:**
- Insert 5,000 frames via `insertFrames()` — verify all rows present, decoded JSON intact
- Insert batch with active recording — verify all `recording_frames` rows created
- Benchmark: 10,000 frames via old `insertFrame()` loop vs new `insertFrames()` batch

---

### 5C. In-Memory Write Queue

**Files:** `backend/src/canalyst/bridge.ts`, `backend/src/serial/reader.ts`, `backend/src/mqtt/bridge.ts`, `backend/src/sim/engine.ts`

**Problem:** All four frame sources call `store.insertFrame()` synchronously on every frame. There is no buffering layer between the hot ingestion path and the database.

**Changes:**
- Create `backend/src/db/write-queue.ts`:
  - Exposes `enqueue(frame: NormalizedFrame): void` — O(1) array push
  - Internal `flush()` runs on a `setInterval` (every 100ms or when buffer hits 500 frames, whichever comes first)
  - `flush()` calls `store.insertFrames(batch)` with the buffered array, then clears it
  - Exposes `drain(): Promise<void>` for graceful shutdown
- Update all four bridges and `SimulationEngine` to call `writeQueue.enqueue(frame)` instead of `store.insertFrame(frame)`
- Wire the write queue in `index.ts`

**Test:**
- Unit test: enqueue 1,000 frames rapidly → verify all flushed to DB after drain
- Unit test: verify flush triggers at both the count threshold and the time threshold
- Integration: run sim engine at 100Hz with 6 ECUs → verify no frames lost

---

### 5D. Fix Bulk Pruning

**File:** `backend/src/db/queries.ts`

**Problem:** `pruneFrames()` selects candidate rows then deletes them one-by-one in a loop. `pruneStoppedRecordings()` also deletes row-by-row.

**Changes:**
- Replace the delete loop in `pruneFrames()` with:
  ```sql
  DELETE FROM can_frames WHERE id IN (
    SELECT f.id FROM can_frames f
    WHERE NOT EXISTS (SELECT 1 FROM recording_frames rf WHERE rf.frame_id = f.id)
    ORDER BY f.id ASC LIMIT ?
  )
  ```
- Replace row-by-row recording prune with a similar bulk `DELETE WHERE id IN (...)` pattern
- Add missing index: `CREATE INDEX IF NOT EXISTS idx_recordings_stopped ON recordings(stopped_at)`

**Test:**
- Seed 60,000 frames → run prune → verify count drops to MAX_FRAMES
- Seed frames with active recording references → verify those frames are NOT pruned
- Benchmark: old prune (10,000 rows) vs new prune

---

### 5E. Fix `latestById()` Query

**File:** `backend/src/db/queries.ts`

**Problem:** Fetches 5,000 rows ordered by timestamp, then deduplicates in JavaScript to find the latest frame per `(bus, can_id)`. Wasteful — SQLite can do this natively.

**Changes:**
- Replace the JS dedup with a proper SQL query:
  ```sql
  SELECT * FROM can_frames
  WHERE id IN (
    SELECT MAX(id) FROM can_frames GROUP BY bus, can_id
  )
  ```
- This returns at most ~37 rows (one per unique CAN ID) instead of 5,000

**Test:**
- Seed 10,000 frames across all 37 CAN IDs → verify `latestById()` returns exactly 37 rows, each the most recent
- Benchmark: old (5,000 row fetch + JS dedup) vs new (37 row fetch)

---

### 5F. Combine Stats Queries

**File:** `backend/src/db/queries.ts`

**Problem:** `setStats()` does 2 separate INSERT/UPDATE calls. `getStats()` does 2 separate SELECT calls. Neither is wrapped in a transaction.

**Changes:**
- Wrap `setStats()` in a single `db.transaction()`
- Combine `getStats()` into a single query:
  ```sql
  SELECT key, value FROM runtime_state WHERE key IN ('stats', 'stats_updated_at')
  ```

**Test:**
- Existing stats tests must pass
- Verify stats round-trip: set → get → compare

---

### Phase 5 Verification

```bash
cd debug-tool/backend
npm run test:unit          # All existing tests pass
npx tsc --noEmit           # Clean compile
```

Manual: Run with simulator at max throughput for 5 minutes. Monitor:
- Node.js event loop lag (should be < 10ms)
- SQLite WAL file size (should stay bounded)
- WebSocket frame delivery latency (should be < 50ms)

---

## Phase 6 — Worker Thread Isolation

Move the entire SQLite database into a dedicated Node.js Worker Thread so that disk I/O can never block the Fastify event loop, WebSocket broadcasting, or CAN decoding.

### 6A. Create the DB Worker

**Files:**
- `[NEW] backend/src/db/worker.ts` — the worker thread script
- `[NEW] backend/src/db/worker-client.ts` — main-thread proxy that mirrors the `DebugStore` API

**Changes:**
- `worker.ts`:
  - Instantiates `DebugStore` inside the worker
  - Listens for `parentPort` messages: `{ type: "insertFrames", frames }`, `{ type: "queryFrames", query, requestId }`, etc.
  - Sends results back via `parentPort.postMessage({ requestId, result })`
- `worker-client.ts`:
  - Creates a `Worker` pointing to `worker.ts`
  - Exposes the same interface as `DebugStore` but async
  - Fire-and-forget for writes (`insertFrames`, `setStats`)
  - Request-response with `Promise` for reads (`queryFrames`, `latestById`, `getStats`, etc.)
  - Uses a `Map<requestId, { resolve, reject }>` to correlate responses

### 6B. Migrate Consumers to Async DB

**Files:** `backend/src/api/can.ts`, `backend/src/api/recordings.ts`, `backend/src/api/cmd.ts`, `backend/src/api/system.ts`, `backend/src/index.ts`

**Changes:**
- Replace `store.queryFrames(...)` (sync) with `await workerClient.queryFrames(...)` (async)
- Fastify route handlers are already async — no structural change needed
- Write queue (`write-queue.ts`) now sends batches to the worker instead of calling `store.insertFrames()` directly

### 6C. Graceful Shutdown

**File:** `backend/src/index.ts`

**Changes:**
- On shutdown: `writeQueue.drain()` → `workerClient.close()` → `worker.terminate()`
- Ensures all buffered frames are written before the worker exits

**Test:**
- Start backend, ingest 1,000 frames, immediately shutdown → verify all 1,000 frames in the SQLite file
- Run full sim for 60 seconds → verify zero frame loss
- Verify event loop lag is < 2ms with worker (vs current 10–50ms)

---

## Phase 7 — CAN Catalog Unification (Phase 0 Debt)

The most fragile technical debt. Two hand-maintained 600-line files (`backend/src/types/can.ts` and `ui/src/lib/can-decoder.ts`) contain near-identical CAN message catalogs, decode switch statements, and encode switch statements. Any CAN message change requires editing both files. The YAML source of truth (`shared/can/can_high.yaml`, `shared/can/can_low.yaml`) already exists but is not used for decode/encode generation.

### 7A. Extend the Code Generator

**File:** `shared/can/generate_can_index.py` (currently 112 lines)

**Problem:** The existing generator produces `can-index.ts` (a signal metadata index), but does NOT generate the decode/encode logic. The hand-maintained switch statements are the actual bug-prone code.

**Changes:**
- Extend (or create a new companion script) to generate:
  1. **`can-catalog.ts`** — the `CAN_MESSAGES[]` array with all field definitions, replacing the hand-maintained arrays in both files
  2. **`can-decode.ts`** — a data-driven `decodeFrame(bus, canId, data)` function that reads signal definitions from the catalog and unpacks bytes generically (using `readI16BE`, `readU32LE`, etc. based on signal type/endianness), replacing the 30-case switch statement
  3. **`can-encode.ts`** — a data-driven `encodePayload(bus, canId, values)` function that packs values into bytes generically, replacing the 25-case switch statement in `can-decoder.ts`
- Output directory: `debug-tool/shared/generated/`
- Add `--check` mode for CI (exit 1 if generated output differs from committed files)

### 7B. Create Shared Package

**Files:**
- `[NEW] debug-tool/shared/package.json` — `@etrike/debug-shared`
- `[NEW] debug-tool/shared/generated/can-catalog.ts`
- `[NEW] debug-tool/shared/generated/can-decode.ts`
- `[NEW] debug-tool/shared/generated/can-encode.ts`
- `[NEW] debug-tool/shared/src/read-helpers.ts` — byte read/write utilities (extracted from current files)
- `[NEW] debug-tool/shared/src/fault-decoders.ts` — `decodeSesFaults()`, `decodeSebFaults()` (extracted)

### 7C. Migrate Backend

**File:** `backend/src/types/can.ts` (533 lines → ~80 lines)

**Changes:**
- Remove `CAN_MESSAGES[]` array, `decodeFrame()` switch statement
- Import from `@etrike/debug-shared`
- Keep backend-only code: `BusDetector`, `INJECTION_TEMPLATES`, `normalizeFrame()`, `validateDataBytes()`

### 7D. Migrate Frontend

**File:** `ui/src/lib/can-decoder.ts` (645 lines → ~60 lines)

**Changes:**
- Remove `CAN_MESSAGES[]` array, `decodeFrame()` switch, `encodePayload()` switch
- Import from `@etrike/debug-shared`
- Keep frontend-only code: `formatDecoded()`, `frameTime()`, `frameAge()`

### 7E. Migrate Simulator

**File:** `simulator/src/can-generator.ts`

**Changes:**
- Import CAN definitions from `@etrike/debug-shared` instead of hard-coding signal shapes

**Test:**
- Run generator → diff output against committed files → must match (`--check` mode)
- Run `backend/src/types/can.test.ts` (711 lines of decode/encode tests) → all pass against the generated decoder
- Run `ui/src/lib/can-decoder.test.ts` → all pass
- Manually add a fake CAN message to `can_low.yaml` → re-run generator → verify it appears in catalog, decode, and encode without any manual code changes

---

## Phase 8 — Simulation Engine Timing

### 8A. High-Resolution Tick Loop

**File:** `backend/src/sim/engine.ts`

**Problem:** The simulation engine uses `setTimeout(loop, 10)` for its 100Hz tick. Node.js `setTimeout` has ~1–4ms jitter depending on OS and GC pressure. For accurate ECU simulation (especially heartbeat timeout detection at 100ms resolution), this jitter causes false timeout triggers and inconsistent physics.

**Changes:**
- Replace `setTimeout` with a `setImmediate` + `performance.now()` polling loop:
  ```typescript
  const loop = () => {
    if (!this._state.running) return;
    const now = performance.now();
    if (now - this.lastTickHr >= this.tickMs) {
      this.tick();
      this.lastTickHr += this.tickMs;
    }
    setImmediate(loop);
  };
  ```
- Add a `tickJitter` diagnostic counter that tracks the delta between expected and actual tick times
- Expose jitter stats via `getState()` so the UI can display simulation fidelity

**Trade-off:** `setImmediate` polling uses more CPU than `setTimeout` when idle. Add a `sleepWhenIdle` flag: if no ECU models are active, fall back to `setTimeout(loop, 100)`.

**Test:**
- Run sim for 10 seconds at 100Hz → collect tick timestamps → verify standard deviation < 1ms (vs current ~3–4ms)
- Run sim with 6 ECUs for 60 seconds → verify zero heartbeat false-timeout events
- Verify CPU usage is acceptable (< 5% on modern hardware when ECUs are active)

---

## Phase 9 — Frontend Performance Polish

### 9A. Eliminate Per-Frame Array Allocation in Stores

**File:** `ui/src/stores/can.ts`

**Problem:** On every incoming CAN frame, `ingestMessage()` calls `frameBuffer.push()` then `frameStore.set(frameBuffer.toArray())`. `toArray()` allocates a new 1,000-element array on every call. At 100+ frames/sec from the WebSocket, this creates significant GC pressure.

**Changes:**
- Change the store to use a **version counter** pattern instead of allocating a new array:
  - The `RingBuffer` itself becomes the store's backing data
  - Instead of `set(newArray)`, increment a version number to trigger Svelte reactivity
  - Components read directly from the RingBuffer (via a `$derived` or reactive getter) instead of subscribing to a full array copy
- Alternative (simpler): keep `toArray()` but throttle store updates to ~30Hz max (matching the WebSocket batch flush rate in `stream.ts`), so `toArray()` is called 30 times/sec instead of 2,000 times/sec

**Test:**
- Open CAN Monitor tab with sim running at 2,000 FPS for 60 seconds
- Monitor browser DevTools Performance tab: GC events should drop by 90%+
- Verify CAN Monitor still displays frames smoothly with no visible lag

---

### 9B. Replace `recentFrameRate` with Sliding Window Counter

**File:** `ui/src/stores/can.ts`

**Problem:** The `recentFrameRate` derived store runs `.filter()` over all 1,000 frames on every store update to count frames within the last 5 seconds. This is O(n) on every frame.

**Changes:**
- Replace with a simple counter: `let frameCount = 0` + `setInterval(() => { recentFps.set(frameCount); frameCount = 0; }, 1000)`
- Increment `frameCount` in `ingestMessage()` — O(1) per frame

**Test:**
- Verify FPS display matches actual WebSocket message rate (within ±5%)
- No performance regression on CAN Monitor tab

---

### 9C. Controller Game Loop Precision (Optional)

**File:** `ui/src/components/Controller.svelte`

**Problem:** Uses `setInterval(tick, 20)` for 50Hz CAN command transmission. Browsers throttle `setInterval` to 1Hz when the tab is backgrounded. The `visibilitychange` dead-man's switch mitigates the safety risk, but the timing is still imprecise when the tab IS focused (browser can batch setTimeout/setInterval calls).

**Changes:**
- Switch to `requestAnimationFrame` with `performance.now()` delta calculation:
  ```typescript
  let lastTick = performance.now();
  const loop = (now: number) => {
    if (!running) return;
    if (now - lastTick >= intervalMs) {
      tick();
      lastTick += intervalMs;
    }
    rafHandle = requestAnimationFrame(loop);
  };
  ```
- Scale speed/yaw ramp rates by actual `dt` instead of assuming a fixed 20ms interval

> **Note:** This is marked optional because `setInterval` at 50Hz for CAN injection is functionally adequate — the CAN bus itself runs at much higher rates. The main benefit of rAF is smoother keyboard response when the user is actively driving via the Controller tab.

**Test:**
- Drive via WASD for 30 seconds → verify speed ramps are smooth
- Switch to another tab and back → verify controller resumes correctly
- Verify dead-man's switch still fires (zeros all outputs) when tab is backgrounded

---

## Phase 10 — API & Query Optimizations

### 10A. Pipeline Correlation Caching

**File:** `backend/src/api/can.ts`

**Problem:** `GET /api/can/pipeline` fetches 2,000 frames from SQLite and runs a JS-side correlation algorithm on every request. This is called on a polling interval by the Pipeline tab.

**Changes:**
- Cache the correlation result in memory with a 500ms TTL
- Invalidate cache when new frames are inserted (via write queue flush callback)
- Return cached result if within TTL

**Test:**
- Hit `/api/can/pipeline` 10 times in 500ms → verify only 1 actual DB query
- Verify cached result updates within 1 second of new frames arriving

---

### 10B. Missing Indexes

**File:** `backend/src/db/schema.ts`

**Changes:**
- Add `CREATE INDEX IF NOT EXISTS idx_recordings_stopped ON recordings(stopped_at)`
- Verify existing indexes cover the query patterns from Phase 5E

**Test:**
- Run `EXPLAIN QUERY PLAN` on all major queries → verify index usage

---

## Summary

| Phase | Scope | Key Deliverable | Risk |
|-------|-------|----------------|------|
| **5** | Database Performance | Prepared stmt cache, batch inserts, write queue, bulk prune | Medium — touches hot path |
| **6** | Worker Thread | SQLite in worker thread, async API layer | Medium — architectural change |
| **7** | CAN Catalog Unification | Auto-generated decode/encode from YAML, shared package | High — touches 2 critical 600-line files |
| **8** | Sim Engine Timing | High-resolution tick loop | Low — isolated to sim module |
| **9** | Frontend Polish | Store allocation fix, FPS counter, optional rAF | Low — UI only |
| **10** | API Optimization | Pipeline cache, indexes | Low — isolated queries |

### Execution Order

Phases 5 → 6 are sequential (6 depends on 5).  
Phase 7 is independent — can run in parallel with 5/6.  
Phases 8, 9, 10 are independent of each other and can run in any order after 5.

### Version Targets

| Milestone | Version |
|-----------|---------|
| Phase 5 complete | v0.4.0-beta.2 |
| Phase 6 complete | v0.4.0-beta.3 |
| Phase 7 complete | v0.4.0-rc.1 |
| Phases 8–10 complete | v0.4.0 |
