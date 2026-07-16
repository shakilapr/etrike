# Phase 1 — Backend skeleton + virtual transport

**Status:** Complete  
**Depends on:** Phase 0 protocol foundation

## Delivered

| Component | Path |
|-----------|------|
| App factory | `backend/control_toolkit/main.py` |
| Virtual dual-bus transport | `transport/virtual.py` |
| RX router + history + topology | `pipeline/router.py`, `state/history.py`, `state/topology.py` |
| Latest + freshness | `state/latest.py`, `pipeline/freshness.py` |
| Protocol bridge | `protocol_bridge.py` (Phase 0 artifacts) |
| REST/WS | `/status`, `/state`, `/history`, `/topology`, `/protocol/*`, `/stream` |

## Exit gate verification

```bash
cd control-toolkit/backend
pytest -v
# 78+ tests including virtual_pipeline, websocket, transport_callback
```

## Explicitly not Phase 1

- Physical CANalyst (`transport/canalyst.py` stub only)
- Sessions/inject/analysis (implemented later but out of Phase 1 scope list)
- Frozen/Recovering freshness (counter stall) — deferred
