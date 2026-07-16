# Phase 6 — Diagnostics, recording, and evidence

**Status:** Partial (software track core APIs + Diagnostics UI)  
**Depends on:** Phase 5

## Delivered

| Component | Path / API |
|-----------|------------|
| Recording service | `services/recording.py` — opt-in ring buffer, evidence quality |
| Diagnostics service | `services/diagnostics.py` — events + episode aggregation |
| Router hook | frames observed into active recording |
| Recording API | `POST/GET/DELETE /api/v1/recordings` |
| Events API | `GET /api/v1/events`, `GET /api/v1/events/{id}`, `GET /api/v1/episodes` |
| Diagnostics UI | timeline, episodes, start/stop recording |
| Tests | `test_recording.py`, `test_diagnostics.py` |

## Still open

- Sequential test runner (`POST /tests`)
- Disk export / evidence windows
- Full dictionary bit-grid (Intel/Motorola)
- Recording worker isolation under overload stress polish
