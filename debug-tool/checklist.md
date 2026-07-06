# Debug Tool — Bug Fix & Verification Checklist

> Track the implementation and testing status for all confirmed bugs from `bugs.md`.
> **Status legend:** `[ ]` Pending | `[/]` In Progress | `[x]` Fixed & Tested

## P0 — Critical Issues

- [x] **BUG-01:** Disconnected bus still shows FPS and "active" in UI
  - [x] Implementation: In `App.svelte`, update the `stats` store when `/api/status` returns `bus_stats`.
  - [ ] Testing: Disconnect hardware, verify UI FPS drops to 0 and health dot turns gray after 5s.
- [x] **BUG-02:** Disconnected ECU shows "ready" in health bar
  - [x] Implementation: Add 3-second staleness check in `ecuPresence` store.
  - [ ] Testing: Power off ECU or stop emulator, verify health status drops from "ready".
- [x] **BUG-12:** DLC mismatch corrupts injection
  - [x] Implementation: Audit and unify DLC definitions in `CAN_MESSAGES` and simulator profiles.
  - [ ] Testing: Run simulator test suite, verify no 400 Bad Request errors on injection.
- [x] **BUG-19:** Synchronous Database Pruning Bottleneck
  - [x] Implementation: Batch `pruneFrames()` deletions or move to a periodic timer.
  - [ ] Testing: High-FPS injection test (1000 FPS), verify event loop does not block when count > 50,000.
- [x] **BUG-20:** Indefinite Database Growth (Memory Leak in WAL)
  - [x] Implementation: Add manual retention limit or bulk delete for stopped recordings.
  - [ ] Testing: Stop 5 large recordings, verify older recordings can be cleanly purged from disk.
- [x] **BUG-23:** WebSocket Broadcast Flood on Bus Lock-in
  - [x] Implementation: Only broadcast status when confidence *transitions* to "high".
  - [ ] Testing: Connect bus, verify WebSocket does not receive 1000 status broadcasts per second.
- [x] **BUG-31:** Python Bridge Busy-Polls at 200Hz
  - [x] Implementation: Implement adaptive polling interval in `canalystii_bridge.py`.
  - [ ] Testing: Monitor Python process CPU usage on idle bus; verify it drops to near 0%.
- [x] **BUG-32:** Python Bridge `by_id` Dictionary Memory Leak
  - [x] Implementation: Cap `by_id` dictionary to the top 100 most frequent IDs inside `emit_stats`.
  - [ ] Testing: Inject 500 random CAN IDs, verify `by_id` payload size is capped and memory is stable.
- [x] **BUG-43:** `cmd.ts` Error Path Uses `updateLatestInjectionStatus`
  - [x] Implementation: Use `updateInjectionByCorrelation` in catch blocks.
  - [ ] Testing: Trigger concurrent injection errors, verify the correct DB row is marked as "error".
- [ ] **BUG-46:** `sim.ts` Hub Access Broken
  - [ ] Implementation: Pass `hub` explicitly to `registerSimRoutes`.
  - [ ] Testing: Start emulator, verify frames are broadcasted over WebSocket to the UI.

---

## P1 — Wrong Behavior

### Backend & DB
- [ ] **BUG-10:** `SERIAL_PORT` Default is Windows-Only
  - [ ] Implementation: Add OS detection for default port (COM3 vs /dev/ttyUSB0).
  - [ ] Testing: Boot on Linux/macOS without env vars, verify reasonable default is used.

- [ ] **BUG-15:** Serial Bridge Reconnection Gives Up
  - [ ] Implementation: Fall back to 30s polling instead of giving up permanently.
  - [ ] Testing: Unplug serial, wait 5 minutes, plug back in, verify it reconnects.
- [x] **BUG-25:** Pipeline Correlation is O(n²)
  - [x] Implementation: Use binary search or pre-indexed lookup for correlation.
  - [ ] Testing: Load Pipeline tab with 2000 frames, verify response time < 50ms.
- [x] **BUG-28:** `clearFrames()` Leaves Stale Counts
  - [x] Implementation: `UPDATE recordings SET frame_count = 0` on clear.
  - [ ] Testing: Delete frames, verify recordings list shows 0 frames.
- [x] **BUG-37:** `attachToActiveRecordings` Runs 3 SQL Statements Per Frame
  - [x] Implementation: Cache active recording IDs in a `Set<number>`.
  - [ ] Testing: Profile DB inserts during active recording, verify `SELECT` count drops.
- [x] **BUG-40:** `stopRecording()` Returns Inaccurate Count
  - [x] Implementation: Compute exact `COUNT(*)` in the return query.
  - [ ] Testing: Stop recording under load, verify returned count matches actual DB rows.
- [ ] **BUG-45:** `findMessage` Fallback Ignores Bus
  - [ ] Implementation: Remove bus-agnostic fallback in `CAN_MESSAGES.find`.
  - [ ] Testing: Look up identical IDs on different buses, verify correct definitions are returned.
- [ ] **BUG-47:** `broadcast()` Mutates Set During Iteration
  - [ ] Implementation: Snapshot `clients` array before iterating.
  - [ ] Testing: Connect/disconnect clients rapidly while broadcasting, verify no crash/skipped clients.
- [x] **BUG-48:** Serial Bridge `cmd_ack` Ignores Correlation ID
  - [x] Implementation: Use `message.correlation_id` in serial `cmd_ack` handler.
  - [ ] Testing: Mock serial `cmd_ack`, verify specific injection is updated in SQLite.

### Frontend (UI)
- [x] **BUG-03:** BusDetector locks to a bus and never unlocks
  - [x] Implementation: Add staleness timeout to BusDetector lock.
  - [ ] Testing: Swap bus physical connection, verify UI auto-switches to the new bus.
- [x] **BUG-04:** Tab switch destroys component state
  - [x] Implementation: Use CSS `display` toggling or move state to stores.
  - [ ] Testing: Set a filter, switch tabs, switch back, verify filter remains active.
- [x] **BUG-24:** `latestById` is O(n) per Frame
  - [x] Implementation: Replace `derived` store with an incrementally updated `writable`.
  - [ ] Testing: Profile UI at 1000 FPS, verify CPU usage drops significantly.
- [x] **BUG-33:** `faults.ts` State Survives Reconnects
  - [x] Implementation: Move persistent module state into `initFaultWatcher` scope.
  - [ ] Testing: Reconnect WS with an active fault, verify no spurious "fault cleared" log.
- [x] **BUG-35:** `sendZeroFrames()` Ignores `selectedBus` (Safety Risk)
  - [x] Implementation: Condition zero-frame transmission on `selectedBus`.
  - [ ] Testing: Press Stop in High Bus mode, verify no low-bus frames are sent.
- [x] **BUG-39:** REST Poll Never Suspends With WS
  - [x] Implementation: Skip `GET /api/status` if WebSocket is connected.
  - [ ] Testing: Verify network tab has no 3-second polling when WS is healthy.
- [ ] **BUG-41:** Periodic ESTOP Missing Confirmation Gate
  - [ ] Implementation: Enforce `confirmEstop` check on periodic start.
  - [ ] Testing: Try to start periodic ESTOP without checkbox, verify error dialog.
- [ ] **BUG-44:** `normalizeFrame` Timestamps Break Age Calculation
  - [ ] Implementation: Unify milliseconds vs seconds handling in `frameAge()`.
  - [ ] Testing: Verify live frames show realistic ages (e.g., `< 1s`) instead of `0 ms`.
