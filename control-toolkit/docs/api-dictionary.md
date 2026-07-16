# Control Toolkit — Backend API dictionary

Base URL (local): **`http://127.0.0.1:8001`**  
API prefix: **`/api/v1`**  
Interactive OpenAPI: **http://127.0.0.1:8001/docs**

All JSON request/response bodies use `Content-Type: application/json` unless noted.  
Errors use RFC 9457-style Problem Details (`detail` / `title` / code) via session/error handlers. Common status codes: **400**, **404**, **409**, **503**.

**Gates**

| Gate | Applies to |
|------|------------|
| Backend `ready` | Most operations after lifecycle startup |
| **Bench TX enabled** | Any bus TX: inject, analysis host-drive, control intent/direct start, HMI TX, synthetic start |
| Session `revision` | Optimistic concurrency on session mutations (when body includes `expected_revision`) |
| Ownership lease | One producer per `(bus, can_id)` during TX |

---

## Quick index

| Method | Path | Group |
|--------|------|--------|
| `GET` | `/api/v1/status` | Health |
| `GET` | `/api/v1/state` | Observation |
| `GET` | `/api/v1/history` | Observation |
| `GET` | `/api/v1/topology` | Observation |
| `WS` | `/api/v1/stream` | Observation |
| `GET` | `/api/v1/settings` | Settings |
| `GET` | `/api/v1/sessions` | Session |
| `GET` | `/api/v1/sessions/profiles` | Session |
| `POST` | `/api/v1/sessions` | Session |
| `POST` | `/api/v1/sessions/{id}/profile` | Session |
| `POST` | `/api/v1/sessions/{id}/bench-tx` | Session |
| `POST` | `/api/v1/sessions/{id}/stop-all` | Session |
| `DELETE` | `/api/v1/sessions/{id}` | Session |
| `POST` | `/api/v1/sessions/{id}/vehicle-view` | Session |
| `POST` | `/api/v1/sessions/{id}/leases` | Ownership |
| `POST` | `/api/v1/sessions/{id}/leases/renew` | Ownership |
| `DELETE` | `/api/v1/sessions/{id}/leases/{lease_id}` | Ownership |
| `GET` | `/api/v1/control/status` | Control |
| `POST` | `/api/v1/control/intent` | Control |
| `POST` | `/api/v1/control/release` | Control |
| `POST` | `/api/v1/control/direct` | Control |
| `POST` | `/api/v1/analysis/host-drive` | Analysis |
| `POST` | `/api/v1/analysis/stop` | Analysis |
| `POST` | `/api/v1/hmi/mode` | HMI |
| `POST` | `/api/v1/hmi/power` | HMI |
| `POST` | `/api/v1/injections/preview` | Inject |
| `POST` | `/api/v1/injections` | Inject |
| `DELETE` | `/api/v1/injections/{job_id}` | Inject |
| `GET` | `/api/v1/protocol/messages` | Protocol |
| `GET` | `/api/v1/protocol/dictionary` | Protocol |
| `POST` | `/api/v1/protocol/dictionary/refresh` | Protocol |
| `GET` | `/api/v1/protocol/messages/{bus}/{can_id}` | Protocol |
| `GET` | `/api/v1/protocol/messages/{bus}/{can_id}/layout` | Protocol |
| `GET` | `/api/v1/synthetic-peers` | Synthetic |
| `POST` | `/api/v1/synthetic-peers/start` | Synthetic |
| `POST` | `/api/v1/synthetic-peers/stop` | Synthetic |
| `GET` | `/api/v1/recordings` | Recording |
| `POST` | `/api/v1/recordings` | Recording |
| `DELETE` | `/api/v1/recordings/{id}` | Recording |
| `GET` | `/api/v1/recordings/{id}` | Recording |
| `GET` | `/api/v1/recordings/{id}/export` | Recording |
| `GET` | `/api/v1/events` | Diagnostics |
| `GET` | `/api/v1/events/{event_id}` | Diagnostics |
| `GET` | `/api/v1/episodes` | Diagnostics |
| `GET` | `/api/v1/evidence/{evidence_id}` | Diagnostics |
| `GET` | `/api/v1/logs` | Audit log |
| `GET` | `/api/v1/logs/stats` | Audit log |
| `GET` | `/api/v1/logs/{log_id}` | Audit log |
| `DELETE` | `/api/v1/logs` | Audit log |
| `GET` | `/api/v1/tests` | Verification |
| `POST` | `/api/v1/tests` | Verification |
| `GET` | `/api/v1/tests/{test_id}` | Verification |

---

## 1. Health & observation

### `GET /api/v1/status`

Service health, adapter, protocol hashes, session snapshot.

**Response (shape)**

| Field | Type | Meaning |
|-------|------|---------|
| `service` | string | Title |
| `version` | string | Backend version |
| `ready` | bool | Lifecycle ready |
| `wire_hash` / `semantic_hash` / `network_hash` | string | Protocol identity |
| `profile` | string | Config default profile |
| `catalog.messages` / `instances` | int | Catalog sizes |
| `adapter` | object | Transport health, channels high/low |
| `session` | object | Active session (see Session state) |

---

### `GET /api/v1/state`

Latest decoded message snapshot (same conceptual payload as stream `state` events).

**Response:** `LatestStateSnapshot` — `sequence`, `wire_hash`, `messages[]` (bus, can_id, name, signals, freshness, …).

---

### `GET /api/v1/history?limit=200`

Chronological raw frame ring (bounded).

| Query | Default | Range |
|-------|---------|--------|
| `limit` | 200 | 1…4096 |

**Response:** `{ metrics, frames[] }` with `global_sequence`, `bus`, `can_id`, `data_hex`, `direction`, `source`, timestamps.

---

### `GET /api/v1/topology`

ECU / bus topology snapshot (liveness from heartbeats).

---

### `WS /api/v1/stream`

Live observation channel. Client should open one connection per UI.

**Server → client messages**

| `type` | When | Payload highlights |
|--------|------|--------------------|
| `hello` | Connect | `wire_hash` |
| `state` | Initial + periodic (~batch Hz) | `sequence`, `wire_hash`, `messages[]`, `session?`, `initial?` |
| `heartbeat` | ~every `stream_heartbeat_ms` | `monotonic_ns`, `wire_hash` |
| `ack` | After client text | `echo` |

**Client → server**

| Payload | Effect |
|---------|--------|
| Text string (ping) | `ack` with same text |

Browser via Vite: `ws://127.0.0.1:5173/api/v1/stream` (proxied to 8001).

---

## 2. Settings

### `GET /api/v1/settings`

Aggregated live configuration for the Settings UI: transport modes, profiles, physical adapter probe, session, adapter, protocol, runtime, history, control, service metadata.

---

## 3. Sessions

### Session state (common object)

| Field | Type | Notes |
|-------|------|--------|
| `session_id` | string \| null | Active id |
| `profile` | `pure_software` \| `bench_test` \| `full_vehicle` | Transport profile |
| `phase` | enum | `stopped` / `running` / … |
| `bench_tx` | `disabled` \| `enabled` | TX gate |
| `revision` | int | Optimistic concurrency |
| `destination` | `virtual` \| `physical` | |
| `requested_mode` / `confirmed_mode` | string \| null | UI header (not firmware authority) |
| `requested_power` / `confirmed_power` | string \| null | |
| `estop_active` | bool \| null | |
| `recording` | bool | |
| `leases` / `jobs` | string[] | Active ids |
| `capabilities` | string[] | e.g. observe, inject |

### `GET /api/v1/sessions`

```json
{ "session": { /* SessionState */ } }
```

### `GET /api/v1/sessions/profiles`

Lists **transport_modes** (`computer` / `real`), **profiles**, and **physical_adapter** availability (CANalyst probe).

### `POST /api/v1/sessions`

Create / open session.

**Body**

| Field | Type | Default |
|-------|------|---------|
| `profile` | profile enum | `pure_software` |
| `capabilities` | string[] | `["observe","inject"]` |
| `test_session_id` | string \| null | null |

**Response:** `{ "session": SessionState }`

### `POST /api/v1/sessions/{session_id}/profile`

**Body:** `{ "profile", "expected_revision"?, "confirm": false }` — `confirm` must be true for controlled transition.

### `POST /api/v1/sessions/{session_id}/bench-tx`

**Body:** `{ "enabled": bool, "expected_revision"? }`

Enable before any bus TX.

### `POST /api/v1/sessions/{session_id}/stop-all`

**Body (optional):** `{ "expected_revision"? }`

Stops motion / jobs (session stop-all path).

### `DELETE /api/v1/sessions/{session_id}`

Close session.

**Body (optional):** `{ "expected_revision"?, "outcome"? }`

### `POST /api/v1/sessions/{session_id}/vehicle-view`

Update UI header fields only.

**Body (all optional):** `requested_mode`, `confirmed_mode`, `requested_power`, `confirmed_power`, `estop_active`, `recording`

---

## 4. Ownership leases

### `POST /api/v1/sessions/{session_id}/leases`

**Body**

| Field | Type |
|-------|------|
| `bus` | `high` \| `low` |
| `can_id` | int |
| `owner` | string |
| `resource` | string \| null |
| `ttl_s` | float (default 5) |

**409** on conflict.

### `POST /api/v1/sessions/{session_id}/leases/renew`

**Body:** `{ "lease_id", "ttl_s"? }`

### `DELETE /api/v1/sessions/{session_id}/leases/{lease_id}`

Release lease.

---

## 5. Control (motion)

High Host kinematics and Low direct actuators are **exclusive** on the server.

### `GET /api/v1/control/status`

```json
{ "control": { /* snapshot */ } }
```

Snapshot highlights: `active`, `mode`, `method` (`high_kinematics` \| `low_direct` \| `none`), `bus`, `shaped_speed_mmps`, `shaped_yaw_mrad_s`, `gear`, `direct_channels`, `job_id`, `loss_reason`, `paths`.

### `POST /api/v1/control/intent`

Keyboard / Drive teleop → periodic **High** `HOST_DRIVE_CMD` (0x300).

**Requires Bench TX** (except pure release paths).

**Body**

| Field | Type | Default | Notes |
|-------|------|---------|--------|
| `sequence` | int ≥ 0 | required | Monotonic per continuous **source** |
| `source` | string | `"keyboard"` | e.g. `control_keyboard`, `drive_console` |
| `mode` | string | `"kinematics"` | Only kinematics for this endpoint |
| `throttle` | float | 0 | −1…1 |
| `steer` | float | 0 | −1…1 |
| `gear` | int \| null | null | 0=N 1=D 2=S 3=R |
| `hard_brake` | bool | false | Forces N / zero throttle |
| `estop` | bool | false | Dual-bus SAFETY_ESTOP + release |

**409** `control.stale_sequence` if same source and sequence goes backward.  
**409** if Bench TX disabled.

**Response:** `{ "control": snapshot }` (+ `estop` results if ESTOP).

### `POST /api/v1/control/release`

**Body (optional):** `{ "reason": "client_release" }`

Stops kinematics + direct jobs; releases TX ownership leases.

### `POST /api/v1/control/direct`

Low-bus continuous actuator TX.

**Body**

| Field | Type | Notes |
|-------|------|--------|
| `channel` | `motor` \| `steering` \| `brake` | |
| `enabled` | bool | false stops channel |
| `values` | object | Channel-specific (below) |
| `period_ms` | float \| null | Default: motor 10, steer/brake 20 |

**Values by channel**

| Channel | Key | Bus frame | Typical values |
|---------|-----|-----------|----------------|
| `motor` | `rt:rt_drive_cmd` | Low 0x204 | `motor_speed_mmps`, `gear` |
| `steering` | `ses:vcu_ses_req` | Low 0x169 | `target_angle_raw`, `target_speed_raw` (enables forced ON) |
| `brake` | `seb:vcu_seb_req` | Low 0x7B9 | `pressure_request_raw`, `control_mode`, `stroke_request_raw` (enables forced ON) |

Start requires Bench TX; stop does not.

---

## 6. Analysis (Host drive inject)

### `POST /api/v1/analysis/host-drive`

**Requires Bench TX.**

**Body**

| Field | Type | Range / default |
|-------|------|-----------------|
| `speed_mmps` | int | −500…3000 (0) |
| `yaw_rate_mrad_s` | int | −3000…3000 (0) |
| `gear` | int | 0…3 (1) |
| `period_ms` | float \| null | null = oneshot; >0 = periodic |

**Response oneshot:** `ok`, `mode: "oneshot"`, `disposition`, `request_id`, `lease_id`, `can_id`, `data_hex`, `values`  
**Response periodic:** `ok`, `mode: "periodic"`, job ids from synthetic host-drive helper

**409** if ownership / gate rejects.

### `POST /api/v1/analysis/stop`

Stop analysis / synthetic host-drive jobs: `{ "ok", "stopped": n }`.

---

## 7. HMI

Both require **Bench TX** when enabling TX.

### `POST /api/v1/hmi/mode`

**Body:** `{ "req_mode": 0|1, "enabled": true }` — 0=MANUAL, 1=AUTO  
Periodic High `HMI_MODE_REQ` @ 1 s when enabled.

### `POST /api/v1/hmi/power`

**Body:** `{ "req_start": 0|1, "enabled": true }` — 0=OFF, 1=ON  
Periodic High `HMI_PWR_REQ` @ 1 s when enabled.

---

## 8. Injections

### `POST /api/v1/injections/preview`

Encode only (no Bench TX required).

**Body:** `{ "bus", "key"?, "can_id"?, "values", "period_ms"?, "counter_field"?, "owner"? }`

**Response:** encode status, `data_hex`, `signals`, warnings.

### `POST /api/v1/injections`

**Requires Bench TX.**

Same body. If `period_ms > 0` → schedule job; else oneshot TX.

**409** `injection.rejected` on ownership/gate failure.

### `DELETE /api/v1/injections/{job_id}`

Cancel scheduled inject. **404** if unknown.

---

## 9. Protocol / CAN dictionary

### `GET /api/v1/protocol/messages`

Catalog instances: hashes + flat instance list.

### `GET /api/v1/protocol/dictionary`

Full dictionary for UI: `messages[]` with `bus`, `id`, `name`, `fields[]` (layout), `source` (`yaml` \| `vendor_codec_map`), signal count, hashes.

Opaque SES/SEB frames are filled from vendor field maps when YAML has no `layout.fields`.

### `POST /api/v1/protocol/dictionary/refresh`

Reload generated protocol package; returns dictionary + `refreshed: true`.

### `GET /api/v1/protocol/messages/{bus}/{can_id}`

One message + `bit_grid`. `can_id` accepts hex (`0x300`) or decimal.

### `GET /api/v1/protocol/messages/{bus}/{can_id}/layout`

`bit_grid` + optional **live** signal overlay from latest state.

---

## 10. Synthetic peers

### `GET /api/v1/synthetic-peers`

`{ "available": [...], "running": [...] }`

### `POST /api/v1/synthetic-peers/start`

**Body:** `{ "names": ["..."] }` — **required** (no full-vehicle auto-start).  
**Requires Bench TX.**

### `POST /api/v1/synthetic-peers/stop`

**Body (optional):** `{ "names"? }` — omit to stop all synthetic jobs started this way.

---

## 11. Recordings

### `GET /api/v1/recordings`

`{ "active", "recordings": [] }`

### `POST /api/v1/recordings`

Start recording. **409** if already active.

### `DELETE /api/v1/recordings/{recording_id}`

Stop active recording.

### `GET /api/v1/recordings/{recording_id}`

Recording summary / body.

### `GET /api/v1/recordings/{recording_id}/export`

Full JSON export + evidence quality.

---

## 12. Diagnostics events

### `GET /api/v1/events?limit=…`

Recent diagnostic events.

### `GET /api/v1/events/{event_id}`

One event.

### `GET /api/v1/episodes`

Diagnostic episodes list.

### `GET /api/v1/evidence/{evidence_id}`

Evidence blob / linkage for diagnostics.

---

## 13. Audit logs (Logging workspace)

### `GET /api/v1/logs`

| Query | Default |
|-------|---------|
| `limit` | 200 (1…5000) |
| `category` | optional filter |
| `severity` | optional |
| `code` | optional |
| `q` | free text |

**Response:** `{ count, stats, logs[] }`

### `GET /api/v1/logs/stats`

Aggregate stats.

### `GET /api/v1/logs/{log_id}`

One entry. **404** if missing.

### `DELETE /api/v1/logs`

Clear log buffer; returns `{ "cleared": n }`.

---

## 14. Verification tests

### `GET /api/v1/tests`

List recent verification runs.

### `POST /api/v1/tests`

Run one stimulus → expect step.

**Body**

```json
{
  "name": "verification_step",
  "owner": "test:verification",
  "stimulus": {
    "type": "inject",
    "bus": "high",
    "key": "host:host_drive_cmd",
    "values": { "speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 1 }
  },
  "expect": {
    "type": "message_observed",
    "bus": "high",
    "name": "HOST_DRIVE_CMD",
    "timeout_ms": 500
  }
}
```

`expect.type`: `message_observed` | `signal_equals` | `signal_in_range`

### `GET /api/v1/tests/{test_id}`

Fetch result. **404** if unknown.

---

## Common flows (UI mapping)

| UI action | API |
|-----------|-----|
| App load / health | `GET /status`, `WS /stream`, `GET /topology` |
| Computer / Real toggle | `DELETE` old session → `POST /sessions` with profile |
| Enable Bench TX | `POST /sessions/{id}/bench-tx` `{enabled:true}` |
| Drive / Control keyboard | `POST /control/intent` loop + `POST /control/release` |
| Control numeric inject | `POST /analysis/host-drive` (after release/clear owners) |
| Low motor/steer/brake | `POST /control/direct` |
| HMI MANUAL/ON | `POST /hmi/mode`, `POST /hmi/power` |
| CAN Dictionary | `GET /protocol/dictionary` |
| Logging table | `GET /logs` |
| Start/stop recording | `POST/DELETE /recordings…` |

---

## Error patterns

| Status | Typical code / detail |
|--------|------------------------|
| 400 | Invalid body, unknown synthetic name, bad can_id |
| 404 | Session/message/log/recording/test/lease not found |
| 409 | Bench TX disabled, ownership conflict, stale sequence, inject rejected, recording already active |
| 503 | Physical profile without adapter (no silent virtual fallback) |

---

## Related docs

| Doc | Content |
|-----|---------|
| [run.md](../run.md) | How to start API/UI |
| http://127.0.0.1:8001/docs | Live OpenAPI |
| `architecture-control-toolkit.md` | Behavioral authority |
