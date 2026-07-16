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
# → GET  http://127.0.0.1:8000/api/v1/status
# → GET  http://127.0.0.1:8000/api/v1/state
# → GET  http://127.0.0.1:8000/api/v1/protocol/messages
# → WS   ws://127.0.0.1:8000/api/v1/stream
# → docs http://127.0.0.1:8000/docs
```

Environment overrides: `CTK_HOST`, `CTK_PORT`, `CTK_PROFILE`
(`pure_software` | `bench_test` | `full_vehicle`).

## Test

```bash
pytest -v
```

## Layout

| Package | Responsibility |
|---|---|
| `control_toolkit/protocol_bridge.py` | Only seam to generated `protocol` package |
| `control_toolkit/config.py` | Typed config (single-worker, profiles, service levels) |
| `control_toolkit/main.py` | App factory + lifespan |
| `control_toolkit/models/` | Immutable frames, state, adapter, session |
| `control_toolkit/transport/` | `interface`, `virtual`; `canalyst` stub for later |
| `control_toolkit/pipeline/` | decoder, router, validator, freshness |
| `control_toolkit/state/` | latest, topology, history |
| `control_toolkit/api/` | status, state, protocol, stream |
| `control_toolkit/services/` | lifecycle, event_bus |
