# Debug Tool — Known Bugs

> Generated 2026-07-06 from full codebase audit.
> Severity: **P0** = data loss / safety confusion, **P1** = wrong behavior, **P2** = cosmetic / edge case.

---

## P0 — Critical

### BUG-46: `sim.ts` Hub Access Broken

**Severity:** P0  
**Files:** `backend/src/api/sim.ts:33-34`

**Symptom:** Simulator injects frames to DB but they never appear in UI.

**Root cause:** `const hub = (app as any).__hub;` is silently undefined in tests or if initialization order changes. The `if (hub)` guard silently swallows the error.

**Fix direction:** Pass `hub: StreamHub` explicitly to `registerSimRoutes()`.

---

### BUG-56: ECU and Telemetry State Freezes on Bus Disconnect (Silent Failure)

**Severity:** P0 (Safety/Monitoring Critical)  
**Files:** `ui/src/stores/telemetry.ts:78-151`, `ui/src/stores/can.ts`

**Symptom:** If the physical CAN bus disconnects or the backend stops forwarding frames, the ECU health indicators (RT, SYS, MTR) remain permanently "ready" (green) and the vehicle state (Gear, Speed, ESTOP) freezes at its last known value instead of timing out.

**Root cause:** `ecuPresence` and `telemetry` are Svelte `derived` stores that depend on `latestById`. Svelte only recalculates derived stores when their dependencies emit new values. If the bus is silent, no frames arrive, `latestById` stops emitting, and the 3-second staleness checks inside the derived stores are never executed.

**Fix direction:** Drive staleness recalculations using an active Svelte timer store (e.g., a `now` store that ticks every 1 second) as an additional dependency in the `derived` blocks, forcing them to re-evaluate timestamps even when frames stop arriving.

---

### BUG-57: Complete API Crash Leaves UI Falsely Reporting "Healthy"

**Severity:** P0 (Safety/Monitoring Critical)  
**Files:** `ui/src/App.svelte:181-190`

**Symptom:** If the backend process crashes completely (WebSocket drops and `/api/status` returns `ERR_CONNECTION_REFUSED`), the Topbar still reports the USB Bridge as "linked", and the CAN Bus FPS / Load remains frozen at their high values forever. 

**Root cause:** When `refreshStatus()` catches a network error, the `catch` block correctly sets `backend_online = false`, but it completely forgets to mutate `$status.bridge` or zero out the `$stats` store. 

**Fix direction:** Update the `catch` block in `refreshStatus()` to explicitly set `adapter_connected: false`, `bridge: { connected: false }`, and push a zeroed `CanStats` object to the `stats` store.

### BUG-50: `Topbar.svelte` Massive `setInterval` CPU/Memory Leak

**Severity:** P0  
**Files:** `ui/src/components/Topbar.svelte:47`

**Symptom:** The browser tab quickly hangs, crashes, or consumes 100% CPU when turn signals are active.

**Root cause:** A `setInterval` is created inside a reactive block (`$: { if (...) }`) without ever being cleared. It spawns hundreds of timers per second when telemetry updates rapidly.

**Fix direction:** Clear the interval when the reactive block re-evaluates or the component is destroyed.

---

## P1 — Wrong Behavior

### BUG-10: `SERIAL_PORT` Default is Windows-Only

**Severity:** P1  
**Files:** `backend/src/config.ts:8`

**Symptom:** Silent failure on Linux/macOS because `COM3` doesn't exist.

**Root cause:** Hardcoded `COM3` default.

**Fix direction:** Platform-detect default or require explicit config.



---

### BUG-15: Serial Bridge Reconnection Gives Up

**Severity:** P1  
**Files:** `backend/src/serial/reader.ts:101-124`

**Symptom:** After 10 attempts (3 mins), serial bridge permanently stops reconnecting.

**Fix direction:** Switch to slow polling (30s) instead of giving up completely.

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

### BUG-51: `telemetry.ts` Steering Angle Scaling Error

**Severity:** P1  
**Files:** `ui/src/stores/telemetry.ts:125`

**Symptom:** The fallback high-bus steering angle is reported as 10x larger than it actually is.

**Root cause:** The math `(diagAngle * 10) / 10` is used to round the `0.1 deg` value, missing a division by 10 to convert to true degrees.

**Fix direction:** Change to `Math.round(diagAngle) / 10` or properly divide by 10.

---

### BUG-52: `telemetry.ts` Steering Angle Hard Clipping

**Severity:** P1  
**Files:** `ui/src/stores/telemetry.ts:126`

**Symptom:** The steering wheel graphic completely vanishes if turned beyond 90 degrees.

**Root cause:** Values exceeding ±90 degrees are assigned `null` rather than clamped.

**Fix direction:** Clamp the value using `Math.max(-90, Math.min(90, rawAngle))` instead of discarding it.

---

### BUG-53: `Dashboard.svelte` Timestamp Sorting Bug

**Severity:** P1  
**Files:** `ui/src/components/Dashboard.svelte:55-57`

**Symptom:** Injected frames permanently stick to the top of the "Active Frames" list.

**Root cause:** `frameStamp` mixes `ts_real` (seconds) and `ts` (milliseconds). Missing `ts_real` causes the function to return a value 1000x larger.

**Fix direction:** Normalize the timestamp before returning (divide `ts` by 1000 if it falls back).

---

## P2 — Cosmetic / Edge Cases

- **BUG-05:** Serial port fails silently (UI shows error, just no backend console log).
- **BUG-16:** CANalyst-II bridge abandoned after detection failure can't be reused.
- **BUG-18:** Emulator `simMode` toggle resets when switching tabs.
- **BUG-26:** `SerialBridge.start()` called twice on reconnect (Double open error).
- **BUG-29:** `stopRecording()` does not prevent double-stop.
- **BUG-30:** `normalizeBus()` falls through to `"high"` silently.
- **BUG-34:** `normalizeFilter` strips bus from bare IDs.
- **BUG-36:** Controller `tick()` reads `heldNow` via Svelte reactive assignment (stale closure edge case).
- **BUG-54:** `Topbar.svelte` reports the USB port state as "open" when disconnected instead of "closed" or "offline".
- **BUG-55:** `Dashboard.svelte` hardcodes the obsolete `"EPS-C"` string for steering instead of the updated `"SES"`.

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
- **BUG-06:** CANalyst-II Auto-Detection blocks startup (False alarm — Fastify listens asynchronously)
- **BUG-08:** WebSocket Reconnect Floods Unfiltered Frames (False alarm — fixed in `ws.ts:39`)
- **BUG-09:** REST `/api/status` returns stale stats (False alarm — `queries.ts:155` handles staleness)
- **BUG-11:** No Transport Hot-Swap (False alarm — implemented in `index.ts` using `bridgeProxy`)
- **BUG-17:** No `.env` loading (False alarm — `import "dotenv/config"` is at `config.ts:1`)
