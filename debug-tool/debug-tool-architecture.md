# Debug Tool Architecture

> **Version:** v0.4.0-alpha — Multi-mode CAN bench-test platform

Browser-based CAN bus monitor, injector, ECU emulator, and bench-test dashboard for the E-Trike drive-by-wire system. Four transport backends (Serial, CANalyst-II, MQTT, Simulator), six ECU behavioral models, TypeScript frontend (Svelte 5), SQLite frame store, REST + WebSocket API.

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

> ⚠️ **Sync warning**: Currently, `can.ts` and `can-decoder.ts` are hand-maintained and out of sync with the true source (`shared/can/can_low.yaml` and `shared/can/can_high.yaml`). The upcoming architecture will use a generator script (`shared/can/generate_can_index.py`) to build a single `can-index.ts` directly from these shared YAML files, ensuring the debug tool and simulator are always perfectly aligned with the firmware.

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
> ⚠️ **Pruning Bottleneck**: Currently, pruning runs synchronously on every frame insert, causing massive event loop blocking at high FPS (BUG-19). Furthermore, it fails to differentiate between active and stopped recordings, leading to indefinite WAL memory leaks (BUG-20). Fixes are pending in Phase 0.

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
                                     ⚠️ Legacy — replaced by SimulationEngine in v0.4.0

  sim/                               ← NEW in v0.4.0-alpha
    ecus/
      host.ts                        HOST drive-by-wire model
      rt.ts                          RT gateway + steering model
      sys.ts                         SYS safety + body model
      mtr.ts                         MTR motor model
      epsc.ts                        EPS-C steering actuator model
      seb.ts                         SEB brake actuator model
      base.ts                        BaseEcu abstract class
    bus/
      virtual-can.ts                 Dual-channel virtual CAN bus
    physics/
      tricycle.ts                    Tricycle kinematics
      plant.ts                       Speed/steering/brake plant model
    engine.ts                        SimulationEngine orchestrator
    router.ts                        FrameRouter — per-ID source routing
    config.ts                        WorkModeConfig types + defaults
    scenario.ts                      Scenario runner

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

## 14. Multi-Mode Architecture (v0.4.0-alpha)

The debug tool is evolving from a passive CAN monitor into a **multi-mode bench-test platform** that supports five distinct work modes. This section describes the target architecture — the patterns, components, and data flows that make each mode possible. Implementation status is noted per component.

### 14.1 Work Modes

Five modes, ordered by how much is running in software vs. hardware:

| Mode | Hardware | Software | Use Case |
|------|----------|----------|----------|
| **Full Simulation** | None | All 6 ECU models + physics | CI testing, development without hardware, scenario replay |
| **Part-by-Part Emulation** | Some ECUs detected | Missing ECUs emulated with behavioral models | Bench test with partial hardware, bring-up of individual ECUs |
| **Hybrid** | Real CAN bus active | Emulated ECUs inject onto physical bus; real frames feed emulated models | Integration testing, mixed hardware/software validation |
| **Bench Test** | Real CAN bus active | No emulation; bypass flags suppress listen-sync requirements | Testing what exists without faking what doesn't |
| **Monitor Only** | Real CAN bus active (or none) | Passive decode + display; no injection, no emulation | Field debugging, bus health checks, log capture |

**Mode selection** controls three orthogonal things:
1. **Which ECU models run** (none / missing-only / all)  
2. **Whether emulated frames inject onto the physical CAN bus** (read-only / inject)  
3. **Which bypass flags are active** (none / auto-suggested / manual)

### 14.2 Unified Frame Pipeline

Every CAN frame — regardless of source — flows through the same pipeline. The Frame Router is the central junction that decides per-ID which source provides each message.

```
                         ┌───────────────────────────────┐
                         │       Frame Router             │
                         │  per-(bus, id) source table    │
                         │  collision detection           │
                         │  duplicate suppression         │
                         └──────────────┬────────────────┘
                                        │
           ┌────────────────────────────┼────────────────────────────┐
           │                            │                            │
    ┌──────┴──────┐              ┌──────┴──────┐              ┌──────┴──────┐
    │  Physical    │              │  Emulated    │              │  Simulated   │
    │  Bridge      │              │  ECUs        │              │  ECUs        │
    │              │              │              │              │              │
    │ Serial       │              │ Per-ECU      │              │ All-ECU      │
    │ CANalyst-II  │              │ behavioral   │              │ behavioral   │
    │ MQTT         │              │ models       │              │ models       │
    │              │              │ (backend)    │              │ (backend)    │
    │ Reads from   │              │              │              │              │
    │ physical bus │              │ Subscribes   │              │ Virtual CAN  │
    │              │              │ to frames    │              │ bus only     │
    └──────┬───────┘              └──────┬───────┘              └──────┬───────┘
           │                            │                            │
           └────────────────────────────┼────────────────────────────┘
                                        │
                         ┌──────────────┴──────────────┐
                         │  Normalize → Decode → Store  │
                         │  SQLite + WebSocket fan-out   │
                         └──────────────┬──────────────┘
                                        │
                         ┌──────────────┴──────────────┐
                         │  UI (Svelte 5)               │
                         │  All tabs persistently       │
                         │  mounted                     │
                         └─────────────────────────────┘
```

**Key invariant:** A given `(bus, id)` pair has exactly one authoritative source at any time. The router enforces this — if both a physical bridge and an emulated ECU try to produce `low:0x206`, the router picks one based on mode config and logs a collision warning.

**Source priority by mode:**

| Mode | Physical bridge | Emulated ECU | Simulated ECU |
|------|:---:|:---:|:---:|
| Full Simulation | — | — | All IDs |
| Part-by-Part Emulation | — | Missing ECUs only | — |
| Hybrid | Detected ECUs | Missing ECUs (can inject to physical bus) | — |
| Bench Test | All connected ECUs | — | — |
| Monitor Only | All connected ECUs | — | — |

### 14.3 ECU Behavioral Models

Each ECU is modeled as a **stateful service** that subscribes to incoming CAN frames, runs its internal state machine, and emits response frames. These models are the foundation for Full Simulation, Part-by-Part Emulation, and Hybrid modes.

**Models and their responsibilities:**

| ECU | Model | Key State Machines | Input Frames | Output Frames |
|-----|-------|-------------------|-------------|---------------|
| **HOST** | Drive-by-wire commander | Drive profile (speed, yaw, gear) | — (keyboard/joystick) | 0x300, 0x301, 0x302, 0x400, 0x7FC |
| **RT** | Gateway + steering controller | Mode (MANUAL/AUTO/ESTOP), Steering (BOOT_WAIT→LISTEN_SYNC→ACTIVE→FAULT), Command watchdog | 0x300, 0x301, 0x201, 0x7B9 feedback | 0x210, 0x220, 0x204, 0x205, 0x169, 0x7FD, 0x302(fwd), 0x310, 0x311 |
| **SYS** | Safety + body controller | Safety monitor (HB timeout, ESTOP), Brake control (SEB takeover), Mode manager | 0x210, 0x7FD, 0x721, 0x206, 0x7FE | 0x011, 0x012, 0x110, 0x600, 0x7B9, 0x7FE |
| **MTR** | Motor controller | Gear state, Fault flags, EGAS L2 monitoring | 0x204, 0x001 | 0x120, 0x206 |
| **EPS-C** | Steering actuator | Angle tracking, Alignment detection, 25 fault bits, L1/L2/L3 error levels | 0x169 | 0x201, 0x202, 0x203, 0x6FA |
| **SEB** | Brake actuator | Stroke/pressure tracking, 23 fault bits, Rolling counter validation | 0x7B9 | 0x721, 0x731, 0x741, 0x6FB |

**Model contract:**
```
ECU Model
  ├── config(params)          // one-time setup (bitrate, timing, bypass flags)
  ├── start()                 // begin state machine ticks
  ├── ingest(frame)           // receive a CAN frame → may trigger state transitions
  ├── tick(dt)                // periodic update → may emit frames
  ├── onFrame(callback)       // register listener for emitted frames
  ├── state()                 // read current ECU state (mode, faults, health)
  └── stop()                  // graceful shutdown
```

**Model provenance:** Two sources, one interface. The **authoritative** implementation is the firmware C++ source (`rt-esp32/src/`, `sys-esp32/src/`) — the same safety monitors, physics models, mode managers, and CAN dispatch that run on the ESP32, host-compilable via the HAL shadow layer. The **convenience** implementation is the TypeScript port in `simulation/src/ecus/` (355 vitest tests). Both satisfy the same `EcuModel` contract. See §14.8 for the native C++ integration strategy.

### 14.4 Per-ID Frame Routing

In Hybrid and Bench modes, the Frame Router maintains a **source table** that maps each `(bus, id)` pair to its authoritative source. This enables fine-grained control over which frames come from hardware and which come from software.

```
Source Table (example — Hybrid mode with EPS-C absent):

  high:0x300 → physical     (real Jetson on CANalyst-II Ch1)
  high:0x7FC → physical     (real host heartbeat)
  high:0x210 → physical     (real RT on high bus)
  high:0x7FD → physical     (real RT heartbeat)
  low:0x204  → physical     (real RT forwarding to low bus)
  low:0x201  → emulated     (EPS-C emulated — no real EPS-C detected)
  low:0x202  → emulated     (EPS-C fault status)
  low:0x721  → physical     (real SEB on low bus)
  ...
```

**Collision rules:**
- A physical frame arriving for an ID routed to "emulated" → logged as unexpected, optionally forwarded to emulated model as input
- An emulated frame for an ID routed to "physical" → silently dropped (real hardware wins)
- Two emulated models both claiming the same ID → configuration error, rejected at startup

**Inject-to-physical flag:** When an emulated ECU owns an ID and `injectEmulatedToPhysical` is true, the emulated frame is sent to the physical CAN bridge as a one-shot injection. This lets emulated ECUs participate in the real CAN bus.

### 14.5 Data Preservation Across Tabs

All 10 tabs remain **persistently mounted**. Switching tabs hides/shows via CSS — no component destruction, no state loss.

**Per-component state that must survive tab switches:**

| Component | State preserved |
|-----------|----------------|
| CAN Monitor | Bus filter, search text, pause toggle, category expand/collapse, scroll position |
| Injector | Selected bus + message, filled signal values, periodic injection timers |
| Controller | Drive setpoints, key-held state (WASD) |
| Emulator | Running ECU list, simMode toggle, per-signal live data |
| Pipeline | Correlation window settings, selected chain |
| Terminal | Command history, typed input, scroll position |
| Dictionary | Selected message, expanded signal details |

| Component | State preserved |
|-----------|----------------|
| CAN Monitor | Bus filter, search text, pause toggle, category expand/collapse, scroll position |
| Injector | Selected bus + message, filled signal values, periodic injection timers |
| Controller | Drive setpoints, key-held state (WASD) |
| Emulator | Running ECU list, simMode toggle, per-signal live data |
| Pipeline | Correlation window settings, selected chain |
| Terminal | Command history, typed input, scroll position |
| Dictionary | Selected message, expanded signal details |

**Current status:** Tab switching uses `{#if}` blocks which destroy component DOM. Target: CSS `display` toggle or Svelte `{#key}` keep-alive pattern.

### 14.6 Work Mode Configuration

Each mode is described by a serializable configuration object. Configurations can be saved, shared, and reloaded.

```typescript
interface WorkModeConfig {
  mode: "full-sim" | "emulator" | "hybrid" | "bench" | "monitor";

  // Which ECUs run as software models
  simulatedEcus: ("host" | "rt" | "sys" | "mtr" | "epsc" | "seb")[];

  // Per-ID source routing (for hybrid/bench modes)
  // "*" = auto-detect from ECU presence; explicit entries override
  idSources: Record<string, "physical" | "emulated" | "simulated" | "*">;

  // Whether emulated frames are injected onto the physical CAN bus
  injectEmulatedToPhysical: boolean;

  // Bench-test bypass flags (suppress listen-sync requirements)
  bypasses: {
    epscSync: boolean;    // Skip EPS-C steering sync → RT can drive without EPS-C
    sebSync: boolean;     // Skip SEB brake sync → SYS can operate without SEB
    mtrAbsent: boolean;   // Skip EGAS L2 motor monitoring
    benchSolo: boolean;   // MCP2515 ListenOnly + skip peer heartbeat timeouts
  };

  // Scenario to run on start (optional)
  scenario?: "drive-forward" | "estop-flow" | "mode-transition" | "heartbeat-timeout";
}
```

**Auto-detection:** In Hybrid and Emulator modes, the system detects which ECUs are physically present via heartbeat frames (0x7FC, 0x7FD, 0x7FE) and status frames (0x210, 0x011, 0x201, 0x721). Missing ECUs are flagged for emulation. The `idSources: "*"` wildcard means "auto-detect from presence."

**Bypass flags at runtime:** Currently bypass flags are compile-time only (`-D CONFIG_BYPASS_EPS_C_SYNC` in platformio.ini). The architecture supports runtime bypass via the ECU model's `config()` call — the model simply skips the listen-sync requirement when the flag is set. For real hardware, bypass flags remain compile-time until a runtime config protocol is added to the firmware.

### 14.7 Simulation Engine

The `SimulationEngine` class (in `debug-tool/backend/src/sim/`) orchestrates ECU models:

```
SimulationEngine
  ├── clock: VirtualClock           // wall-clock or accelerated
  ├── bus: VirtualCanBus            // dual-channel, routes frames between models
  ├── models: Map<ECU, EcuModel>    // active ECU instances
  ├── router: FrameRouter           // per-ID source table
  ├── scenario: Scenario | null     // active scenario (or null for interactive)
  │
  ├── start(config)                 // initialize models per WorkModeConfig
  ├── injectFromPhysical(frame)     // real CAN frame → route to matching model
  ├── emitFromModel(frame)          // model output → router → store + WebSocket
  ├── tick()                        // advance all models, process physics
  ├── getState()                    // snapshot of all model states
  └── stop()                        // graceful shutdown
```

**Virtual CAN bus** (`src/sim/bus/virtual-can.ts`): Routes frames between ECU models within the engine. Supports both bus channels (high/low), frame latency simulation, and bus-off injection for fault testing.

**Physics model** (`src/sim/physics/`): Tricycle kinematics — converts drive commands (speed, yaw) into per-wheel speeds, steering angle, and brake force. Feeds back into actuator models (EPS-C angle, MTR load, SEB pressure).

### 14.8 Model Implementation Strategy — Our ECUs vs. Third-Party Units

The six ECUs fall into two categories requiring fundamentally different modeling approaches, because the **source of truth** is different for each.

| | Our ECUs (RT, SYS, MTR) | Third-party (EPS-C, SEB) |
|---|---|---|
| **Have source code?** | ✅ C++ in `rt-esp32/src/`, `sys-esp32/src/`, `mtr-stm32/src/` | ❌ Vendor black boxes |
| **Platform** | ESP32-S3 (RT, SYS), STM32F103C8 (MTR) | EPS-C, SEB — proprietary controllers |
| **Source of truth** | The firmware source itself | The CAN protocol document (CSV/YAML) |
| **Modeling approach** | Compile firmware C++ natively → bit-identical behavior | CAN-level behavioral model — what frames the ECU sends, at what rates, in response to which commands |
| **Validation strategy** | Native tests against firmware logic | Record real hardware CAN traffic → replay → diff model output against capture |
| **What we model** | Internal state machines, logic, decision paths | Observable CAN behavior: frame timing, command→response correlation, fault mode escalation (L1/L2/L3), checksum/rolling-counter compliance |

**Protocol assets for third-party units:**

| Asset | EPS-C | SEB |
|-------|-------|-----|
| Manufacturer CSV | `docs/by-wire - steering.csv` | `docs/by-wire - brake.csv` |
| YAML definition | `shared/can/can_low.yaml` | `shared/can/can_low.yaml` |
| TypeScript model | `simulation/src/ecus/epsc.ts` (130 lines) | `simulation/src/ecus/seb.ts` (144 lines) |
| Known fault bits | 25 (L1/L2/L3 severity) | 23 (L1/L2/L3 severity) |
| Comm timeout | 30ms (3× 100Hz frames) | 20ms (2× 100Hz frames) |
| Security features | Rolling counter + XOR checksum | Rolling counter + XOR checksum |

#### 14.8.1 Our ECUs (RT, SYS, MTR): Native C++ Compilation

The firmware logic modules are host-compilable with g++. Header-only design plus a HAL shadow layer that replaces platform-specific APIs (ESP-IDF for RT/SYS, STM32 HAL for MTR) with host equivalents.

```
ESP-IDF (RT, SYS)               Host stub
──────────────────               ─────────
ESP_LOGI(tag, ...)          →   printf("[I] tag: ...")
esp_timer_get_time()        →   std::chrono::steady_clock (or test-controlled time)
twai_transmit(...)          →   virtual_can_bus.send(frame)
gpio_set_level(...)         →   no-op

STM32 HAL (MTR)                  Host stub
───────────────                  ─────────
HAL_ADC_GetValue(&hadc1)    →   g_adc_value (test-configurable)
HAL_GPIO_ReadPin(GPIOA, N)  →   (g_gpio_state & mask) ? SET : RESET
HAL_GPIO_WritePin(GPIOA, N) →   g_gpio_state |= mask / &= ~mask
```

The `native-test/` CMake project compiles firmware source directly. Three integration paths exist for loading native models into the Node.js backend:

| Path | Complexity | Cross-platform | Latency | Recommendation |
|------|-----------|---------------|---------|---------------|
| **IPC (stdin/stdout JSON-Lines)** | Low | ✅ Recompile per platform | ~1ms per frame | **Pragmatic choice** |
| WASM via Emscripten | Medium-High | ✅ Single .wasm binary | Sub-ms | Viable, FreeRTOS→WASM extra work |
| Node.js napi native addon | High | ❌ Per-platform .node binary | Sub-ms | Over-engineered for 50 Hz tick rates |

**Recommended: IPC child process.** The same pattern already used for the CANalyst-II Python bridge — the backend spawns a native executable, communicates via stdin/stdout JSON Lines. Zero Node.js build complexity. Zero native addon maintenance. Same lifecycle management as the existing Python bridge.

```
Backend (Node.js)                     sim-engine-native (C++)
      │                                       │
      │── {"type":"frame","bus":"high","id":"0x300",...}──→│  stdin
      │── {"type":"config","bypass_epsc_sync":true}─────→│
      │                                       │
      │←─ {"type":"frame","bus":"low","id":"0x204",...}────│  stdout
      │←─ {"type":"state","ecu":"rt","mode":"AUTO",...}────│
```

The `EcuModel` interface (§14.3) is the contract — the SimulationEngine doesn't care whether the implementation is an IPC child process, a WASM module, or a TypeScript class.

**What compiles natively today:**

| Module | File | Native test coverage |
|--------|------|---------------------|
| Physics model | `rt-esp32/src/physics_model.cpp` | 7 assertions |
| Safety monitor (RT) | `rt-esp32/src/safety_monitor.h` | 21 assertions |
| Safety monitor (SYS) | `sys-esp32/src/safety_monitor.h` + `.cpp` | Startup grace, HB, frozen counter |
| Mode manager (SYS) | `sys-esp32/src/mode_manager.h` + `.cpp` | Toggle, ESTOP, CAN set |
| CAN dispatch (RT) | `rt-esp32/src/can_dispatch.h` | Gateway forwarding, bus filtering |
| CAN RX router | `rt-esp32/src/can_rx_router.h` | 19 assertions |
| Heartbeat | `rt-esp32/src/heartbeat.h` | Dual-bus, recovery |
| Command watchdog | `rt-esp32/src/watchdog.h` | Staleness, wraparound |
| CAN protocol | `shared/can/can_protocol.h` | 15 struct roundtrips |
| Throttle input (MTR) | `mtr-stm32/src/throttle_input.h` | 5 assertions — ADC dead zone, linear mapping, max speed |
| Gear control (MTR) | `mtr-stm32/src/gear_control.h` | Init defaults, single-gear sense, conflict detection failsafe, MOSFET state |

**Not host-compilable:** `main.cpp` on all three ECUs (FreeRTOS task orchestration), MCP2515 driver (SPI on RT), TWAI driver (ESP-IDF on RT/SYS), STM32 CAN driver (`mtr-stm32/src/can_driver.h`), encoder (PCNT), DAC (I2C on SYS and MTR), ADC reads, GPIO interrupts. These are the I/O layer — the logic layer above them is fully host-compilable across all three platforms.

**Two-layer architecture — why this works:**

The firmware source has a clean separation between logic and I/O that makes host compilation straightforward:

```
LOGIC LAYER (host-compilable)          I/O LAYER (ESP32/STM32 only)
─────────────────────────────          ─────────────────────────────
physics_model.h/.cpp                   main.cpp (FreeRTOS tasks)
steering_control.h (state machine)     can_driver_mcp2515.cpp (SPI)
safety_monitor.h (checks)              can_driver_twai.cpp (ESP-IDF)
mode_manager.h/.cpp (state machine)    mcp4725_dac.h (I2C)
can_dispatch.h (routing)               encoder_pcnt.cpp (PCNT)
heartbeat.h (timing)                   throttle_input.h::read_raw() (ADC)
watchdog.h (staleness)                 gear_control.h::read_sense_pin() (GPIO)
throttle_input.h::tick() (math)
gear_control.h::set_mosfets() (logic)
```

**Key architectural properties:**
- Logic modules use `extern std::atomic<T>` globals for cross-task state — the simulation host provides these
- FreeRTOS dependency is **type-only** in logic headers (`QueueHandle_t` → `void*`); actual queue operations live in `main.cpp`
- `#ifdef CONFIG_BYPASS_*` compile flags are set via `-D` for simulation mode — exactly the bypasses we want active
- MTR has inline HAL calls in I/O functions (`read_raw`, GPIO helpers), but simulation only calls the logic functions (`tick()`, `set_mosfets()`) — I/O stubs just need to exist for the linker

#### 14.8.2 Third-Party ECUs (EPS-C, SEB): CAN-Level Behavioral Models

Since we don't have source code for purchased/vendor ECUs, the modeling approach is fundamentally different. We model **what the ECU does on the CAN bus**, not what happens inside it.

**Modeling from protocol documentation:** The manufacturer CSV files define the complete CAN contract — frame IDs, signal layouts, byte orders, scaling factors, enum values, and fault code tables. This IS the specification. The model implements that specification.

**What a third-party ECU model does:**

1. **Subscribes to command frames** — e.g., EPS-C listens for `0x169 VCU_SES_REQ`, SEB listens for `0x7B9 VCU_SEB_REQ`, MTR listens for `0x204 RT_DRIVE_CMD`
2. **Runs a simple internal model** — angle tracking with rate limiting (EPS-C), stroke/pressure response with first-order lag (SEB), speed tracking (MTR)
3. **Emits response frames at the correct rate** — `0x201` at 100Hz, `0x721` at 100Hz, `0x206` at 50Hz — matching the real ECU's timing
4. **Implements protocol security features** — rolling counters that increment, XOR checksums (`^ 0xFF`), alignment state tracking
5. **Models fault escalation** — comm timeout → L3 error → error_status byte → fault bit masks in ErrInfo frames
6. **Accepts physics plant input** — `setActualAngle(deg)` for EPS-C, `setActualStroke(mm)` for SEB, `setActualSpeed(mmps)` for MTR — the physics model drives the actuator model, which reports realistic feedback

**Validation strategy for third-party models:**

```
1. Record:    Connect real EPS-C/SEB/MTR to CANalyst-II, run test scenarios, capture all frames
2. Replay:    Feed recorded command frames (0x169/0x7B9/0x204) into the TypeScript model
3. Compare:   Diff model output against recorded response frames (0x201/0x721/0x206)
4. Measure:   Timing accuracy (frame rate), value accuracy (angle/pressure/speed within tolerance),
              fault behavior (correct bits set for each error condition)
5. Iterate:   Tune model parameters until output matches real hardware within acceptable tolerance
```

**Current model fidelity (TypeScript, pre-validation):**

| ECU | Frames modeled | Known gaps |
|-----|---------------|------------|
| EPS-C | 0x201, 0x202, 0x203, 0x6FA | Angle tracking is 0th-order (instant, no rate limit). Fault injection is binary (L3 on/off), doesn't exercise individual fault bits. No torque feedback model. |
| SEB | 0x721, 0x731, 0x741, 0x6FB | Stroke/pressure response is 0th-order (no hydraulic dynamics). Angle sensor coupling is a placeholder. Startup grace period is modeled (500ms). |
| MTR | 0x120, 0x206 | Delegates to `MtrMotorController` in `simulation/src/controllers/`. 4-bit fault_flags, gear state enum. Speed tracking follows plant model. |

**Why TypeScript is correct for third-party units:** There is no C++ source to compile. The protocol document IS the truth. A TypeScript model that faithfully implements the documented CAN behavior is the right approach. The native C++/WASM path only makes sense for our ECUs where we have firmware source.

---

## 15. Implementation Status (v0.4.0-alpha)

### Built and Verified

| Component | Location | Status |
|-----------|----------|--------|
| FrameRouter (per-ID source routing) | `backend/src/sim/router.ts` | ✅ Built, integrated into DebugStore |
| SimulationEngine | `backend/src/sim/engine.ts` | ✅ Built, ticks all 6 ECU models at 100Hz |
| VirtualCanBus | `backend/src/sim/virtual-can.ts` | ✅ Built, dual-channel frame routing |
| VirtualClock | `backend/src/sim/engine.ts` | ✅ Tick-based, supports accelerated time |
| ECU model — HOST | `backend/src/sim/ecus/host-model.ts` | ✅ Drive, brake, heartbeat |
| ECU model — RT | `backend/src/sim/ecus/rt-model.ts` | ✅ Gateway forwarding, ESTOP, mode, steering, heartbeat |
| ECU model — SYS | `backend/src/sim/ecus/sys-model.ts` | ✅ Safety monitor, brake forwarding, heartbeat |
| ECU model — MTR | `backend/src/sim/ecus/mtr-model.ts` | ✅ Speed tracking, brake response, ESTOP, gear |
| ECU model — EPS-C | `backend/src/sim/ecus/epsc-model.ts` | ✅ Angle tracking, 25 fault bits, checksums, L1/L2/L3 |
| ECU model — SEB | `backend/src/sim/ecus/seb-model.ts` | ✅ Stroke/pressure tracking, 23 fault bits, checksums |
| Native C++ IPC path | `native-test/sim-engine/main_native.cpp` | ✅ Compiles firmware physics_model.cpp, JSON-Lines IPC verified |
| IpcEngineAdapter | `backend/src/sim/ipc-adapter.ts` | ✅ Spawns native process, implements EcuModel |
| Work mode selector | Topbar dropdown | ✅ 5 modes, config persistence |
| Tab data preservation | `App.svelte` CSS display toggle | ✅ All 10 tabs stay mounted |
| CAN health dashboard | `Stats.svelte` | ✅ TEC/REC with CAN error states (Warning/Passive/Bus-Off) |
| ECU topology diagram | `EcuTopology.svelte` | ✅ SVG with real/emulated/missing indicators |
| Emulator behavioral models | `Emulator.svelte` | ✅ Dynamic data responds to drive/brake/steer/ESTOP |
| YAML→TS CAN catalog generator | `shared/can/generate_can_index.py` | ✅ 37 messages, single source of truth |
| Stats staleness (BUG-01) | `queries.ts` | ✅ 5s TTL, returns zeros when stale |
| ECU presence staleness (BUG-02) | `telemetry.ts` | ✅ 3s timeout |
| BusDetector auto-reset (BUG-03) | `can.ts` | ✅ 10s timeout |
| Non-blocking startup (BUG-06) | `index.ts` | ✅ Server listens before transport detection |
| WS filter race (BUG-08) | `ws.ts` | ✅ Filter sent before onState |
| Runtime transport switching (BUG-11) | `index.ts` | ✅ POST /api/system/switch-transport |
| dotenv support (BUG-17) | `config.ts` | ✅ Auto-loads .env |
| Frame rate from DB (BUG-01 partial) | `queries.ts` | ✅ Stats TTL, not yet derived from DB query |

### Verified Feedback Loops

| Loop | Command | Response | Status |
|------|---------|----------|--------|
| Drive | 0x300 → 0x204 | 0x206, 0x120 | ✅ Verified end-to-end |
| Brake | 0x301 → 0x205 | Speed reduced | ✅ Verified end-to-end |
| Steering | 0x169 | 0x201 (angle + checksum + roll counter) | ✅ Verified end-to-end |
| Brake-by-wire | 0x7B9 | 0x721 (stroke + pressure + checksum) | ✅ Verified end-to-end |
| ESTOP | 0x001 | Speed=0, safety=1, estop=1 | ✅ Verified end-to-end |
| Mode | 0x110 | 0x210 reports new mode | ✅ Verified end-to-end |
| Heartbeat | 0x7FD | SYS 0x011 heartbeat_ok | ✅ Verified end-to-end |

### Still Needed (Hardware Required)

- Full HIL test with all 5 ECUs on CAN bus
- Capture→replay validation for EPS-C/SEB models against real hardware
- Native C++ RT model (IPC) end-to-end test with real firmware behavior

---

## 16. Reference

- [Main Architecture](../architecture.md) — system topology, message catalog, mode state machine
- [CAN Dictionary](../can-dictionary.md) — full bit-level signal catalog
- [Bench Test Plan](../docs/can-bench-test.md) — hardware setup and injection guide
- [Wiring Reference](../docs/wiring.md) — pin-level wiring for all ECUs
- [CAN YAML Sources](../shared/can/can_high.yaml) — source of truth for CAN definitions
