# Phase 3 — Operating profiles and session management

**Status:** Complete (software track)  
**Depends on:** Phase 1 virtual pipeline  
**Hardware track:** Full Vehicle / Bench Test remain declared but non-executable without a physical adapter

## Delivered

| Component | Path |
|-----------|------|
| Session / profile / Bench TX models | `backend/control_toolkit/models/session.py` |
| Session FSM + revision + Stop All | `backend/control_toolkit/services/session_manager.py` |
| Stimulus leases (one producer per bus+ID) | `backend/control_toolkit/services/ownership.py` |
| Session REST API | `backend/control_toolkit/api/sessions.py` |
| Profile list (available / blocked) | `GET /api/v1/sessions/profiles` |
| Vehicle view (req vs conf mode/power) | `POST /api/v1/sessions/{id}/vehicle-view` |
| Architecture shell UI | `frontend/src/App.tsx`, `App.css` |
| Store / stream / API client | `frontend/src/store.ts`, `useStream.ts`, `api.ts` |

## Session lifecycle

```
create → Preparing → Listening → Running → (stop-all / close)
close outcome: stopped | completed | failed | inconclusive
```

- **Pure Software:** opens virtual High/Low; TX only when Bench TX enabled.
- **Full Vehicle / Bench Test:** API returns `profile.physical_unavailable` — **never** silent virtual fallback.
- Controlled profile change requires `confirm=true`, disables Bench TX, cancels jobs, clears leases.

## Bench TX

- Default **disabled**; explicit enable on a running Pure Software session with open transport.
- Auto-disabled on profile change, Stop All, close, and neutralize paths.
- Physical enable path returns 503 until the hardware track.

## Leases

- `claim` / `renew` / `release` on `(bus, can_id)` with TTL.
- Conflict → 409 `ownership.conflict`.
- Stop All clears all leases and jobs.

## UI (architecture §6–11 subset)

Persistent header: profile, destination, adapter, High/Low activity, requested vs confirmed mode/power, ESTOP, recording, Bench TX, session phase, stream quality, wire hash, Inject ESTOP.

Left rail workspaces: Overview, Network, Live CAN, Control, Bench, CAN Dictionary, Diagnostics, Settings.

| Workspace | Phase 3 content |
|-----------|-----------------|
| Overview | Safety strip, status cards, command/feedback table |
| Network | Dual-bus health + topology nodes |
| Live CAN | Latest-by-message table, bus/search filter, detail drawer |
| Control | HMI request buttons, analysis host-drive inject, Bench TX, Stop All, lease release on unmount |
| Bench | Setup checklist + live session TX/lease snapshot |
| Dictionary | YAML catalog browser |
| Diagnostics | Session evidence snapshot (full pipeline later) |
| Settings | Profile list; Pure Software start; physical activate refuses cleanly |

## Exit gate verification

```bash
cd control-toolkit/backend
pytest tests/test_profiles.py tests/test_session_state.py tests/test_bench_tx.py \
  tests/test_source_ownership.py tests/test_api_sessions.py tests/test_sessions.py -v

cd control-toolkit/frontend
npm run test:e2e
```

## Explicitly not Phase 3

- Physical CANalyst transport and physical Bench TX
- Full HMI CAN TX at 1 Hz (Phase 5+)
- Full synthetic-peer engine UI (later)
- Recording pipeline / diagnostic timeline (Phase 6)
- Keyboard/gamepad teleoperation (Phase 7)
