# VTC Backend

FastAPI backend for the E-Trike Vehicle Test Console. Phase 1 skeleton
(see [`../workplan.md`](../workplan.md) Phase 1).

The backend never re-implements CAN codecs. It consumes the YAML-generated
`protocol/` runtime — the same contract the RT/SYS firmware compiles against.
The YAML contracts in `protocol/contracts/` are the source of truth.

## Setup

Requires Python ≥ 3.11. From this directory:

```bash
python -m venv .venv
. .venv/Scripts/activate       # Windows (Git Bash); use .venv/bin/activate on POSIX
pip install -e ".[dev]"
```

## Run

```bash
uvicorn vtc.main:app --host 127.0.0.1 --port 8000
# → GET http://127.0.0.1:8000/api/v1/status
# → interactive docs at /docs
```

## Test

```bash
pytest
```

## Layout

| Package | Responsibility | Phase |
|---|---|---|
| `vtc/protocol_bridge.py` | Only seam to the generated `protocol` package | 1 |
| `vtc/config.py` | Typed config (single-worker, profiles, service levels) | 1 |
| `vtc/main.py` | App factory + lifespan | 1 |
| `vtc/models/` | Immutable frames, state, adapter, session | 1 |
| `vtc/transport/` | `interface` (done), `virtual` (§1.3), `canalyst` (Phase 2) | 1–2 |
| `vtc/pipeline/` | `decoder` (done), `router`/`validator`/`freshness` (§1.4–1.5) | 1 |
| `vtc/state/` | `latest` (done), `topology`/`history` (§1.5) | 1 |
| `vtc/api/` | `status`/`state`/`protocol`/`stream` | 1 |
| `vtc/services/` | `lifecycle` + `event_bus` (done) | 1 |

Working endpoints today: `GET /api/v1/status`, `GET /api/v1/state`,
`GET /api/v1/protocol/messages[/{bus}/{id}]`, and `WS /api/v1/stream` — a live
subscription (`hello` → initial `state` → coalesced `state` batches → `heartbeat`
when idle → critical `event` fan-out; `batch_seq` for gap detection, `resync` to
force a fresh snapshot).
