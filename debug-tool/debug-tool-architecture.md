# Debug Tool — System Architecture

A CAN bus diagnostic, monitoring, and hardware-in-the-loop test tool for the E-Trike vehicle control system. Connects to the **high-level CAN bus** (Jetson ↔ RT), bridges all traffic to a web UI over Wi-Fi/MQTT, and can inject commands to simulate the Jetson for bench testing.

---

## 1. Why High-Level Bus Only

The high-level CAN bus carries everything needed for system-level debugging:

| What | CAN IDs | How it gets there |
|------|---------|-------------------|
| Jetson commands | `0x300`, `0x301`, `0x302`, `0x400` | Native — Jetson transmits here |
| Safety status | `0x011`, `0x210`, `0x001` | RT forwards from low bus + RT own reports |
| Actual speed | `0x120` | RT forwards from low bus (MTR→SYS→RT→high) |
| System diagnostics | `0x600` | RT forwards from low bus (SYS→RT→high) |
| Heartbeats | `0x7FC`, `0x7FD` | Jetson + RT native |
| Obstacle distance | `0x400` | Jetson perception pipeline |

**What stays on the low bus** (actuator-level, only needed for deep SYNTREE/MTR debugging):
`0x169`, `0x201`, `0x202`, `0x203`, `0x204`, `0x205`, `0x206`, `0x6FA`, `0x6FB`, `0x6FB`, `0x721`, `0x731`, `0x741`, `0x7B9`, `0x7FE`, `0x012`, `0x110`

This means:
- **1 CAN transceiver** (built-in TWAI), no MCP2515 SPI chip
- **1 CAN RX task** instead of 2
- **Simpler MQTT topic tree** — no `low`/`high` split
- **~5 FreeRTOS tasks** instead of 7

If low-bus monitoring is needed later, a second TWAI (ESP32-S3 has two) or MCP2515 can be added as a v2 feature.

---

## 2. Topology

```
┌── Developer Machine ────────────────────────────────────────────────────┐
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  Browser (localhost:5173)                                         │    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐            │    │
│  │  │Dashboard │ │Monitor   │ │Injector  │ │Stats     │            │    │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘            │    │
│  └────────────────────────────┬─────────────────────────────────────┘    │
│                               │ WebSocket + REST (:3000)                │
│  ┌────────────────────────────┴─────────────────────────────────────┐    │
│  │  Fastify Backend (:3000)                Aedes MQTT Broker (:1883)│    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐    │    │
│  │  │ REST API │ │WebSocket │ │Internal  │ │ SQLite           │    │    │
│  │  │ Routes   │ │Stream    │ │MQTT Sub  │ │ (better-sqlite3) │    │    │
│  │  └──────────┘ └──────────┘ └─────┬────┘ └────────┬─────────┘    │    │
│  └──────────────────────────────────┼───────────────┼──────────────┘    │
│                                     │ MQTT          │ DB writes          │
└─────────────────────────────────────┼───────────────┼───────────────────┘
                                      │               │
                                Wi-Fi (TCP :1883)      │
                                      │               │
┌── ESP32-S3 (debug-esp32) ───────────┼───────────────┼───────────────────┐
│                                     │               │                    │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  FreeRTOS Scheduler                                               │    │
│  │                                                                   │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │ can_rx_task  │  │ can_decode   │  │ mqtt_publish │            │    │
│  │  │ prio 5       │  │ prio 4       │  │ prio 3       │            │    │
│  │  │ TWAI RX      │──│ ID→JSON      │──│ MQTT pub     │            │    │
│  │  └──────────────┘  └──────────────┘  └──────┬───────┘            │    │
│  │                                             │                     │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────┴───────┐            │    │
│  │  │ mqtt_sub     │  │ can_inject   │  │ stats_task   │            │    │
│  │  │ prio 3       │  │ (on cmd)     │  │ prio 1, 1 Hz │            │    │
│  │  │ cmd topics ──│──│ CAN TX       │  │ per-ID cnts  │            │    │
│  │  └──────────────┘  └──────┬───────┘  └──────────────┘            │    │
│  │                           │                                       │    │
│  └───────────────────────────┼───────────────────────────────────────┘    │
│                              │                                            │
│  ┌───────────────────────────┴───────────────────────────────────────┐    │
│  │  TWAI (built-in CAN controller)                                    │    │
│  │  GPIO 5 (TX), GPIO 4 (RX), 500 kbit/s                             │    │
│  │  + SN65HVD230 transceiver                                         │    │
│  └───────────────────────────┬───────────────────────────────────────┘    │
└──────────────────────────────┼────────────────────────────────────────────┘
                               │
                     High-Level CAN Bus
                         (500 kbit/s)
                               │
              ┌────────────────┼────────────────┐
              │                │                │
         ┌────┴────┐     ┌────┴────┐     ┌─────┴─────┐
         │ Jetson  │     │RT ESP32 │     │ (other    │
         │  Orin   │     │(Gateway)│     │  nodes    │
         └─────────┘     └─────────┘     │  on low   │
                                         │  bus)     │
                                         └───────────┘
```

**Key:** The debug-ESP32 is a passive listener + on-demand injector on the high bus. It never transmits unless commanded.

---

## 3. CAN Message Catalog — 12 IDs

All IDs are from [shared/can/can_protocol.h](../shared/can/can_protocol.h).

### 3.1 Monitored Frames (always passive)

| ID | Name | Sender | Period | DLC | Decoded Fields |
|----|------|--------|--------|-----|----------------|
| `0x001` | SAFETY_ESTOP | any | Event | 0 | (none — zero-length frame) |
| `0x011` | SYS_SAFETY_STS | RT (fwd) | 5 Hz | 2 | `estop_active`, `heartbeat_ok` |
| `0x120` | SYS_THROTTLE_STS | RT (fwd) | 100 Hz | 2 | `speed_mmps` |
| `0x210` | RT_STATE_RPT | RT | 10 Hz | 3 | `mode` (Manual/Auto/Estop), `steer_valid`, `reversing` |
| `0x220` | RT_PID_RPT | RT | — (reserved) | 6 | `speed_setpoint_mmps`, `speed_measured_mmps`, `pid_output` |
| `0x300` | HOST_DRIVE_CMD | Jetson | ≤100 Hz | 8 | `speed_mmps`, `yaw_rate_mrad_s`, `gear` |
| `0x301` | HOST_BRAKE_REQ | Jetson | Demand | 4 | `brake_pressure_kpa` |
| `0x302` | HOST_LIGHT_CMD | Jetson | Change | 1 | `left_turn`, `right_turn`, `brake_light`, `headlight` |
| `0x400` | HOST_OBSTACLE_DIST | Jetson | 10 Hz | 4 | `distance_mm` |
| `0x600` | SYS_DIAG_RPT | RT (fwd) | 1 Hz | 8 | `mode`, `brake_engaged`, `heartbeat_ok`, `estop_active`, `free_heap_kb`, `tec`, `rec` |
| `0x7FC` | JETSON_HEARTBEAT | Jetson | 2 Hz | 1 | `alive_ctr` |
| `0x7FD` | RT_HEARTBEAT | RT | 2 Hz | 1 | `alive_ctr` |

### 3.2 Injectable Frames

| ID | Name | Use for Testing |
|----|------|-----------------|
| `0x001` | SAFETY_ESTOP | Trigger ESTOP propagation, verify gateway |
| `0x300` | HOST_DRIVE_CMD | Simulate Jetson driving — tests full RT→SYS→MTR pipeline |
| `0x301` | HOST_BRAKE_REQ | Simulate Jetson braking — tests RT→SYS→SEB pipeline |
| `0x302` | HOST_LIGHT_CMD | Test light relay outputs |
| `0x400` | HOST_OBSTACLE_DIST | Test obstacle braking threshold |
| `0x7FC` | JETSON_HEARTBEAT | Test RT heartbeat timeout → ESTOP behavior |
| `0x011` | SYS_SAFETY_STS | Simulate SYS safety status (if testing Jetson in isolation) |
| `0x120` | SYS_THROTTLE_STS | Simulate speed feedback (if testing Jetson in isolation) |

---

## 4. MQTT Protocol

### 4.1 Topic Tree (simplified — single bus)

```
etrike/
└── debug/
    ├── status                        ← retained, "online" | "offline"
    ├── uptime                        ← seconds since boot (5 s period)
    │
    ├── can/
    │   ├── rx/
    │   │   ├── 0x001                 ← one topic per CAN ID
    │   │   ├── 0x011
    │   │   ├── 0x120
    │   │   ├── 0x210
    │   │   ├── 0x220
    │   │   ├── 0x300
    │   │   ├── 0x301
    │   │   ├── 0x302
    │   │   ├── 0x400
    │   │   ├── 0x600
    │   │   ├── 0x7FC
    │   │   └── 0x7FD
    │   └── stats                     ← 1 Hz: per-ID counts, bus load %, TEC/REC
    │
    ├── cmd/
    │   ├── send                      → inject single CAN frame
    │   ├── send/periodic             → start/stop periodic injection
    │   └── response                  ← JSON ack/error for each command
    │
    └── system/
        └── reset                     → soft-reset the ESP32
```

### 4.2 CAN Frame Payload (RX)

Published by debug-esp32 for every received frame. Topic: `etrike/debug/can/rx/<ID>`

```json
{
  "ts": 1719234567.890123,
  "id": "0x300",
  "name": "HOST_DRIVE_CMD",
  "dlc": 8,
  "data": [0, 0, 7, 208, 0, 0, 100, 1],
  "decoded": {
    "speed_mmps": 2000,
    "yaw_rate_mrad_s": 100,
    "gear": 1,
    "gear_name": "D"
  }
}
```

### 4.3 Stats Payload (periodic, 1 Hz)

Topic: `etrike/debug/can/stats`

```json
{
  "ts": 1719234568.0,
  "uptime_s": 3600,
  "total_frames": 245000,
  "frames_per_s": 340,
  "bus_load_pct": 22.3,
  "tec": 0,
  "rec": 0,
  "by_id": {
    "0x120": 360000,
    "0x300": 18000,
    "0x210": 3600,
    "0x011": 1800,
    "0x600": 360,
    "0x7FC": 720,
    "0x7FD": 720
  }
}
```

### 4.4 Command Injection Payload

```json
// → etrike/debug/cmd/send
{
  "request_id": "a1b2c3d4",
  "id": "0x300",
  "dlc": 8,
  "data": [0, 0, 7, 208, 0, 0, 100, 1]
}
```

```json
// → etrike/debug/cmd/send/periodic  (start)
{
  "request_id": "a1b2c3d5",
  "action": "start",
  "id": "0x300",
  "dlc": 8,
  "data": [0, 0, 7, 208, 0, 0, 100, 1],
  "interval_ms": 20,
  "count": 5000
}
```

```json
// → etrike/debug/cmd/send/periodic  (stop)
{
  "request_id": "a1b2c3d6",
  "action": "stop",
  "id": "0x300"
}
```

### 4.5 Command Response

```json
// ← etrike/debug/cmd/response
{
  "request_id": "a1b2c3d4",
  "status": "ok",
  "error": null
}
```

---

## 5. Backend (`backend/`)

### 5.1 Stack

| Layer | Choice |
|-------|--------|
| Runtime | Node.js 22 |
| Language | TypeScript 5.x |
| Web framework | Fastify 5.x |
| MQTT broker | Aedes (embedded, port 1883) |
| MQTT client | mqtt (MQTT.js) |
| Database | better-sqlite3 |
| Validation | Zod |

### 5.2 REST API

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/status` | Backend health + debug-esp32 online/offline + uptime |
| `GET` | `/api/can/ids` | All 12 CAN IDs with names, DLC, field definitions, enum labels |
| `GET` | `/api/can/frames` | Query history: `?id=0x300&since=<ts>&limit=500` |
| `GET` | `/api/can/stats` | Latest bus statistics snapshot |
| `POST` | `/api/cmd/send` | Inject CAN frame: `{id, dlc, data}` |
| `POST` | `/api/cmd/periodic` | Start/stop periodic: `{action, id, dlc, data, interval_ms, count}` |
| `GET` | `/api/recordings` | List sessions |
| `POST` | `/api/recordings` | Start recording: `{label}` |
| `PUT` | `/api/recordings/:id/stop` | Stop recording |
| `GET` | `/api/recordings/:id/frames` | Get recording frames (paginated) |
| `DELETE` | `/api/recordings/:id` | Delete recording |
| `GET` | `/api/templates` | Pre-built injection templates |

### 5.3 WebSocket (`/ws`)

Client connects → receives live stream. Protocol:

```json
// Server → Client
{ "type": "can_frame",  "payload": { /* CAN frame */ } }
{ "type": "stats",      "payload": { /* stats */ } }
{ "type": "cmd_ack",    "payload": { "request_id": "...", "status": "ok" } }
{ "type": "status",     "payload": { "debug_esp32_online": true } }
```

```json
// Client → Server (optional filter)
{ "type": "filter", "ids": ["0x300", "0x120", "0x210"] }
```

### 5.4 SQLite Schema

```sql
CREATE TABLE can_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,           -- backend wall clock (unix epoch)
    ts_device   REAL NOT NULL,           -- ESP32 boot-relative seconds
    can_id      TEXT NOT NULL,           -- '0x300'
    can_name    TEXT NOT NULL,           -- 'HOST_DRIVE_CMD'
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,           -- raw 8 bytes
    decoded     TEXT NOT NULL            -- JSON string
);
CREATE INDEX idx_frames_id_ts ON can_frames(can_id, ts_real);
CREATE INDEX idx_frames_ts    ON can_frames(ts_real);

CREATE TABLE injected_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    can_id      TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    request_id  TEXT NOT NULL,
    response    TEXT                     -- JSON ack/error
);

CREATE TABLE recordings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    label       TEXT,
    started_at  REAL NOT NULL,
    stopped_at  REAL,
    frame_count INTEGER DEFAULT 0
);

CREATE TABLE recording_frames (
    recording_id INTEGER NOT NULL REFERENCES recordings(id),
    frame_id     INTEGER NOT NULL REFERENCES can_frames(id),
    PRIMARY KEY (recording_id, frame_id)
);
```

### 5.5 Internal MQTT Client

The backend's internal MQTT client subscribes to:
- `etrike/debug/can/rx/#` → write to `can_frames`, fan-out to WebSocket clients
- `etrike/debug/can/stats` → cache, fan-out to WebSocket
- `etrike/debug/status` → update device online state (retained)
- `etrike/debug/cmd/response` → match `request_id`, update `injected_frames`

---

## 6. Frontend (`ui/`)

### 6.1 Stack

| Layer | Choice |
|-------|--------|
| Framework | Svelte 5 + Vite |
| Language | TypeScript |
| Charts | Chart.js (via svelte-chartjs) |
| Testing | Playwright |

### 6.2 Pages

#### Dashboard

```
┌─────────────────────────────────────────────────────────┐
│  E-Trike Debug                           🟢 ESP32 Online│
├──────────────┬──────────────┬────────────┬──────────────┤
│ Bus Load     │ Frames/s     │ ESTOP      │ Mode         │
│ 22.3%        │ 340 fps      │ ⬜ CLEAR   │ AUTO         │
├──────────────┴──────────────┴────────────┴──────────────┤
│  Latest Values                                          │
│  Speed: 1500 mm/s  │  Yaw: 100 mrad/s  │  Gear: D      │
│  Brake: 0 kPa      │  Obstacle: ∞ mm   │  HB RT: ✅    │
└─────────────────────────────────────────────────────────┘
```

#### CAN Monitor

```
┌─────────────────────────────────────────────────────────┐
│  CAN Monitor                     [⏸ Pause] [Filter...]  │
├────────────┬───────────────┬────────────────────────────┤
│ Timestamp  │ ID / Name     │ Decoded                    │
├────────────┼───────────────┼────────────────────────────┤
│ 12:34:00.001│ 0x300 DRIVE   │ speed=2000 yaw=100 gear=D │
│ 12:34:00.005│ 0x120 SPEED   │ speed=1980 mm/s           │
│ 12:34:00.010│ 0x210 STATE   │ mode=AUTO steer=OK        │
│ 12:34:00.020│ 0x011 SAFETY   │ estop=0 hb=OK            │
│ 12:34:01.000│ 0x600 DIAG     │ mode=AUTO heap=245 TEC=0 │
│ ...        │ ...           │ ...                        │
└────────────┴───────────────┴────────────────────────────┘
```
- Click row → expand raw hex + all decoded fields
- Filter: by CAN ID (multi-select), time range
- Export visible rows as JSON/CSV

#### CAN Injector

```
┌─────────────────────────────────────────────────────────┐
│  CAN Injector                                            │
├─────────────────────────────────────────────────────────┤
│  CAN ID: [0x300 HOST_DRIVE_CMD ▼]                       │
│                                                         │
│  ┌─ Payload ────────────────────────────────────────┐   │
│  │ speed_mmps:       [  2000  ] mm/s  (-500..3000)  │   │
│  │ yaw_rate_mrad_s:  [   100  ] mrad/s (-3000..3000)│   │
│  │ gear:             [  D   ▼]  (N/D/S/R)           │   │
│  └──────────────────────────────────────────────────┘   │
│                                                         │
│  Raw: 00 00 07 D0 00 00 64 01                          │
│                                                         │
│  [Send Once]   [▶ Send Periodic...]                     │
│                                                         │
│  ┌─ History ────────────────────────────────────────┐   │
│  │ 12:34:10  0x300  speed=2000 gear=D       ✅ ok   │   │
│  │ 12:34:05  0x001  ESTOP                    ✅ ok   │   │
│  │ 12:33:50  0x301  brake=5000               ❌ ack  │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

#### Statistics
- Line chart: frame rate over time (last 5 min)
- Bar chart: per-ID frame counts
- Bus load gauge
- TEC/REC display (alert if non-zero)

---

## 7. Firmware (`debug-esp32/`)

### 7.1 Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3-DevKitC-1 |
| CAN transceiver | SN65HVD230 on GPIO 5 (TX), GPIO 4 (RX) |
| Bus termination | 120Ω (provided by existing bus termination) |
| Wi-Fi | 2.4 GHz STA mode |

**One transceiver, one bus.** The ESP32-S3's built-in TWAI controller handles everything.

### 7.2 FreeRTOS Tasks

| Task | Prio | Stack | Period | Purpose |
|------|------|-------|--------|---------|
| `can_rx` | 5 | 4096 | Blocking | TWAI receive → push to decode queue (32 deep) |
| `can_decode` | 4 | 6144 | Event-driven | Pop queue → dispatch ID → call `from_frame()`/`unpack()` → serialize JSON → push to MQTT pub queue |
| `mqtt_publish` | 3 | 8192 | Event-driven | Pop pub queue → `esp_mqtt_client_publish()` |
| `mqtt_subscribe` | 3 | 6144 | Blocking | MQTT event handler → parse cmd topics → inject CAN frame or start/stop periodic timer |
| `stats` | 1 | 4096 | 1000 ms | Compute per-ID counts, bus load %, publish stats JSON |
| `heartbeat` | 2 | 3072 | 5000 ms | Publish `status=online` (retained) + uptime |

**Total stack:** ~28 KB. ESP32-S3 has 512 KB SRAM.

### 7.3 Frame Decoding

The decode task uses a dispatch table keyed on CAN ID, calling the appropriate `from_frame()` or `unpack()` from `shared/can/can_protocol.h`. This is the **single source of truth** — no decoding logic is duplicated in TypeScript.

```cpp
void decode_and_publish(const can::Frame& fr) {
    switch (fr.id) {
        case can::kIdHostDriveCmd: {
            auto cmd = can::HostDriveCmd::from_frame(fr);
            publish_json("etrike/debug/can/rx/0x300",
                R"({"name":"HOST_DRIVE_CMD","decoded":{)"
                R"("speed_mmps":%d,"yaw_rate_mrad_s":%d,"gear":%d,"gear_name":"%s"}})",
                cmd.speed_mmps, cmd.yaw_rate_mrad_s, cmd.gear,
                can::gear_name(static_cast<can::Gear>(cmd.gear)));
            break;
        }
        case can::kIdHostBrakeReq: {
            auto cmd = can::HostBrakeReq::from_frame(fr);
            publish_json("etrike/debug/can/rx/0x301", ...);
            break;
        }
        // ... 10 more IDs
        default:
            // Unknown ID → raw hex only, no decoded fields
            publish_json("etrike/debug/can/rx/<id>",
                R"({"name":"UNKNOWN_%03X","decoded":{}})", fr.id);
    }
}
```

### 7.4 Command Injection Safety

- Periodic injection: max 10 kHz rate, max 50,000 frames per command
- ESTOP (`0x001`) requires `"confirm_estop": true` in the command payload
- All periodic injections auto-cancel on MQTT disconnect
- Every injected frame is counted and published in stats

### 7.5 Wi-Fi Config

```ini
# sdkconfig.defaults
CONFIG_DEBUG_WIFI_SSID="MyNetwork"
CONFIG_DEBUG_WIFI_PASSWORD="MyPassword"
CONFIG_DEBUG_MQTT_BROKER_URL="mqtt://192.168.1.100:1883"
```

---

## 8. Simulator (`simulator/`)

Hardware-free device simulator for UI development and CI testing. Connects to MQTT exactly like the real ESP32.

```
sim.ts
├── Connects to MQTT broker (localhost:1883)
├── Publishes etrike/debug/status = "online" (retained)
├── Generates synthetic CAN traffic:
│   ├── 0x120 at 100 Hz (speed=~1500 mm/s, slight random walk)
│   ├── 0x300 at 50 Hz  (drive cmd matching speed)
│   ├── 0x210 at 10 Hz  (mode=AUTO, steer_valid=true)
│   ├── 0x011 at 5 Hz   (estop=0, hb_ok=true)
│   ├── 0x600 at 1 Hz   (diag: mode=AUTO, heap=245, TEC=0)
│   ├── 0x400 at 10 Hz  (obstacle=UINT32_MAX)
│   ├── 0x7FC at 2 Hz   (Jetson HB, counter++)
│   ├── 0x7FD at 2 Hz   (RT HB, counter++)
│   └── Stats at 1 Hz
├── Handles etrike/debug/cmd/send → acks with response
├── Handles etrike/debug/cmd/send/periodic → starts/stops generator
└── Loads traffic profile from profiles/<name>.json
```

---

## 9. E2E Tests (`e2e/`)

Playwright tests against backend + simulator + UI:

| Test | What It Verifies |
|------|-----------------|
| Dashboard shows online | Simulator connects → green dot within 2s |
| CAN frames stream to monitor | MQTT → WebSocket → table row rendered |
| Inject single frame | UI form → REST → MQTT → ack shown |
| Inject periodic | Start → frames at rate → stop → no more frames |
| Filter by CAN ID | Filter dropdown → only matching rows |
| Recording start/stop | Frames captured → recording listed with correct count |
| Export recording | Download produces valid JSON |
| Stats render | Charts + gauges populated with data |
| ESTOP confirmation gate | Inject 0x001 without flag → error. With flag → ok |

---

## 10. Build Phases

```
Phase 1 — Types (no deps)
  backend/src/types/can.ts        ← TS mirror of can_protocol.h (12 IDs)
  ui/src/lib/can-decoder.ts       ← Client-side field definitions

Phase 2 — Backend (no hardware needed)
  Aedes broker + internal MQTT subscriber
  SQLite schema + queries
  Fastify REST API + WebSocket stream

Phase 3 — Firmware (needs ESP32-S3 + SN65HVD230)
  TWAI CAN RX + decode dispatch
  Wi-Fi STA + MQTT client (esp_mqtt)
  Command injector + periodic timer
  Stats + heartbeat tasks

Phase 4 — UI (dev against backend + simulator)
  Dashboard, Monitor, Injector, Stats pages

Phase 5 — Simulator
  MQTT device simulator with traffic profiles

Phase 6 — E2E Tests
  Playwright full-stack tests
```

---

## 11. File Inventory

```
debug-tool/
├── README.md
├── debug-tool-architecture.md    ← this file
│
├── backend/
│   ├── package.json
│   ├── tsconfig.json
│   └── src/
│       ├── index.ts
│       ├── config.ts
│       ├── mqtt/{broker.ts, client.ts}
│       ├── api/{can.ts, cmd.ts, recordings.ts, system.ts}
│       ├── ws/stream.ts
│       ├── db/{schema.ts, queries.ts, prune.ts}
│       └── types/can.ts
│
├── ui/
│   ├── package.json
│   ├── tsconfig.json
│   ├── vite.config.ts
│   ├── index.html
│   └── src/
│       ├── App.svelte, main.ts
│       ├── lib/{api.ts, ws.ts, can-decoder.ts}
│       ├── components/{Dashboard,CanMonitor,CanInjector,Stats}.svelte
│       └── stores/can.ts
│
├── debug-esp32/
│   ├── platformio.ini
│   ├── sdkconfig.defaults
│   └── src/
│       ├── main.cpp
│       ├── config.h
│       ├── can_monitor.cpp
│       ├── frame_decoder.cpp
│       ├── mqtt_bridge.cpp
│       ├── can_injector.cpp
│       └── stats.cpp
│
├── simulator/
│   ├── package.json
│   ├── tsconfig.json
│   └── src/
│       ├── sim.ts
│       ├── can-generator.ts
│       ├── cmd-handler.ts
│       └── profiles/default.json
│
└── e2e/
    ├── package.json
    ├── playwright.config.ts
    └── tests/
        ├── dashboard.spec.ts
        ├── can-monitor.spec.ts
        ├── can-inject.spec.ts
        └── stats.spec.ts
```
