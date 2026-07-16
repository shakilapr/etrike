# Control Toolkit Backend

FastAPI backend for the E-Trike Control Toolkit.

The backend never re-implements CAN codecs. It consumes the YAML-generated
`protocol/` runtime — the same contract RT/SYS firmware is generated from.
Contracts live in `protocol/contracts/`.

Default profile is **Pure Software** (virtual High + Low buses, no hardware).

## Setup

Requires Python ≥ 3.11. From this directory:

```bash
python -m venv .venv
.venv\Scripts\activate
pip install -e ".[dev]"
```

## Run

```bash
uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8000
# → GET  /api/v1/status | /state | /protocol/messages
# → POST /api/v1/sessions | …/bench-tx | …/stop-all
# → POST /api/v1/injections/preview | /injections
# → POST /api/v1/synthetic-peers/start
# → POST /api/v1/hmi/mode | /hmi/power
# → WS   /api/v1/stream
# → docs http://127.0.0.1:8000/docs
```

Environment overrides: `CTK_HOST`, `CTK_PORT`, `CTK_PROFILE`
(`pure_software` | `bench_test` | `full_vehicle`).

Physical profiles return **503** until CANalyst is implemented (no silent virtual fallback).

## Test

```bash
pytest -v
```

## Layout

| Package | Responsibility |
|---|---|
| `protocol_bridge.py` | Seam to generated `protocol` package |
| `config.py` / `main.py` | Config + app factory |
| `models/` | Frames, state, adapter, session |
| `transport/` | Virtual dual-bus; CANalyst stub |
| `pipeline/` | Decode, validate, freshness, router |
| `state/` | Latest store |
| `services/` | Lifecycle, sessions, TX gate, scheduler, peers, ownership |
| `api/` | REST + WebSocket |
