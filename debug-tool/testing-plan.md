# Debug Tool — Comprehensive Testing Plan

> Version: v0.4.0-alpha  
> Last updated: 2026-07-06  
> Purpose: Define a complete, layered test strategy so every mode, transport, and data path can be verified — both in CI without hardware and manually with real ECUs.

---

## 1. Testing Philosophy

The debug tool has three distinct failure domains, each requiring its own test strategy:

| Domain | What can go wrong | Test type |
|--------|-------------------|-----------|
| **Backend logic** | DB mutations, frame decoding, stats staleness, recording integrity | Unit + Integration (Vitest, in-process) |
| **Transport bridges** | Serial reconnect, CANalyst-II stdin, MQTT routing, bus detection | Integration (fake bridge + loopback) |
| **UI behaviour** | Tab state, store reactivity, filter race, frame rendering | Component (Vitest + jsdom) + E2E (Playwright) |
| **End-to-end flow** | Full pipeline: inject → CAN bus → UI display | E2E + Hardware-in-the-loop (HIL) |

**Golden rule:** Tests at a lower layer are faster and more reliable. Only escalate to a higher layer when lower-layer tests cannot cover the behaviour.

---

## 2. Current Test Coverage (Baseline)

| File | Framework | Status | What is covered |
|------|-----------|--------|----------------|
| `ui/src/stores/can.test.ts` | Vitest | ✅ 16 tests | Frame ring buffer, `ingestMessage`, `latestById`, `recentFrameRate` |
| `e2e/tests/debug-tool.spec.ts` | Playwright | ✅ 10 tests | Page load, tab navigation, API shape |
| `e2e/tests/mcp2515-high-bus.spec.ts` | Playwright | ⚠️ partial | Hardware-dependent, skipped in CI |
| `backend/tests/` | — | ❌ empty | No backend unit tests at all |

### Critical gaps

- **Zero backend unit tests**: `queries.ts`, `decodeFrame()`, `BusDetector`, all API routes are completely untested
- **No transport-layer tests**: No test for SerialBridge reconnect, CanalystBridge stdin, or MQTT routing
- **No staleness tests**: BUG-01, BUG-02, BUG-13 have no automated regression coverage
- **No injection round-trip tests**: No test verifying that `/api/cmd/send` → bridge → `cmd_ack` → DB status update works correctly
- **No recording lifecycle tests**: start → frames attach → stop → replay is untested

---

## 3. Layer 1 — Backend Unit Tests (Vitest, no hardware)

**Location:** `backend/src/**/__tests__/` (co-located) or `backend/tests/unit/`  
**Run command:** `npm test` (already wired to `vitest run`)  
**Requirement:** Must pass with zero hardware, zero network, zero external processes.

### 3.1 `DebugStore` (queries.ts)

Use `better-sqlite3` in `:memory:` mode. All tests should be fast (<5ms each).

```
describe("DebugStore — frame lifecycle")
  ✅ insertFrame returns StoredCanFrame with row_id
  ✅ insertFrame stores decoded JSON correctly
  ✅ queryFrames returns frames ordered newest-first
  ✅ queryFrames filters by bus
  ✅ queryFrames filters by id
  ✅ queryFrames filters by since timestamp
  ✅ queryFrames respects limit cap (max 5000)
  ✅ latestById returns exactly one entry per (bus, id)
  ✅ latestById returns the chronologically newest frame per key

describe("DebugStore — pruning")
  ✅ insertFrame does NOT prune when count < maxFrames
  ✅ insertFrame prunes to maxFrames when limit is exceeded
  ✅ pruneFrames skips frames referenced by active recordings (BUG-20 regression)
  ✅ pruneFrames deletes frames from stopped recordings (BUG-20 fix)
  ✅ at 1000 FPS simulation: pruning must complete in <100ms per batch (performance)

describe("DebugStore — stats staleness")
  ✅ getStats returns defaultStats when no stats have been set
  ✅ getStats returns defaultStats when stored stats are >5s stale (BUG-01 regression)
  ✅ getStats returns stored stats when fresh (<5s old)
  ✅ setStats followed by getStats round-trips correctly

describe("DebugStore — recordings")
  ✅ startRecording returns a recording with stopped_at = null
  ✅ frames inserted while recording is active attach to it
  ✅ frames inserted after stopRecording do NOT attach
  ✅ stopRecording sets stopped_at and freezes frame_count
  ✅ stopRecording twice does not overwrite stopped_at (COALESCE guard)
  ✅ deleteRecording removes recording and its recording_frames rows
  ✅ clearFrames resets frame_count to 0 on all recordings (BUG-28 regression)
  ✅ recordingFramesById returns null for non-existent id
  ✅ recordingFramesById returns empty array after clearFrames (BUG-28)

describe("DebugStore — injection tracking")
  ✅ insertInjection stores with status "queued"
  ✅ updateInjectionByCorrelation updates the correct row (not "latest")
  ✅ updateInjectionByCorrelation falls back to latest when correlationId not found
  ✅ updateLatestInjectionStatus updates the most recently inserted row
  ✅ concurrent insertInjection + updateLatestInjectionStatus race (BUG-21 regression)
```

### 3.2 `decodeFrame()` and `normalizeFrame()` (types/can.ts)

```
describe("decodeFrame")
  ✅ 0x001 SAFETY_ESTOP returns empty object (DLC=0)
  ✅ 0x011 SYS_SAFETY_STS decodes estop_active, heartbeat_ok, light bits correctly
  ✅ 0x120 SYS_THROTTLE_STS decodes signed i16 BE speed correctly (including negative)
  ✅ 0x300 HOST_DRIVE_CMD decodes i32 speed, i24 yaw, gear correctly
  ✅ 0x210 RT_STATE_RPT decodes mode, safety_state nibble, estop_reason nibble
  ✅ 0x169 VCU_SES_REQ decodes rolling counter and checksum
  ✅ 0x721 SEB_STATUS decodes all bit-packed fields correctly
  ✅ unknown ID returns { bus } with no crash
  ✅ all 37 messages have DLC matching shared YAML (cross-check — BUG-12 regression)

describe("normalizeFrame")
  ✅ pads data array to 8 bytes
  ✅ truncates data array to dlc bytes in output
  ✅ calls decodeFrame when decoded is not provided
  ✅ uses provided decoded when present (skips re-decode)
  ✅ sets ts to Date.now() when ts is not provided

describe("BusDetector")
  ✅ returns "high" by default before any frames
  ✅ locks to "high" after 3 HIGH_UNIQUE_IDS frames
  ✅ locks to "low" after 3 LOW_UNIQUE_IDS frames
  ✅ stays locked after lock (more frames do not change result)
  ✅ resets correctly after reset()
  ✅ does not lock when both high and low IDs are seen (ambiguous)

describe("normalizeBus")
  ✅ "low" → "low"
  ✅ "high" → "high"
  ✅ "HIGH" → warns and returns "high" (BUG-30 regression)
  ✅ null → warns (BUG-30 regression)
  ✅ undefined → warns (BUG-30 regression)
```

### 3.3 API Routes (via Fastify `inject()`)

Use Fastify's built-in `app.inject()` — zero HTTP, zero network, pure in-process.

```
describe("GET /api/can/ids")
  ✅ returns 200 with all 37 messages
  ✅ every message has { bus, id, name, dlc, sender, injectable, fields }

describe("GET /api/can/frames")
  ✅ returns 200 with empty array when no frames inserted
  ✅ filters by bus query param
  ✅ filters by id query param
  ✅ filters by since query param
  ✅ respects limit query param

describe("POST /api/cmd/send")
  ✅ returns 400 when bus is invalid
  ✅ returns 400 when id is not injectable
  ✅ returns 400 when data length != dlc
  ✅ returns 400 for ESTOP without confirm_estop=true
  ✅ returns 200 "queued" for valid injectable frame
  ✅ calls bridge.sendCommand with correct payload
  ✅ returns 503 and updates DB to "error" when bridge throws

describe("POST /api/sim/periodic/start")
  ✅ creates a timer in __simTimers map
  ✅ replaces existing timer for same key (no timer leak)
  ✅ each tick calls store.insertFrame and hub.broadcast

describe("POST /api/sim/periodic/stop")
  ✅ clears the timer and removes from map
  ✅ does not crash when stopping non-existent timer

describe("GET /api/can/pipeline")
  ✅ returns empty chains when no frames
  ✅ returns a chain when 0x300 trigger + 0x204 response are within window
  ✅ does not match 0x204 outside the correlation window (200ms)
  ✅ returns at most 10 chains (slice(-10))

describe("POST /api/recordings")
  ✅ creates recording with null stopped_at
  ✅ frames inserted during active recording are linked
  ✅ stop recording returns updated stopped_at

describe("GET /api/status")
  ✅ returns backend_online=true
  ✅ includes websocket_clients count
  ✅ includes storage.frames count
  ✅ bus_stats includes both high and low buses
```

---

## 4. Layer 2 — Transport Bridge Integration Tests

**Location:** `backend/tests/integration/`  
**Framework:** Vitest + fake streams (no real hardware)  
**Requirement:** Must pass in CI. Uses in-process fake data pipes, not real serial/USB.

### 4.1 SerialBridge — Fake Serial Port

Inject a fake `EventEmitter` that mimics `SerialPort`. Feed it controlled JSONL lines.

```
describe("SerialBridge — line parsing")
  ✅ parses a CAN frame line → insertFrame + hub.broadcast
  ✅ parses a stats line → setStats + hub.broadcast
  ✅ parses a status line → updates state.connected
  ✅ parses a cmd_ack line → updateInjectionByCorrelation
  ✅ invalid JSON line → broadcasts status warning, does not crash

describe("SerialBridge — bus detection")
  ✅ HIGH_UNIQUE_IDs frame triggers bus detection for "high"
  ✅ after 3 HIGH frames, bridge locks bus and broadcasts state
  ✅ BusDetector resets on port open (BUG-03 regression)
  ✅ broadcastStatus only fires on confidence CHANGE, not every frame (BUG-23 regression)

describe("SerialBridge — reconnect")
  ✅ port close triggers scheduleReconnect
  ✅ reconnect destroys old port before opening new one (BUG-26 regression)
  ✅ reconnect uses exponential backoff delays
  ✅ stops reconnecting after MAX_RECONNECT_ATTEMPTS
  ✅ reconnectAttempt resets to 0 on successful open

describe("SerialBridge — sendCommand")
  ✅ writes JSON line to port when port is open
  ✅ throws when port is not open
```

### 4.2 CanalystBridge — Fake Python Process

Use `child_process.spawn` with a real Python echo script, or a fake `ChildProcess` stub.

```
describe("CanalystBridge — process lifecycle")
  ✅ spawnProcess starts with correct env vars (CANALYST_BITRATE, CH0_BUS, etc.)
  ✅ stdout lines are parsed as JSONL (same tests as SerialBridge line parsing)
  ✅ stderr warnings broadcast to UI but don't crash
  ✅ process exit with code != 0 triggers scheduleReconnect
  ✅ process exit with code 0 does NOT trigger reconnect

describe("CanalystBridge — sendCommand")
  ✅ writes JSON line to process.stdin when link_open=true
  ✅ throws when process is null or stdin not writable
  ✅ Python bridge actually processes the stdin command (BUG-27 regression — requires canalystii_bridge.py fix)

describe("CanalystBridge — waitForConnection")
  ✅ resolves true when process emits connected status before timeout
  ✅ resolves false when process exits with error before timeout
  ✅ resolves current state on timeout (does not hang forever)
```

### 4.3 MqttBridge — Loopback MQTT

Start an actual aedes broker in-process. Connect a test MQTT client and publish frames.

```
describe("MqttBridge — frame ingestion via MQTT")
  ✅ publish to etrike/debug/can/rx/high → frame inserted and broadcast
  ✅ publish to etrike/debug/can/stats → stats updated
  ✅ publish to etrike/debug/status → bridge.state.connected updated
  ✅ publish to etrike/debug/cmd/response → updateInjectionByCorrelation called
  ✅ publish with invalid JSON → warning broadcast, no crash

describe("MqttBridge — sendCommand")
  ✅ publishes command to etrike/debug/cmd/send topic at QoS 1
  ✅ throws when broker is null
```

### 4.4 StreamHub — WebSocket

Use Fastify's `inject()` WebSocket helper.

```
describe("StreamHub — WebSocket")
  ✅ new client receives { type: "status", payload: { connected: true } } on connect
  ✅ new client receives { type: "can_ids", ... } catalog on connect
  ✅ broadcast can_frame reaches client with no filter set
  ✅ broadcast can_frame is filtered out when client set bus filter
  ✅ broadcast can_frame is filtered out when client set id filter
  ✅ client filter message { type: "filter", buses: ["low"] } updates bus filter
  ✅ invalid filter JSON sends warning, does not crash
  ✅ client connection closes → removed from clients set (no memory leak)
  ✅ connection limit: 101st client is rejected with 1013
  ✅ zombie client (no pong >60s) is terminated (stale timer test)
```

---

## 5. Layer 3 — UI Component Tests (Vitest + jsdom)

**Location:** `ui/src/**/__tests__/` or `ui/src/**/*.test.ts`  
**Framework:** Vitest + `@testing-library/svelte` (add to devDeps)  
**Requirement:** Must pass in CI. No Playwright, no browser, no backend.

```
describe("Stores — latestById incremental update (BUG-24 fix)")
  ✅ updating one frame does not trigger full O(n) re-scan
  ✅ adding frame with new key inserts into map without overwriting others
  ✅ adding frame with existing key replaces only that key

describe("Stores — ecuPresence staleness (BUG-02 fix)")
  ✅ ECU with frame older than 3s shows as absent
  ✅ ECU with frame newer than 3s shows as present
  ✅ staleness threshold updates reactively as time passes

describe("Stores — telemetry derived values")
  ✅ motorSpeedKmh converts mm/s to km/h correctly (1000 mm/s → 3.6 km/h)
  ✅ steerAngleDeg clamps to [-90, 90] range
  ✅ brakePressureMpa converts kPa → MPa correctly
  ✅ gear falls back correctly when motor frame absent
  ✅ estopActive reads from high bus safety frame first

describe("Stores — fault decoder")
  ✅ SES fault bits decode to named fields correctly
  ✅ SEB fault bits decode to named fields correctly
  ✅ l3_fault is true when any L3 bit is set

describe("CAN Decoder — UI-side")
  ✅ decodeFrame output matches backend decodeFrame output for all 37 messages
  ✅ encodePayload for HOST_DRIVE_CMD round-trips through decodeFrame
  ✅ normalizeCanId handles hex prefix, uppercase, decimal input consistently
```

---

## 6. Layer 4 — End-to-End Tests (Playwright, disabled transport)

**Location:** `e2e/tests/`  
**Transport mode:** `CAN_TRANSPORT=disabled` + software injection via `/api/sim/inject`  
**Requirement:** Must pass in CI without hardware.

### 6.1 Mode: Monitor Only (software injection)

```
describe("Monitor tab — software injection")
  ✅ inject 0x300 HOST_DRIVE_CMD via /api/sim/inject → frame appears in CAN Monitor
  ✅ injected frame shows correct decoded speed value
  ✅ switching to Monitor tab while frames arrive does NOT wipe frame list (BUG-04)
  ✅ switching back to Monitor tab restores scroll position
  ✅ bus filter "High only" hides low-bus frames
  ✅ ID filter for 0x300 shows only that message

describe("Dashboard — ECU presence staleness")
  ✅ inject RT heartbeat (0x7FD) → RT shows green in health bar
  ✅ wait 4s without heartbeat → RT shows absent/gray (BUG-02 fix)
  ✅ inject again → RT shows green within 1s

describe("Statistics tab — bus staleness")
  ✅ inject frames → statistics show non-zero FPS
  ✅ stop injecting → after 5s, FPS drops to 0 (BUG-01 fix)

describe("Pipeline tab")
  ✅ inject 0x300 then 0x204 within 200ms → pipeline shows linked chain
  ✅ inject 0x300 then 0x204 after 500ms → pipeline shows NO chain (out of window)
```

### 6.2 Mode: Injector Tab

```
describe("Injector — one-shot injection")
  ✅ select template "Host drive 2.0 m/s" → fields pre-filled
  ✅ click Send → frame appears in injection history with status "queued"
  ✅ ESTOP injection without confirm_estop checkbox → blocked, error shown
  ✅ ESTOP injection WITH confirm_estop → allowed

describe("Injector — periodic injection")
  ✅ start periodic 0x7FC every 500ms → heartbeat visible in Monitor
  ✅ stop periodic → no more frames for that ID in Monitor
  ✅ switch to Monitor and back → periodic injection still running (BUG-04 + BUG-18 fix)

describe("Injector — DLC validation")
  ✅ submit frame with data.length != dlc → shows validation error
  ✅ submit frame with invalid bus → shows error
```

### 6.3 Mode: Controller Tab

```
describe("Controller — keyboard drive")
  ✅ press W → HOST_DRIVE_CMD appears in Monitor with positive speed
  ✅ press S → HOST_DRIVE_CMD has negative speed (reverse)
  ✅ press A → non-zero yaw_rate_mrad_s
  ✅ release all keys → speed/yaw drop to 0 in next frame
  ✅ ESTOP active in dashboard → controller sends stop command
```

### 6.4 Mode: Unit Test Tab

```
describe("Unit Test tab — profiles")
  ✅ at least one profile button visible
  ✅ clicking a profile starts software injection with the profile frames
  ✅ Monitor shows frames for the active profile
  ✅ stopping the profile stops injection
```

### 6.5 Full Pipeline — inject → decode → UI

```
describe("Full data path (software)")
  ✅ POST /api/sim/inject → frame in DB (GET /api/can/frames)
  ✅ POST /api/sim/inject → frame broadcast over WebSocket → UI frames store updated
  ✅ POST /api/sim/inject 0x011 with estop_active=1 → Dashboard ESTOP indicator red
  ✅ POST /api/sim/inject 0x7FD (RT heartbeat) → RT row green in health bar
  ✅ POST /api/sim/periodic/start → frames accumulate in /api/can/frames over 2s
  ✅ DELETE /api/can/frames → /api/can/frames returns empty, recordings.frame_count = 0 (BUG-28)
```

---

## 7. Layer 5 — Hardware-in-the-Loop (HIL) Tests

**Requirement:** Requires physical hardware. Run manually or on HIL CI rig. Skipped in normal CI.  
**Transport mode:** `CAN_TRANSPORT=canalystii` with real device.

### 7.1 CANalyst-II Transport Tests

```
Manual checklist:
  [ ] backend starts with CANalyst-II connected → auto-detected (logs "CANalyst-II auto-detected")
  [ ] backend starts WITHOUT CANalyst-II → falls back to serial in <5s (BUG-06 fix)
  [ ] CAN frames visible in Monitor within 1s of bus traffic starting
  [ ] inject HOST_DRIVE_CMD via Injector → frame visible on oscilloscope/analyzer on HIGH bus (BUG-27 fix)
  [ ] inject frame on low bus → frame visible on LOW bus channel
  [ ] unplug CANalyst-II USB → status shows disconnected within 5s, reconnects when re-plugged
  [ ] bus stats show zero FPS within 5s of bus silence (BUG-01 fix)
```

### 7.2 Serial (ESP32) Transport Tests

```
Manual checklist:
  [ ] ESP32 connected on COM port → frames appear in Monitor
  [ ] bus auto-detection: high-bus-only frames → BusDetector shows "high" with confidence "high"
  [ ] unplug ESP32 → status shows disconnected
  [ ] re-plug ESP32 → auto-reconnects without backend restart (BUG-26 fix)
  [ ] inject frame via Injector → ESP32 logs confirm receipt
  [ ] 5-minute disconnect → backend still reconnects (BUG-05/15 fix: no 10-attempt cap)
```

### 7.3 Hybrid Mode — Physical + Emulated

```
Manual checklist:
  [ ] connect only RT controller → SYS/MTR absent → health bar shows RT green, SYS/MTR gray
  [ ] start SYS emulation → SYS heartbeat (0x7FE) visible on bus from tool
  [ ] RT reports heartbeat_ok = 1 after SYS emulation starts
  [ ] unplug RT → RT goes absent → emulation starts automatically
  [ ] bus load matches expected frame rates from YAML cycle_ms values
```

---

## 8. Test Infrastructure Setup

### 8.1 Backend — Add Vitest test runner structure

```
debug-tool/backend/
  tests/
    unit/
      db.test.ts          ← DebugStore (queries.ts)
      can-types.test.ts   ← decodeFrame, normalizeFrame, BusDetector
      api-can.test.ts     ← GET/DELETE /api/can/* routes
      api-cmd.test.ts     ← POST /api/cmd/* routes
      api-sim.test.ts     ← POST /api/sim/* routes
      api-recordings.test.ts
    integration/
      serial-bridge.test.ts
      canalyst-bridge.test.ts
      mqtt-bridge.test.ts
      stream-hub.test.ts
    helpers/
      make-app.ts         ← builds a Fastify instance with :memory: DB for tests
      fake-bridge.ts      ← stub HardwareBridge that records sendCommand calls
      fake-serial.ts      ← EventEmitter that mimics SerialPort for SerialBridge tests
```

Required additions to `backend/package.json`:
```json
"devDependencies": {
  "@vitest/coverage-v8": "^4.x",
  "supertest": "^7.x"
}
```

Run: `npm test` (already configured to `vitest run`)

### 8.2 UI — Add `@testing-library/svelte`

```
debug-tool/ui/
  src/
    stores/
      can.test.ts           ← already exists
      telemetry.test.ts     ← NEW
      faults.test.ts        ← NEW
    lib/
      can-decoder.test.ts   ← NEW — cross-check against backend decodeFrame output
```

Required additions to `ui/package.json`:
```json
"devDependencies": {
  "@testing-library/svelte": "^5.x",
  "@testing-library/jest-dom": "^6.x",
  "vitest": "^4.x",
  "jsdom": "^25.x"
}
```

### 8.3 E2E — Playwright test helpers

Add a shared fixture `e2e/fixtures/backend.ts` that:
1. Starts the backend with `CAN_TRANSPORT=disabled` and `DB_PATH=:memory:` before tests
2. Tears it down after
3. Exposes `injectFrame(id, bus, data)` helper using `/api/sim/inject`

```typescript
// e2e/fixtures/backend.ts
import { test as base, request } from "@playwright/test";
import { spawn } from "child_process";

export const test = base.extend({
  backendUrl: async ({}, use) => {
    const proc = spawn("npm", ["run", "dev"], {
      cwd: "../backend",
      env: { ...process.env, CAN_TRANSPORT: "disabled", DB_PATH: ":memory:", PORT: "3099" }
    });
    await new Promise(r => setTimeout(r, 1500)); // wait for boot
    await use("http://127.0.0.1:3099");
    proc.kill();
  }
});
```

### 8.4 CI Pipeline (GitHub Actions / local `npm test`)

```yaml
# .github/workflows/debug-tool.yml
jobs:
  backend-tests:
    steps:
      - run: npm ci && npm test
        working-directory: debug-tool/backend

  ui-tests:
    steps:
      - run: npm ci && npm test
        working-directory: debug-tool/ui

  e2e-tests:
    steps:
      - run: npm ci && npx playwright install --with-deps chromium
        working-directory: debug-tool/e2e
      - run: npx playwright test
        working-directory: debug-tool/e2e
        env:
          CAN_TRANSPORT: disabled
          DB_PATH: ":memory:"
```

---

## 9. Regression Tests for Known Bugs

Every P0 and P1 bug in `bugs.md` must have a corresponding automated regression test before the fix is considered done. Map:

| Bug | Regression test location | Key assertion |
|-----|--------------------------|---------------|
| BUG-01 (stale FPS) | `unit/db.test.ts` | `getStats()` returns zeros when stats are >5s old |
| BUG-02 (stale ECU presence) | `ui/stores/telemetry.test.ts` | Frame older than 3s → ECU absent |
| BUG-04 (tab state wipe) | `e2e/tests/tab-state.spec.ts` | Filter survives tab switch |
| BUG-12 (simulator DLC) | `unit/can-types.test.ts` | `decodeFrame` DLC matches YAML for all 37 messages |
| BUG-19 (prune bottleneck) | `unit/db.test.ts` | 1000 inserts complete in <500ms |
| BUG-20 (WAL leak) | `unit/db.test.ts` | Stopped recording frames are prunable |
| BUG-21 (injection race) | `unit/api-cmd.test.ts` | Concurrent injections: correct row updated |
| BUG-23 (WS flood) | `integration/serial-bridge.test.ts` | `broadcastStatus` called once on lock-in, not per-frame |
| BUG-24 (O(n) scan) | `ui/stores/telemetry.test.ts` | `latestById` update: only target key changes |
| BUG-26 (double open) | `integration/serial-bridge.test.ts` | Second `start()` destroys old port first |
| BUG-27 (stdin ignored) | `integration/canalyst-bridge.test.ts` | `sendCommand` → Python stdin handler receives JSON |
| BUG-28 (clearFrames count) | `unit/db.test.ts` | `clearFrames` sets `frame_count = 0` on all recordings |

---

## 10. Test Execution Summary

| Command | What runs | CI? | Hardware? |
|---------|-----------|-----|-----------|
| `cd backend && npm test` | Layer 1 + 2 (unit + integration) | ✅ | ❌ |
| `cd ui && npm test` | Layer 3 (component) | ✅ | ❌ |
| `cd e2e && npx playwright test` | Layer 4 (E2E, disabled transport) | ✅ | ❌ |
| `cd e2e && npx playwright test --tag @hil` | Layer 5 (HIL) | ❌ manual | ✅ required |
| `cd backend && npm run test:coverage` | Coverage report (target: 80%) | ✅ | ❌ |

**Priority order for implementation:**
1. `unit/db.test.ts` — most bugs live here, zero setup required
2. `unit/can-types.test.ts` — catches DLC mismatches and decoding errors
3. `unit/api-cmd.test.ts` — catches injection race
4. `integration/serial-bridge.test.ts` — catches reconnect and WS flood
5. `ui/stores/telemetry.test.ts` — catches staleness regressions in UI
6. E2E tab-state and staleness specs — catches BUG-01, BUG-02, BUG-04 end-to-end
