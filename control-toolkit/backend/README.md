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

Physical profiles use the dual-channel **CANalyst-II** adapter through
`python-can` (`CH0=High`, `CH1=Low`, 500 kbit/s). They return **503** when the
adapter/driver cannot be opened; there is no silent virtual fallback. See
[`../docs/canalyst-ii-setup.md`](../docs/canalyst-ii-setup.md) before connecting
CAN wiring.

## Test

```bash
pytest -v

# Passive hardware preflight (never transmits)
python scripts/canalyst_preflight.py
```

## Layout

| Package | Responsibility |
|---|---|
| `protocol_bridge.py` | Seam to generated `protocol` package |
| `config.py` / `main.py` | Config + app factory |
| `models/` | Frames, state, adapter, session |
| `transport/` | Virtual dual-bus + fail-safe CANalyst-II physical adapter |
| `pipeline/` | Decode, validate, freshness, router |
| `state/` | Latest store |
| `services/` | Lifecycle, sessions, TX gate, scheduler, peers, ownership |
| `api/` | REST + WebSocket |
