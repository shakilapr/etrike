# Phase 4 — Read-only frontend foundation

**Status:** Complete (software track)  
**Depends on:** Phase 3 sessions  
**UI design:** [`ui-design-control-toolkit.md`](ui-design-control-toolkit.md)

## Delivered

| Component | Path |
|-----------|------|
| Vite + React + TypeScript + Zustand | `frontend/` |
| Dark engineering theme (borders, density, 4 px grid) | `frontend/src/App.css` |
| Application shell (header + fixed sidebar) | `App.tsx` |
| WebSocket stream + reconnect attempts | `useStream.ts` |
| Overview · Network · Live CAN | workspaces in `App.tsx` |
| Chronological stream (history API) | Live CAN “Stream” mode |
| Vehicle preview (2D kinematics) | `VehiclePreview.tsx` (from `tricycle_kinematics_simulator.html`) |
| Control / Bench / Dictionary / Diagnostics / Settings | shells + wired APIs |
| Playwright smoke | `frontend/e2e/smoke.spec.ts` |

## UI design compliance

| Guideline | Implementation |
|-----------|----------------|
| Work-first shell | Stable top bar + fixed 240 px sidebar + main workspace |
| Density | 40 px controls, 32–36 dense, 20 px panel pad, 24–32 section gaps |
| Typography | Inter + JetBrains Mono; sentence case; tabular nums |
| Neutral palette | Dark instrument panel; red only for ESTOP/danger |
| Borders over shadows | 1 px borders; no glass/gradient cards |
| Tables first-class | Sticky headers, filters, selected row, stream mode |
| Explicit forms | Labels above, units, allowed ranges on Control inject |
| Status language | Live / Late / Missing / … with ● + label |
| Stream quality | LIVE / DELAYED / LOST + retry count |

## Vehicle preview

Port of `tricycle_kinematics_simulator.html`:

- Ego-centered canvas, ICR, dynamic steer clamp/slew
- Protocol-named telemetry: HOST_DriveSpeed, HOST_YawRate, gear, brake
- **Keyboard** local kinematics or **Follow CAN** from `HOST_DRIVE_CMD`
- Sidebar entry: **Vehicle preview**

## Exit gate verification

```bash
cd control-toolkit/frontend
npx tsc -b
npm run test:e2e
```

## Explicitly not Phase 4

- shadcn/Tailwind stack (design uses hand-tuned CSS matching the same rules)
- OpenAPI-generated client (hand API client is sufficient for current surface)
- Full bit-grid dictionary (Phase 6 depth)
- TanStack Table virtualization for multi-kHz chrono (bounded history poll for now)
