# Debug Tool Architecture

Browser-based CAN bus monitor, injector, and bench-test dashboard for the E-Trike drive-by-wire system. Four transport backends (Serial, CANalyst-II, MQTT, Simulator), TypeScript frontend (Svelte 5), SQLite frame store, REST + WebSocket API.

> Full run guide: [`run.md`](run.md). Hardware setup: [`CANALYST-II-SETUP.md`](CANALYST-II-SETUP.md).

---

## 1. Topology

```
                          ┌──────────┐
                          │  Browser │  Svelte 5 SPA — dashboard, monitor,
                          │  :5173   │  injector, controller, pipeline view
                          └────┬─────┘
                               │ REST + WebSocket (Vite proxy → :3000)
                          ┌────┴─────┐
        ┌─────────────────│ Backend  │─────────────────┐
        │                 │  :3000   │                 │
        │                 │ Fastify 5│                 │
        │                 └────┬─────┘                 │
        │                      │                       │
   ┌────┴────┐          ┌──────┴──────┐         ┌─────┴─────┐
   │ Serial  │          │ CANalyst-II │         │   MQTT    │
   │ Bridge  │          │   Bridge    │         │  Bridge   │
   │ (ESP32) │          │  (Python)   │         │ (Aedes)   │
   └────┬────┘          └──────┬──────┘         └─────┬─────┘
        │                      │                      │
   ESP32-S3 USB          CANalyst-II USB         MQTT broker
   JSON Lines             WinUSB/pyusb            :1883 (embedded)
   (115200 baud)          (dual-channel)          or remote
        │                      │                      │
   Single TWAI           Ch0 → low bus          ESP32 Wi-Fi
   (+ optional MCP2515)   Ch1 → high bus         or simulator
```

**Backend** (`backend/`): Fastify 5 REST API + WebSocket + SQLite. Normalises incoming CAN frames from the selected transport, decodes them against the CAN message catalog, stores them, and broadcasts to connected UI clients.

**Frontend** (`ui/`): Svelte 5 SPA built with Vite 6. Eight tabs — Dashboard, CAN Monitor, CAN Dictionary, Injector, Controller, Unit Test, Pipeline, Statistics. Connects via REST for initial state and WebSocket for live frame streaming.

**Simulator** (`simulator/`): MQTT publisher that generates synthetic dual-bus CAN traffic for development without hardware.

**Firmware** (`debug-esp32/`): Optional ESP32-S3 that bridges a physical CAN bus to MQTT over Wi-Fi. Independent of the main RT/SYS firmware.

---

## 2. Transport Modes

Selected via `CAN_TRANSPORT` environment variable. All bridges implement the `HardwareBridge` interface:

```typescript
interface HardwareBridge {
  readonly state: BridgeState;
  start(): void | Promise<void>;
  sendCommand(command: Record<string, unknown>): void;
  close(): Promise<void>;
}
```

### 2.1 Serial (`CAN_TRANSPORT=serial`, default)

| Setting | Default | Purpose |
|---------|---------|---------|
| `SERIAL_PORT` | `COM3` | USB CDC ACM serial port |
| `SERIAL_BAUD` | `115200` | Baud rate |

Opens a Node.js `SerialPort`, pipes through `ReadlineParser` (newline-delimited JSON). Each line is one CAN frame, stats object, or status message. Auto-reconnect with exponential backoff (max 10 attempts, 30 s cap).

**Protocol**: The ESP32 firmware sends JSON Lines:
```json
{"ts":1718400000.123,"id":"0x204","dlc":5,"data":[0,0,7,208,1]}
{"type":"stats","ts":1718400001.0,"uptime_s":3600,"buses":{...}}
{"type":"status","esp32_connected":true}
{"type":"cmd_ack","status":"ok","cmd":"send"}
```

**Bus auto-detection**: When frames lack an explicit `bus` field, `BusDetector` identifies the bus by tracking unique CAN IDs. High-only IDs (0x300, 0x210, 0x220) → lock to "high". Low-only IDs (0x169, 0x201, 0x204) → lock to "low". Three sightings required for lock. Defaults to "high" until detected.

### 2.2 CANalyst-II (`CAN_TRANSPORT=canalystii`)

| Setting | Default | Purpose |
|---------|---------|---------|
| `CANALYST_PYTHON` | `python` | Python interpreter |
| `CANALYST_BITRATE` | `500000` | CAN bitrate (both channels) |
| `CANALYST_POLL_MS` | `5` | Poll interval |
| `CANALYST_DEVICE_INDEX` | `0` | Device index |
| `CANALYST_CH0_BUS` | `low` | Channel 0 bus assignment |
| `CANALYST_CH1_BUS` | `high` | Channel 1 bus assignment |

**Architecture**: TypeScript backend spawns `canalystii_bridge.py` as a child process. Communication via stdin/stdout JSON Lines:

```
Backend (Node.js)                    Bridge (Python)
      │                                    │
      │── {"cmd":"send","bus":"low",...}──→│  stdin  → send_frame()
      │                                    │
      │←─ {"ts":...,"bus":"low",...}──────│  stdout ← receive_channel()
      │←─ {"type":"stats",...}────────────│  stdout ← emit_stats()
      │←─ {"type":"cmd_ack",...}──────────│  stdout ← handle_command()
```

The Python bridge uses the `canalystii` package (pyusb) with WinUSB driver (installed via Zadig). Periodic injection is managed by the Python bridge's own timer loop, independent of Node.js event-loop jitter.

### 2.3 MQTT (`CAN_TRANSPORT=mqtt`)

| Setting | Default | Purpose |
|---------|---------|---------|
| `MQTT_PORT` | `1883` | Embedded broker port |

Backend runs an **embedded Aedes MQTT broker** — no external broker required.

| Topic | Direction | Content |
|-------|-----------|---------|
| `etrike/debug/can/rx/+/+` | Device → Backend | CAN frames |
| `etrike/debug/can/stats` | Device → Backend | Per-bus statistics |
| `etrike/debug/status` | Device → Backend | Device online status |
| `etrike/debug/cmd/send` | Backend → Device | One-shot injection |
| `etrike/debug/cmd/send/periodic` | Backend → Device | Periodic injection start/stop |
| `etrike/debug/cmd/response` | Device → Backend | Command acknowledgments |

### 2.4 Simulator (`CAN_TRANSPORT=mqtt` + `simulator/`)

Runs `SimEngine` which connects to MQTT and publishes synthetic frames on a configurable profile. Used for UI development and testing without hardware. See §9 for known limitations.

### 2.5 Disabled (`CAN_TRANSPORT=disabled`)

Backend starts with no transport. REST API is available for testing or replaying recordings.

---

## 3. CAN Message Catalog

**37 messages** (15 high-bus + 22 low-bus), hand-maintained in two identical copies:

| File | Purpose |
|------|---------|
| `backend/src/types/can.ts` | `CAN_MESSAGES[]`, `decodeFrame()`, `normalizeFrame()`, `INJECTION_TEMPLATES[]`, `BusDetector` |
| `ui/src/lib/can-decoder.ts` | Mirror catalog, `decodeFrame()`, `encodePayload()`, formatting helpers, `normalizeCanId()` |

> ⚠️ **Sync warning**: Both files cite `shared/can/can_signals.yaml` as source of truth — but that file doesn't exist. The actual sources are `shared/can/can_low.yaml` and `shared/can/can_high.yaml`. No automated sync exists. Known mismatches documented in `../tem/issues.md`.

### 3.1 High-Level CAN Bus (15 IDs)

| ID | Name | Sender | DLC | Key Decoded Fields |
|----|------|--------|-----|--------------------|
| `0x001` | SAFETY_ESTOP | any | 0 | — |
| `0x011` | SYS_SAFETY_STS | SYS (fwd) | 3 | estop_active, heartbeat_ok, light_state |
| `0x120` | SYS_THROTTLE_STS | MTR (fwd) | 2 | speed_mmps (i16 BE) |
| `0x206` | MTR_MOTOR_FBK | MTR (fwd) | 4 | actual_speed_mmps, gear_state, fault_flags |
| `0x210` | RT_STATE_RPT | RT | 4 | mode, safety_state, reversing, rx_overflow |
| `0x220` | RT_PID_RPT | RT | 6 | speed_setpoint, speed_measured, pid_output |
| `0x300` | HOST_DRIVE_CMD | Host | 8 | speed_mmps (i32 BE), yaw_rate_mrad_s (i24 BE), gear |
| `0x301` | HOST_BRAKE_REQ | Host | 4 | brake_pressure_kpa (i32 BE) |
| `0x302` | HOST_LIGHT_CMD | Host | 1 | left_turn, right_turn, brake_light, headlight |
| `0x310` | STEER_DIAG | RT | 8 | angle, fault, motor current, ECU temp |
| `0x311` | BRAKE_DIAG | RT | 8 | pressure, fault, motor current, ECU temp |
| `0x400` | HOST_OBSTACLE_DIST | Host | 4 | distance_mm (u32 BE) |
| `0x600` | SYS_DIAG_RPT | SYS (fwd) | 8 | mode, brake, hb_ok, estop, free_heap, tec, rec |
| `0x7FC` | HOST_HEARTBEAT | Host | 1 | alive_ctr |
| `0x7FD` | RT_HEARTBEAT | RT | 2 | alive_ctr + health_flags |

### 3.2 Low-Level CAN Bus (22 IDs)

| ID | Name | Sender | DLC | Key Decoded Fields |
|----|------|--------|-----|--------------------|
| `0x001` | SAFETY_ESTOP | any | 0 | — |
| `0x011` | SYS_SAFETY_STS | SYS | 3 | estop_active, heartbeat_ok, light_state |
| `0x012` | SYS_DCDC_CMD | SYS | 1 | enable |
| `0x110` | SYS_MODE_CMD | SYS | 1 | mode |
| `0x120` | SYS_THROTTLE_STS | MTR | 2 | speed_mmps |
| `0x169` | VCU_SES_REQ | RT | 8 | target_angle (i16 LE), target_speed, rolling_counter |
| `0x201` | SES_STATUS | EPS-C | 8 | angle_status, str_angle (i16 LE), torque, error_status |
| `0x202` | SES_ErrInfo | EPS-C | 8 | 25 fault flags (u32 LE mask) |
| `0x203` | SES_Version | EPS-C | 8 | sw_version, hw_version |
| `0x204` | RT_DRIVE_CMD | RT | 5 | motor_speed_mmps (i32 BE), gear |
| `0x205` | RT_BRAKE_CMD | RT | 4 | brake_pressure_kpa (i32 BE) |
| `0x206` | MTR_MOTOR_FBK | MTR | 4 | actual_speed_mmps, gear_state, fault_flags |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | 1 | light bitfield |
| `0x600` | SYS_DIAG_RPT | SYS | 8 | mode, brake, hb_ok, estop, free_heap, tec, rec |
| `0x6FA` | SES_Test | EPS-C | 8 | motor_current, ecu_temp, supply_voltage |
| `0x6FB` | SEB_Test | SEB | 8 | motor_current, ecu_temp (−40 offset), supply_voltage |
| `0x721` | SEB_STATUS | SEB | 8 | stroke_value, pressure_value, angle_value, error_status |
| `0x731` | SEB_ErrInfo | SEB | 8 | 23 fault flags (u32 LE mask) |
| `0x741` | SEB_Version | SEB | 8 | sw_version, hw_version |
| `0x7B9` | VCU_SEB_REQ | RT/SYS | 8 | stroke_req (u16 LE), pressure_req, rolling_counter |
| `0x7FD` | RT_HEARTBEAT | RT | 2 | alive_ctr + health_flags |
| `0x7FE` | SYS_HEARTBEAT | SYS | 2 | alive_ctr + health_flags |

---

## 4. Data Flow

```
CAN Transceiver (SN65HVD230 / TJA1050)
  │
  ├─→ ESP32-S3 ──→ JSON Lines over USB Serial
  │                     │
  ├─→ CANalyst-II ──→ Python bridge ──→ JSON Lines stdout
  │                     │
  └─→ MQTT ──→ Aedes subscribe       │
                           │
                ┌──────────┴──────────┐
                │   normalizeFrame()   │  ← bus detection + decode
                │   BusDetector.feed() │
                └──────────┬──────────┘
                           │
                ┌──────────┴──────────┐
                │   DebugStore         │  ← SQLite (max 50000 rows)
                │   .insertFrame()     │     WAL mode, periodic prune
                └──────────┬──────────┘
                           │
                ┌──────────┴──────────┐
                │   StreamHub          │  ← WebSocket fan-out
                │   .broadcast()       │     per-client bus/id filters
                └──────────┬──────────┘
                           │
                ┌──────────┴──────────┐
                │   Svelte Stores      │  ← frames[1000], stats, status
                │   ingestMessage()    │
                └──────────┬──────────┘
                           │
                ┌──────────┴──────────┐
                │   UI Components      │
                │   Dashboard · Monitor│
                │   Injector · Stats   │
                │   Pipeline · Dict    │
                └─────────────────────┘
```

**Per-frame processing** (backend bridges, shared logic):

1. Parse JSON line/message into raw object
2. If `type === "stats"` → normalize → store → broadcast
3. If `type === "status"` → update bridge state → broadcast
4. If `type === "cmd_ack"` → update injection DB → broadcast
5. If `id` + `data` present → auto-detect bus if needed → `normalizeFrame()` → `store.insertFrame()` → `hub.broadcast()`

---

## 5. WebSocket Protocol

Single endpoint: `GET /ws`. Up to 100 concurrent clients.

### Server → Client

```typescript
type StreamEvent =
  | { type: "can_frame";  payload: CanFrame }
  | { type: "stats";      payload: CanStats }
  | { type: "cmd_ack";    payload: object }
  | { type: "status";     payload: object }
  | { type: "can_ids";    payload: { messages: Array<{bus, id, name}> } };
```

On connect: server sends `can_ids` (full catalog) + `status` (connected). Keepalive ping every 30 s.

### Client → Server (filter)

```json
{"type": "filter", "buses": ["high"], "ids": ["0x300", "0x7FC"]}
```

Filters are AND-combined: a frame must match at least one bus AND at least one ID. Not setting a filter means all frames delivered. Re-applied automatically on reconnect.

### Client Reconnection (`ui/src/lib/ws.ts`)

Exponential backoff: 500 ms → 1 s → 2 s → ... → 10 s cap, with ±1 s jitter.

---

## 6. REST API

Base: `http://127.0.0.1:3000`. All responses JSON.

### 6.1 CAN Frames

| Method | Path | Query | Response |
|--------|------|-------|----------|
| `GET` | `/api/can/frames` | `bus`, `id`, `since`, `limit` | `{ frames: StoredCanFrame[] }` |
| `GET` | `/api/can/latest` | — | `{ latest: Record<string, CanFrame> }` |
| `DELETE` | `/api/can/frames` | — | `{ cleared: true }` |
| `GET` | `/api/can/ids` | — | `{ ids: CanMessageDef[] }` |
| `GET` | `/api/can/stats` | — | `{ stats: CanStats }` |
| `GET` | `/api/can/pipeline` | — | `{ chains: PipelineChain[] }` |

### 6.2 Injection

| Method | Path | Body / Validation |
|--------|------|-------------------|
| `POST` | `/api/cmd/send` | `{ bus, id, dlc, data, confirm_estop? }` — Zod validated |
| `POST` | `/api/cmd/periodic` | `{ action:"start"\|"stop", bus, id, dlc, data, interval_ms, count? }` |
| `GET` | `/api/templates` | Returns `INJECTION_TEMPLATES[]` |
| `GET` | `/api/cmd/history` | Returns last 50 injected frames |

**Guards**: 0x001 requires `confirm_estop: true`. Non-injectable catalog IDs return 400. DLC and data validated against `validateDataBytes()`.

### 6.3 Recordings

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/recordings` | List all |
| `POST` | `/api/recordings` | Start: `{ label? }` |
| `PUT` | `/api/recordings/:id/stop` | Stop recording |
| `GET` | `/api/recordings/:id/frames` | Get frames: `?limit=` |
| `DELETE` | `/api/recordings/:id` | Delete |

### 6.4 System

| Method | Path | Response |
|--------|------|----------|
| `GET` | `/api/status` | Bridge state, uptime, bus detection, storage counts, WS clients |
| `POST` | `/api/system/stop` | Closes transport bridge |
| `POST` | `/api/system/restart` | Closes + restarts bridge |
| `POST` | `/api/system/shutdown` | Graceful shutdown (200ms delay then exit) |

---

## 7. Database

SQLite via `better-sqlite3` (synchronous API, WAL mode). Path: `debug-tool/backend/data/debug-tool.sqlite`.

### Schema

```sql
can_frames (id INTEGER PK, ts_real REAL, ts_device INTEGER,
  bus TEXT CHECK('high','low'), can_id TEXT, can_name TEXT,
  dlc INTEGER, data BLOB, decoded TEXT)
  -- INDEX (bus, can_id, ts_real), INDEX (ts_real)

injected_frames (id INTEGER PK, ts_real REAL, bus TEXT,
  can_id TEXT, dlc INTEGER, data BLOB, status TEXT, correlation_id TEXT)
  -- INDEX (ts_real), INDEX (correlation_id)

recordings (id INTEGER PK, label TEXT, started_at REAL,
  stopped_at REAL, frame_count INTEGER DEFAULT 0)

recording_frames (recording_id INTEGER FK, frame_id INTEGER FK,
  PRIMARY KEY (recording_id, frame_id))

runtime_state (key TEXT PK, value TEXT)  -- key-value for stats
```

**Pruning**: When `can_frames` exceeds `MAX_FRAMES` (default 50000), oldest rows not referenced by active recordings are deleted. WAL checkpoint every 30 s.

---

## 8. Frontend Architecture

### 8.1 Svelte Stores (`ui/src/stores/`)

| Store | Type | Behavior |
|-------|------|----------|
| `frames` | `Writable<CanFrame[]>` | Last 1000 frames (ring-buffer via `slice(-1000)`) |
| `stats` | `Writable<CanStats>` | Latest per-bus stats |
| `status` | `Writable<Partial<BackendStatus>>` | Transport, connection, bus detection |
| `wsConnected` | `Writable<boolean>` | WebSocket state |
| `commandAcks` | `Writable<object[]>` | Last 30 command acks |
| `errorLog` | `Writable<ErrorEntry[]>` | Last 50 errors |
| `heldKeys` | `Writable<Set<string>>` | Pressed keys for keyboard controller |
| `kbEvent` | `Writable<KbEvent\|null>` | One-shot actions (ESTOP, zero-all) |
| `kbBus` | `Writable<Bus>` | Active keyboard bus (Tab toggles) |
| `latestById` | `Derived<Record<string, CanFrame>>` | Most recent per (bus, id) |

### 8.2 Tabs

| Tab | Component | Purpose |
|-----|-----------|---------|
| Dashboard | `Dashboard.svelte` | Overview: health, heartbeats, latest frame per ID |
| CAN Monitor | `CanMonitor.svelte` | Live stream, bus/id filter, pause, 7 color-coded categories |
| CAN Dictionary | `CanDictionary.svelte` | Full catalog browser with signal tables |
| Injector | `CanInjector.svelte` | Select bus→message→fields, send once or loop |
| Controller | `Controller.svelte` | WASD keyboard drive at 50 Hz |
| Unit Test | `UnitTest.svelte` | In-browser decoder test runner |
| Pipeline | `PipelineView.svelte` | Host→RT→actuator command chain correlation |
| Statistics | `Stats.svelte` | Per-bus FPS, load%, TEC/REC, top IDs |

### 8.3 Keyboard Controller

50 Hz game loop. Reads `heldKeys` reactive set each tick.

| Key | High Bus | Low Bus |
|-----|----------|---------|
| `Tab` | Toggle bus | Toggle bus |
| `W` / `S` | 0x300 speed ±2000 | 0x204 speed ±2000 |
| `A` / `D` | 0x300 yaw ±87 mrad/s | 0x169 angle ±5° |
| `B` | 0x301 brake 5000 kPa | 0x205 brake 5000 kPa |
| `Space` ×2 | 0x001 ESTOP | 0x001 ESTOP |
| `Esc` | Zero all | Zero all |

### 8.4 Pipeline View

Correlates Host commands (0x300 on high) through RT forwarding to actuator commands (0x204, 0x169 on low) and feedback (0x201). Uses 200 ms correlation window, ±50 mm/s speed tolerance, ±5° angle tolerance. Polls `/api/can/pipeline` which scans last 2000 frames from SQLite.

---

## 9. Simulator

`simulator/src/` publishes synthetic dual-bus CAN traffic via MQTT. `SimEngine` connects to broker, subscribes to command topics, and starts per-message `setInterval` timers. `can-generator.ts` produces frames with sine-wave speeds and rolling counters.

> ⚠️ **Known issues**: The simulator's DLC values, byte layouts, and message names do not match the YAML source of truth. See `../tem/issues.md` D15-D17. Use for UI development only.

---

## 10. Firmware (`debug-esp32/`)

Optional ESP32-S3 that bridges physical CAN to MQTT over Wi-Fi. 8 FreeRTOS tasks:

| Task | Prio | Purpose |
|------|------|---------|
| `can_rx_a` | 5 | TWAI receive → decode queue |
| `can_rx_b` | 5 | MCP2515 receive → decode queue (optional) |
| `can_decode` | 4 | ID dispatch → JSON → MQTT publish queue |
| `mqtt_tx` | 3 | MQTT publish queue consumer |
| `cmd_rx` | 3 | MQTT subscribe → command queue |
| `can_inject` | 3 | Command queue → CAN TX |
| `stats` | 1 | 1 Hz stats aggregation |
| `status` | 2 | 5 s heartbeat |

Single TWAI controller (GPIO 4/5) + optional MCP2515 (GPIO 36–40) for dual-bus.

---

## 11. Configuration

All via environment variables, Zod-validated in `backend/src/config.ts`:

| Variable | Default | Allowed |
|----------|---------|---------|
| `HOST` | `127.0.0.1` | any |
| `PORT` | `3000` | 1–65535 |
| `CAN_TRANSPORT` | `serial` | `serial`, `canalystii`, `mqtt`, `disabled` |
| `MQTT_PORT` | `1883` | 1–65535 |
| `SERIAL_PORT` | `COM3` | any serial port |
| `SERIAL_BAUD` | `115200` | int |
| `CANALYST_BITRATE` | `500000` | int |
| `CANALYST_POLL_MS` | `5` | int |
| `CANALYST_DEVICE_INDEX` | `0` | int |
| `CANALYST_CH0_BUS` | `low` | `high`, `low` |
| `CANALYST_CH1_BUS` | `high` | `high`, `low` |
| `DB_PATH` | `data/debug-tool.sqlite` | any path |
| `MAX_FRAMES` | `50000` | int |
| `SERVE_UI` | (unset) | `true` to serve built UI statically |

---

## 12. Directory Map

```
debug-tool/
  README.md
  run.md
  start.bat / start.sh
  CANALYST-II-SETUP.md
  debug-tool-architecture.md          ← this file

  backend/
    package.json                     @etrike/debug-backend
    tsconfig.json                    ES2022, CommonJS, strict
    canalystii_bridge.py             Python bridge for CANalyst-II
    .env.example
    src/
      index.ts                       Main entry (Fastify server + shutdown)
      config.ts                      Zod config loader
      bridge/types.ts                HardwareBridge interface + BridgeState
      serial/reader.ts               SerialBridge (USB CDC ACM)
      canalyst/bridge.ts             CanalystBridge (Python child process)
      mqtt/bridge.ts                 MqttBridge (Aedes embedded broker)
      api/can.ts                     /api/can/* + pipeline correlator
      api/cmd.ts                     /api/cmd/* + injection logic
      api/recordings.ts              /api/recordings/*
      api/system.ts                  /api/system/* + /api/status
      ws/stream.ts                   StreamHub (WebSocket fan-out)
      db/schema.ts                   SQLite DDL
      db/queries.ts                  DebugStore (DAO)
      types/can.ts                   CAN catalog, decode, encode, BusDetector
      types/can.test.ts              Vitest unit tests (711 lines)

  ui/
    package.json                     @etrike/debug-ui
    vite.config.ts                   Vite 6, :5173, proxy /api + /ws → :3000
    index.html
    src/
      main.ts                        App bootstrap
      App.svelte                     Root: tabs, status bar, keyboard
      styles.css
      lib/api.ts                     REST client
      lib/ws.ts                      WebSocket client + reconnect
      lib/ws-types.ts                StreamEvent types
      lib/can-decoder.ts             CAN catalog mirror + encode/decode
      lib/can-decoder.test.ts        Vitest unit tests
      stores/can.ts                  Svelte stores (frames, stats, status)
      stores/errors.ts               Error log store
      stores/keyboard.ts             Keyboard state
      components/
        Dashboard.svelte
        CanMonitor.svelte
        CanInjector.svelte
        Controller.svelte
        Stats.svelte
        PipelineView.svelte
        CanDictionary.svelte
        SignalBox.svelte
        SignalTable.svelte
        BitGrid.svelte
        MessageCard.svelte
        UnitTest.svelte

  simulator/
    package.json                     @etrike/debug-simulator
    tsconfig.json                    ES2022, ESM
    src/
      index.ts                       CLI entry
      sim-engine.ts                  SimEngine (MQTT publisher)
      can-generator.ts               Synthetic frame generation

  debug-esp32/
    platformio.ini                   ESP32-S3 PlatformIO project
    src/main.cpp                     CAN↔MQTT bridge firmware

  e2e/
    package.json                     @etrike/debug-e2e
    playwright.config.ts
    tests/
      debug-tool.spec.ts             E2E scenarios
      mcp2515-high-bus.spec.ts       MCP2515-specific tests
```

---

## 13. Build & Run

```powershell
# Backend
cd debug-tool/backend
npm install && npm run dev     # dev (tsx --watch)
npm run build && npm start     # production

# Frontend
cd debug-tool/ui
npm install && npm run dev     # dev (:5173, proxies → :3000)
npm run build                  # → ui/dist/

# Simulator
cd debug-tool/simulator
npm install && npx tsx src/index.ts

# E2E
cd debug-tool/e2e
npm install && npx playwright test

# Production (serve built UI from backend)
cd debug-tool/backend
$env:SERVE_UI = "true"
npm start                       # serves UI from ui/dist/, SPA fallback
```

---

## 14. Reference

- [Main Architecture](../architecture.md) — system topology, message catalog, mode state machine
- [CAN Dictionary](../can-dictionary.md) — full bit-level signal catalog
- [Bench Test Plan](../docs/can-bench-test.md) — hardware setup and injection guide
- [Wiring Reference](../docs/wiring.md) — pin-level wiring for all ECUs
- [CAN YAML Sources](../shared/can/can_high.yaml) — source of truth for CAN definitions
