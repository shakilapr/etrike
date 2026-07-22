# Phase 6 — Diagnostics, recording, and evidence

**Status:** Complete (software track)  
**Depends on:** Phase 5

## Delivered

| Component | Path / API |
|-----------|------------|
| Recording service | `services/recording.py` — ring buffer, evidence quality, export, windows |
| Diagnostics service | `services/diagnostics.py` — events, episodes, recovery hysteresis |
| Verification service | `services/verification.py` — one-at-a-time sequential steps |
| Bit layout | `services/bit_layout.py` — Intel/Motorola occupancy grid |
| Recording API | `POST/GET/DELETE /api/v1/recordings`, `GET …/export` |
| Events API | `GET /api/v1/events`, `GET /api/v1/events/{id}`, `GET /api/v1/episodes` |
| Tests API | `POST/GET /api/v1/tests`, `GET /api/v1/tests/{id}` |
| Evidence API | `GET /api/v1/evidence/{id}` |
| Protocol layout | `GET /api/v1/protocol/messages/{bus}/{id}`, `…/layout` + live overlay |
| Dictionary UI | bit-grid + signal table + live values |
| Diagnostics UI | timeline, episodes, recording, evidence window |
| Audit log service | `services/audit_log.py` — operational trail for Logging tab |
| Logging API | `GET/DELETE /api/v1/logs` |
| Logging UI | filterable table + detail + export |
| Tests | `test_recording`, `test_diagnostics`, `test_verification`, `test_evidence_quality`, `test_bit_layout`, `test_audit_log` |

## Exit gate

- [x] Diagnostic episodes aggregate (not per-frame flood); recovery hysteresis
- [x] Recording captures frames or marks Incomplete
- [x] Formal Pass requires Complete evidence when recording active
- [x] CAN Dictionary bit layouts from protocol YAML
- [x] Sequential verification Pass/Fail/Inconclusive
- [x] Event API returns structured data

## Deferred (backlog polish)

- Disk worker under overload stress
- ECU diagnostic flag classification from full metadata matrix
- Triggered capture / offline replay (Later phases)
