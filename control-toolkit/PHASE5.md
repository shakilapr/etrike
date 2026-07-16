# Phase 5 — Command pipeline and injection

**Status:** Complete (software track / virtual)  
**Depends on:** Phase 4 frontend foundation

## Delivered

| Component | Path / API |
|-----------|------------|
| Encoder | `services/encoder.py` + protocol codecs |
| TX gate | `services/tx_gate.py` — profile, Bench TX, ownership |
| Periodic scheduler | `services/scheduler.py` — re-encode, counter, skip catch-up |
| Injection API | `POST /injections/preview`, `POST /injections`, `DELETE /injections/{id}` |
| Analysis host-drive | `POST /analysis/host-drive`, `POST /analysis/stop` |
| HMI 1 Hz | `POST /hmi/mode`, `POST /hmi/power` → `0x111` / `0x112` + rolling counter |
| ESTOP DLC=0 | `safety:safety_estop` on high bus `0x001` |
| Control UI | mode/power HMI TX, inject, Stop All, ESTOP |
| Tests | `test_encoder`, `test_tx_gate`, `test_scheduler`, `test_hmi`, `test_injections`, `test_analysis`, `test_api_surface` |

## Exit gate verification

```bash
cd control-toolkit/backend
pytest tests/test_encoder.py tests/test_tx_gate.py tests/test_scheduler.py \
  tests/test_hmi.py tests/test_injections.py tests/test_analysis.py \
  tests/test_api_surface.py -v
```

## Explicitly deferred

- Full Accepted→Queued disposition chain polish
- Generic inject UI for every catalog message
- Physical Bench TX (hardware track)
