# Debug Tool — System Architecture

A CAN bus diagnostic, monitoring, and hardware-in-the-loop test tool for the E-Trike vehicle control system. Connects to **both CAN buses** (high + low) via the ESP32-S3's dual TWAI controllers. Streams all 28 CAN messages to a web UI over USB, and can inject commands on either bus to simulate any node — Jetson, RT, SYS, MTR, or SYNTREE actuators.

---

## 1. Why Dual-Bus

The high bus carries system-level telemetry. The low bus carries actuator-level commands and feedback. To verify the full pipeline — inject a Jetson command, see RT's output, see the actuator response — you need both.

```
Inject 0x300 (high)  →  watch RT produce 0x204 + 0x169 (low)  →  see EPS-C respond 0x201 (low)
Inject 0x301 (high)  →  watch RT produce 0x205 (low)  →  see SYS produce 0x7B9 (low)  →  see SEB respond 0x721 (low)
```

Single-bus monitoring leaves you blind to RT's outputs and the actuators' responses. Since AI writes the code, the complexity of a second bus is just more switch cases — zero human maintenance cost.

**USB serial transport** — the ESP32 is always connected to the bench computer by USB. Faster than Wi-Fi, immune to EMI, no network config. Wi-Fi/MQTT available as a compile-time option using the same JSON Lines protocol.

```
ESP32-S3 ──USB──► Computer ──WebSocket──► Browser
  │                │
  │                ├── Backend reads COM port
  │                ├── Stores to SQLite
  │                └── Fans out to UI via WebSocket (:3000/ws)
  │
  ├── High-Level CAN Bus (TWAI0, GPIO 5/4, 500 kbit/s)
  └── Low-Level CAN Bus  (TWAI1, GPIO 17/16, 500 kbit/s)
```

---

## 2. Topology

```
┌── Developer Machine ────────────────────────────────────────────────────┐
│                                                                          │
│  Browser (localhost:5173)                                                │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐                   │
│  │Dashboard │ │Monitor   │ │Injector  │ │Stats     │                   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘                   │
│       │              │             │                                    │
│       └──────────────┼─────────────┘                                    │
│                      │ WebSocket + REST (:3000)                         │
│  ┌───────────────────┴──────────────────────────────────────────────┐   │
│  │  Fastify Backend (:3000)                                          │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────┐ ┌──────────────────┐ │   │
│  │  │ REST API │ │WebSocket │ │Serial Reader │ │ SQLite           │ │   │
│  │  └──────────┘ └──────────┘ └──────┬───────┘ └────────┬─────────┘ │   │
│  └───────────────────────────────────┼──────────────────┼───────────┘   │
│                                      │ USB (CDC ACM)    │ DB writes     │
└──────────────────────────────────────┼──────────────────┼──────────────┘
                                       │ COM3              │
┌── ESP32-S3 (debug-esp32) ────────────┼──────────────────┼──────────────┐
│                                      │                   │               │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │  FreeRTOS Scheduler                                               │    │
│  │                                                                   │    │
│  │  can_rx_high (prio 5)    can_rx_low (prio 5)                     │    │
│  │  TWAI0 → dec_hi_q        TWAI1 → dec_lo_q                        │    │
│  │       │                        │                                  │    │
│  │       └────────┬───────────────┘                                  │    │
│  │                │                                                  │    │
│  │         can_decode (prio 4)                                       │    │
│  │         ID dispatch → JSON → serial_tx_q                          │    │
│  │                │                                                  │    │
│  │         serial_tx (prio 3)    serial_rx (prio 3)                  │    │
│  │         printf JSON Lines     stdin → parse → cmd_q               │    │
│  │                                      │                            │    │
│  │                               can_inject (prio 3)                 │    │
│  │                               cmd_q → TWAI0/1 TX                  │    │
│  │                                                                   │    │
│  │  stats (prio 1, 1 Hz)     status (prio 2, 5 s)                   │    │
│  └──────────────────────────────────────────────────────────────────┘    │
│                                                                         │
│  ┌──────────────┐  ┌──────────────┐                                     │
│  │  TWAI0       │  │  TWAI1       │                                     │
│  │  GPIO 5,4    │  │  GPIO 17,16  │                                     │
│  │  High CAN Bus│  │  Low CAN Bus │                                     │
│  └──────┬───────┘  └──────┬───────┘                                     │
└─────────┼──────────────────┼────────────────────────────────────────────┘
          │                  │
    High-Level CAN      Low-Level CAN
    (500 kbit/s)        (500 kbit/s)
```

---

## 3. CAN Message Catalog — All 28 IDs

### 3.1 High-Level CAN Bus (12 IDs)

| ID | Name | Sender | Period | DLC | Decoded Fields | Inject |
|----|------|--------|--------|-----|----------------|--------|
| `0x001` | SAFETY_ESTOP | any | Event | 0 | — | ✅ |
| `0x011` | SYS_SAFETY_STS | RT (fwd) | 5 Hz | 2 | `estop_active`, `heartbeat_ok` | ✅ |
| `0x120` | SYS_THROTTLE_STS | RT (fwd) | 100 Hz | 2 | `speed_mmps` | ✅ |
| `0x206` | MTR_MOTOR_FBK | RT (fwd) | 50 Hz | 4 | `actual_speed_mmps`, `gear_state`, `fault_flags` | ✅ |
| `0x210` | RT_STATE_RPT | RT | 10 Hz | 3 | `mode`, `steer_valid`, `reversing` | ✅ |
| `0x220` | RT_PID_RPT | RT | — (reserved) | 6 | `speed_setpoint`, `speed_measured`, `pid_output` | — |
| `0x300` | HOST_DRIVE_CMD | Jetson | ≤100 Hz | 8 | `speed_mmps`, `yaw_rate_mrad_s`, `gear` | ✅ |
| `0x301` | HOST_BRAKE_REQ | Jetson | Demand | 4 | `brake_pressure_kpa` | ✅ |
| `0x302` | HOST_LIGHT_CMD | Jetson | Change | 1 | `left_turn`, `right_turn`, `brake_light`, `headlight` | ✅ |
| `0x400` | HOST_OBSTACLE_DIST | Jetson | 10 Hz | 4 | `distance_mm` | ✅ |
| `0x600` | SYS_DIAG_RPT | RT (fwd) | 1 Hz | 8 | `mode`, `brake_engaged`, `hb_ok`, `estop_active`, `free_heap_kb`, `tec`, `rec` | — |
| `0x7FC` | JETSON_HEARTBEAT | Jetson | 2 Hz | 1 | `alive_ctr` | ✅ |
| `0x7FD` | RT_HEARTBEAT | RT | 2 Hz | 1 | `alive_ctr` | — |

### 3.2 Low-Level CAN Bus (22 IDs)

| ID | Name | Sender | Period | DLC | Decoded Fields | Inject |
|----|------|--------|--------|-----|----------------|--------|
| `0x001` | SAFETY_ESTOP | any | Event | 0 | — | ✅ |
| `0x011` | SYS_SAFETY_STS | SYS | 5 Hz | 2 | `estop_active`, `heartbeat_ok` | ✅ |
| `0x012` | SYS_DCDC_CMD | SYS | Change | 1 | `enable` | — |
| `0x110` | SYS_MODE_CMD | SYS | Change | 1 | `mode` | ✅ |
| `0x120` | SYS_THROTTLE_STS | MTR | 100 Hz | 2 | `speed_mmps` | ✅ |
| `0x169` | VCU_SES_REQ | RT | 50 Hz | 8 | `target_angle`, `target_speed`, `control_enable`, `rolling_counter`, `checksum` | ✅ |
| `0x201` | SES_STATUS | EPS-C | 100 Hz | 8 | `angle_status`, `str_angle`, `tgt_angle_spd`, `error_status` | ✅ |
| `0x202` | SES_ErrInfo | EPS-C | 10 Hz | 8 | 25 fault flags (8× L3) | — |
| `0x203` | SES_Version | EPS-C | 1 Hz | 8 | SW + HW version | — |
| `0x204` | RT_DRIVE_CMD | RT | 100 Hz | 5 | `motor_speed_mmps`, `gear` | ✅ |
| `0x205` | RT_BRAKE_CMD | RT | 50 Hz | 4 | `brake_pressure_kpa` | ✅ |
| `0x206` | MTR_MOTOR_FBK | MTR | 50 Hz | 4 | `actual_speed_mmps`, `gear_state`, `fault_flags` | ✅ |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | Change | 1 | light bitfield | ✅ |
| `0x600` | SYS_DIAG_RPT | SYS | 1 Hz | 8 | `mode`, `brake_engaged`, `hb_ok`, `estop_active`, `free_heap_kb`, `tec`, `rec` | — |
| `0x6FA` | SES_Test | EPS-C | 100 Hz | 8 | motor current, ECU temp, supply voltage | — |
| `0x6FB` | SEB_Test | SEB | 100 Hz | 8 | motor current, ECU temp, supply voltage | — |
| `0x721` | SEB_STATUS | SEB | 100 Hz | 8 | `stroke_value`, `pressure_value`, `angle_value`, `error_status` | ✅ |
| `0x731` | SEB_ErrInfo | SEB | 10 Hz | 8 | 23 fault flags (16× L3) | — |
| `0x741` | SEB_Version | SEB | 1 Hz | 8 | SW + HW version | — |
| `0x7B9` | VCU_SEB_REQ | RT/SYS | 50 Hz | 8 | `stroke_req`, `pressure_req`, `control_mode`, `rolling_counter`, `checksum` | ✅ |
| `0x7FD` | RT_HEARTBEAT | RT | 2 Hz | 1 | `alive_ctr` | — |
| `0x7FE` | SYS_HEARTBEAT | SYS | 10 Hz | 1 | `alive_ctr` | — |

---

## 4. USB Serial Protocol — JSON Lines

One JSON object per line, `\n` delimited.

### 4.1 CAN Frame (ESP32 → Backend)

```json
{"ts":890123,"bus":"low","id":"0x204","name":"RT_DRIVE_CMD","dlc":5,"data":[0,0,7,208,1,0,0,0],"decoded":{"motor_speed_mmps":2000,"gear":1,"gear_name":"D"}}
```

The `bus` field is `"high"` or `"low"`. All other fields match the single-bus format.

### 4.2 Stats (ESP32 → Backend, 1 Hz)

```json
{"type":"stats","uptime_s":3600,"high":{"total":89120,"fps":247,"load_pct":16.2,"tec":0,"rec":0,"by_id":{"0x300":18000,"0x120":36000}},"low":{"total":152340,"fps":423,"load_pct":28.5,"tec":0,"rec":0,"by_id":{"0x120":36000,"0x204":36000,"0x201":36000,"0x7B9":18000}}}
```

### 4.3 Command (Backend → ESP32)

```json
{"cmd":"send","bus":"low","id":"0x204","dlc":5,"data":[0,0,7,208,1,0,0,0]}
```

```json
{"cmd":"send_periodic","action":"start","bus":"high","id":"0x300","dlc":8,"data":[0,0,7,208,0,0,100,1],"interval_ms":20,"count":5000}
```

### 4.4 Command Ack (ESP32 → Backend)

```json
{"type":"cmd_ack","cmd":"send","status":"ok"}
```

---

## 5. Backend (`backend/`)

### 5.1 Stack

| Layer | Choice |
|-------|--------|
| Runtime | Node.js 22 |
| Language | TypeScript 5.x |
| Web framework | Fastify 5.x |
| Serial port | `serialport` npm |
| Database | better-sqlite3 |
| Validation | Zod |

### 5.2 REST API

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/status` | Backend health + ESP32 connected + uptime + bus stats |
| `GET` | `/api/can/ids` | All 28 CAN IDs with names, bus, DLC, field defs, enum labels |
| `GET` | `/api/can/frames` | Query history: `?bus=low&id=0x204&since=<ts>&limit=500` |
| `GET` | `/api/can/stats` | Latest per-bus statistics |
| `POST` | `/api/cmd/send` | Inject CAN frame: `{bus, id, dlc, data}` |
| `POST` | `/api/cmd/periodic` | Start/stop periodic injection |
| `GET` | `/api/recordings` | List recording sessions |
| `POST` | `/api/recordings` | Start recording: `{label}` |
| `PUT` | `/api/recordings/:id/stop` | Stop recording |
| `GET` | `/api/recordings/:id/frames` | Get recording frames (paginated) |
| `DELETE` | `/api/recordings/:id` | Delete recording |
| `GET` | `/api/templates` | Pre-built injection templates |

### 5.3 WebSocket (`/ws`)

```json
// Server → Client
{ "type": "can_frame",  "payload": { /* CAN frame with bus field */ } }
{ "type": "stats",      "payload": { /* per-bus stats */ } }
{ "type": "cmd_ack",    "payload": { "cmd": "send", "status": "ok" } }
{ "type": "status",     "payload": { "esp32_connected": true } }

// Client → Server (filter)
{ "type": "filter", "buses": ["low"], "ids": ["0x204", "0x169", "0x201"] }
```

### 5.4 SQLite Schema

```sql
CREATE TABLE can_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    ts_device   INTEGER NOT NULL,
    bus         TEXT NOT NULL CHECK(bus IN ('high','low')),
    can_id      TEXT NOT NULL,
    can_name    TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    decoded     TEXT NOT NULL
);
CREATE INDEX idx_frames_bus_id_ts ON can_frames(bus, can_id, ts_real);
CREATE INDEX idx_frames_ts ON can_frames(ts_real);

CREATE TABLE injected_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    bus         TEXT NOT NULL,
    can_id      TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    status      TEXT
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

### 5.5 Serial Port Reader

```typescript
const port = new SerialPort({ path: "COM3", baudRate: 115200 });
const rl = readline.createInterface({ input: port });

rl.on("line", (json) => {
    const msg = JSON.parse(json);
    if (msg.type === "stats")   { cache stats; fan WS; }
    if (msg.type === "status")  { update device state; fan WS; }
    if (msg.type === "cmd_ack") { resolve pending command; }
    if (msg.id)                 { write to DB; fan WS; }
});

// Send command
function sendCommand(cmd: object) {
    port.write(JSON.stringify(cmd) + "\n");
}
```

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
│ Low Bus      │ High Bus     │ ESTOP      │ Mode         │
│ 423 fps      │ 247 fps      │ ⬜ CLEAR   │ AUTO         │
│ 28.5% load   │ 16.2% load   │            │              │
├──────────────┴──────────────┴────────────┴──────────────┤
│  Latest Values                                          │
│  Speed: 1500 mm/s  │  Steer: 12.3°  │  Brake: 0 kPa    │
│  Gear: D           │  HB RT: ✅     │  HB SYS: ✅      │
└─────────────────────────────────────────────────────────┘
```

#### CAN Monitor

- Real-time scrolling table with color-coded rows: **low bus = blue**, **high bus = orange**
- Columns: timestamp, bus, CAN ID, name, decoded payload
- Filter by bus (high/low/both), CAN ID (multi-select), time range
- Click row → expand raw hex + all decoded fields
- Pause/resume, export as JSON/CSV

#### CAN Injector

- **Bus selector** (high/low) + CAN ID dropdown → dynamic form with labeled fields
- Raw hex preview updates as fields are edited
- "Send Once" and "Send Periodic" buttons
- Pre-built templates (e.g. "Drive at 1.5 m/s", "Steer 10° left", "Full brake")
- Command history with status (ok/error)
- **Keyboard real-time control** — when injector has focus, keys drive directly:

| Key | Action | Bus | Frame |
|-----|--------|-----|-------|
| `W` | Speed +200 mm/s | high | `0x300` |
| `S` | Speed −200 mm/s | high | `0x300` |
| `A` | Steer left | high | `0x300` yaw +87 mrad/s |
| `D` | Steer right | high | `0x300` yaw −87 mrad/s |
| `↑` `↓` | Speed ±100 mm/s fine | high | `0x300` |
| `←` `→` | Yaw ±50 mrad/s fine | high | `0x300` |
| `Space` | **ESTOP** (double-tap) | both | `0x001` |
| `B` | Brake 5000 kPa | high | `0x301` |
| `R` | Release brake | high | `0x301` |
| `G` | Cycle gear N→D→S→R | high | `0x300` |
| `Esc` | Kill — zero everything | high | `0x300` + `0x301` zeroed |

Each keypress sends an immediate CAN frame — no form, no submit. Client tracks accumulated drive state.
Holding a key repeats at OS key-repeat rate (~30 Hz). "KB: ON/OFF" indicator in toolbar, click to toggle.

#### Statistics

- Two line charts: frame rate per bus (5 min window)
- Bar chart: top-10 CAN IDs by count
- Bus load gauges (both buses)
- TEC/REC display per bus (alert if non-zero)

---

## 7. Firmware (`debug-esp32/`)

### 7.1 Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3-DevKitC-1 |
| CAN High | TWAI0 (built-in controller 0) — GPIO 5 (TX), GPIO 4 (RX) — SN65HVD230 |
| CAN Low | TWAI1 (built-in controller 1) — GPIO 17 (TX), GPIO 16 (RX) — SN65HVD230 |
| USB | Built-in USB-UART (CDC ACM) — power + data |
| Termination | 120Ω on both buses (provided by existing bus termination) |

### 7.2 FreeRTOS Tasks

ESP-IDF requires FreeRTOS — every app runs on it. The debug tool uses clean task separation with queues because AI writes the boilerplate. Zero human maintenance cost for clean architecture.

| Task | Prio | Stack | Period | Purpose |
|------|------|-------|--------|---------|
| `can_rx_high` | 5 | 4096 | Blocking | TWAI0 receive → push to `dec_hi_q` (32 deep) |
| `can_rx_low` | 5 | 4096 | Blocking | TWAI1 receive → push to `dec_lo_q` (32 deep) |
| `can_decode` | 4 | 6144 | Event-driven | Drain both decode queues → dispatch ID → `from_frame()`/`unpack()` → serialize JSON → push to `serial_tx_q` |
| `serial_tx` | 3 | 4096 | Event-driven | Pop `serial_tx_q` → `printf(json, "\n")` over USB CDC |
| `serial_rx` | 3 | 4096 | Blocking | `select()` on stdin → read JSON line → parse command → push to `cmd_q` |
| `can_inject` | 3 | 4096 | Event-driven | Pop `cmd_q` → `twai_transmit()` on correct bus. Also manages periodic injection timer. |
| `stats` | 1 | 4096 | 1000 ms | Per-ID per-bus counters, bus load %, TEC/REC → push stats JSON to `serial_tx_q` |
| `status` | 2 | 3072 | 5000 ms | Heap free, uptime, stack HWM → push status JSON to `serial_tx_q` |

**Total stack:** ~32 KB. ESP32-S3 has 512 KB SRAM.

### 7.3 Frame Decoding

The decode task uses a dispatch table keyed on CAN ID, calling the appropriate `from_frame()` or `unpack()` from `shared/can/can_protocol.h` — the single source of truth. All 28 IDs are decoded in firmware.

```cpp
void decode_frame(const can::Frame& fr, bool is_low_bus, char* buf, size_t max_len) {
    const char* bus = is_low_bus ? "low" : "high";
    switch (fr.id) {
        case can::kIdHostDriveCmd: {
            auto cmd = can::HostDriveCmd::from_frame(fr);
            snprintf(buf, max_len,
                R"({"ts":%lu,"bus":"%s","id":"0x300","name":"HOST_DRIVE_CMD",)"
                R"("dlc":8,"data":[%d,%d,%d,%d,%d,%d,%d,%d],)"
                R"("decoded":{"speed_mmps":%d,"yaw_rate_mrad_s":%d,"gear":%d,"gear_name":"%s"}})",
                now_ms(), bus,
                fr.data[0],fr.data[1],fr.data[2],fr.data[3],
                fr.data[4],fr.data[5],fr.data[6],fr.data[7],
                cmd.speed_mmps, cmd.yaw_rate_mrad_s, cmd.gear,
                can::gear_name(static_cast<can::Gear>(cmd.gear)));
            break;
        }
        case can::kIdRtDriveCmd: {
            auto cmd = can::RtDriveCmd::from_frame(fr);
            snprintf(buf, max_len, /* ... */);
            break;
        }
        case can::kIdVcuSesReq: {
            auto cmd = can::VcuSesReq::unpack(fr.data);
            snprintf(buf, max_len, /* SYNTREE little-endian fields */);
            break;
        }
        // ... all 28 IDs + unknown-ID fallback (raw hex only)
    }
}
```

### 7.4 Command Injection Safety

- Periodic injection: max 10 kHz rate, max 50,000 frames per command
- ESTOP (`0x001`) requires `"confirm_estop":true` in command
- All periodic injections auto-cancel on USB disconnect (CDC DTR drop)
- `can_inject` task writes to TWAI TX on the correct bus based on command's `bus` field
- Every injected frame counted in per-bus stats

### 7.5 Transport Config

```cpp
// config.h
enum class Transport { USB_CDC, WIFI_MQTT };
constexpr Transport kTransport = Transport::USB_CDC;
```

Wi-Fi/MQTT is a compile-time alternative. The JSON Lines frame format is identical — only the I/O layer changes (serial port vs MQTT topics). Switch the enum and reflash.

---

## 8. Simulator (`simulator/`)

Hardware-free device simulator. Connects to backend via TCP socket using the same JSON Lines protocol.

```
sim.ts
├── Sends {"type":"status","esp32_connected":true} on connect
├── Generates synthetic CAN traffic on both buses:
│   ├── High: 0x120 (100 Hz), 0x300 (50 Hz), 0x210 (10 Hz), 0x011 (5 Hz),
│   │         0x206 (50 Hz), 0x400 (10 Hz), 0x600 (1 Hz), 0x7FC (2 Hz), 0x7FD (2 Hz)
│   └── Low:  0x120 (100 Hz), 0x204 (100 Hz), 0x201 (100 Hz), 0x169 (50 Hz),
│              0x7B9 (50 Hz), 0x206 (50 Hz), 0x721 (100 Hz), 0x7FD (2 Hz),
│              0x7FE (10 Hz), 0x011 (5 Hz), 0x600 (1 Hz), 0x110 (change),
│              0x6FA (100 Hz), 0x6FB (100 Hz), 0x202 (10 Hz), 0x731 (10 Hz)
├── Handles commands → acks with cmd_ack
├── Periodic injection → starts/stops synthetic generator
└── Traffic profile configurable via profiles/<name>.json
```

---

## 9. E2E Tests (`e2e/`)

Playwright tests against backend + simulator + UI:

| Test | What It Verifies |
|------|-----------------|
| Dashboard shows online | Simulator → green dot, both bus gauges render |
| CAN frames stream to monitor | Both buses → WebSocket → color-coded table rows |
| Bus filter works | Select "low only" → only blue rows shown |
| Inject single frame | UI → REST → serial → ack shown |
| Inject on low bus | Select low bus → inject 0x204 → appears in low bus monitor |
| Periodic injection | Start → frames at rate → stop → no more frames |
| Keyboard drive | Focus injector → press W → 0x300 frame sent immediately |
| ESTOP double-tap | Single Space → warning. Double Space → 0x001 sent |
| Recording | Start → frames captured from both buses → stop → correct count |
| Export recording | Download button → valid JSON with bus field |
| Stats dual-bus | Both bus charts render with distinct data |

---

## 10. Build Phases

```
Phase 1 — Types (no deps)
  backend/src/types/can.ts          ← TS mirror of can_protocol.h (28 IDs, both buses)
  ui/src/lib/can-decoder.ts         ← Client-side field definitions

Phase 2 — Backend (no hardware needed)
  Serial port reader (JSON Lines)
  SQLite schema + queries
  Fastify REST API + WebSocket stream

Phase 3 — Firmware (needs ESP32-S3 + 2× SN65HVD230)
  Dual TWAI init + CAN RX tasks
  Frame decoder (28-ID dispatch, shared/can/can_protocol.h)
  Serial I/O (printf JSON Lines, stdin readline)
  Command injector (per-bus TWAI TX, periodic timer)
  Stats + status tasks

Phase 4 — UI (dev against backend + simulator)
  Dashboard, Monitor (dual-bus color), Injector (bus selector + keyboard), Stats

Phase 5 — Simulator
  JSON Lines device simulator with dual-bus traffic profiles

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
│       ├── serial/reader.ts      ← SerialPort + readline
│       ├── api/{can.ts, cmd.ts, recordings.ts, system.ts}
│       ├── ws/stream.ts
│       ├── db/{schema.ts, queries.ts, prune.ts}
│       └── types/can.ts          ← 28 CAN IDs, both buses
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
│       ├── main.cpp              ← app_main: dual TWAI init, queue + task creation
│       ├── config.h               ← CAN GPIOs, timing, transport switch
│       ├── can_monitor.cpp        ← can_rx_high + can_rx_low tasks
│       ├── frame_decoder.cpp      ← 28-ID dispatch → JSON
│       ├── serial_io.cpp          ← serial_tx (printf) + serial_rx (stdin select)
│       ├── can_injector.cpp       ← cmd_q → TWAI0/1 TX, periodic timer
│       └── stats.cpp              ← Per-ID per-bus counters, bus load, TEC/REC
│
├── simulator/
│   ├── package.json
│   ├── tsconfig.json
│   └── src/
│       ├── sim.ts
│       ├── can-generator.ts       ← Dual-bus synthetic traffic
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
