# Debug Tool Fixes Handoff

Date: 2026-07-06

This document summarizes the debug-tool bug-fix pass, what was completed, how it was verified, and what is still worth checking before release.

## Current Status

The debug UI and backend both typecheck and pass their automated tests in the current workspace.

Latest verified commands:

```powershell
python shared/can/generate_can_index.py --check
cd debug-tool/ui
npm run check
npm test
cd ../backend
npm run check
npm test
```

Observed results:

- `python shared/can/generate_can_index.py --check`: passed, generated CAN index is current.
- `debug-tool/ui npm run check`: passed, `svelte-check found 0 errors and 0 warnings`.
- `debug-tool/ui npm test`: passed, 122 tests.
- `debug-tool/backend npm run check`: passed.
- `debug-tool/backend npm test`: passed, 180 tests.

## Completed Work

### CAN YAML Source of Truth

The canonical CAN dictionary sources are now:

- `shared/can/can_high.yaml`
- `shared/can/can_low.yaml`

`debug-tool/ui/src/lib/can-index.ts` is generated output and should not be edited by hand.

Implemented in `shared/can/generate_can_index.py`:

- Generates valid TypeScript signal objects with `name:<string>`.
- Emits `protocol: "yaml"` for YAML-backed messages.
- Emits `byte_order`, defaulting to `"motorola"` when the YAML does not specify one.
- Emits signal `receivers`, defaulting to `[]`.
- Allows nullable `min` and `max`.
- Uses string-keyed `values: Record<string, string>`.
- Fails on duplicate `bus:id` definitions instead of silently choosing one.
- Supports `--check` to fail when `can-index.ts` is stale.

Regenerated file:

- `debug-tool/ui/src/lib/can-index.ts`

### UI Typecheck Fixes

Fixed the remaining Svelte/TypeScript blockers:

- `debug-tool/ui/src/components/Emulator.svelte`
  - Split the duplicate `simMode` variable into `softwareSimEnabled` and `simVehicleMode`.
  - Removed unused `simInject` and unused `ecuKeys`.
  - Replaced unsafe ECU presence record casts with `keyof EcuPresence` narrowing.

- `debug-tool/ui/src/components/EcuTopology.svelte`
  - Replaced unsafe ECU presence record cast with typed key narrowing.

- `debug-tool/ui/src/components/Stats.svelte`
  - Moved invalid `{@const}` helper functions into script-level helpers.

- `debug-tool/ui/src/components/Topbar.svelte`
  - Resolved `modeLabel` naming conflict by aliasing the imported work-mode helper and renaming the local vehicle-mode label helper.

- `debug-tool/ui/src/components/MessageCard.svelte`
  - Normalized freshness timestamp calculation so millisecond timestamps and second timestamps are treated consistently.

- `debug-tool/ui/src/lib/ws.ts`
  - Captured the active socket in the reconnect callback path to satisfy nullability and avoid using a possibly-null shared `socket`.

### Already Present and Verified in This Tree

These plan items were already implemented before the final typecheck cleanup and are covered by the current passing checks/tests:

- `latestById` is a writable store updated incrementally in `debug-tool/ui/src/stores/can.ts`.
- Initial frame loads rebuild `latestById`; individual WebSocket frames update it without rescanning the full ring buffer.
- ECU presence uses a 3-second freshness threshold in `debug-tool/ui/src/stores/telemetry.ts`.
- REST polling skips while WebSocket is connected in `debug-tool/ui/src/App.svelte`.
- Backend frame pruning is detached from the hot insert path and runs through `DebugStore.runMaintenance()`.
- `recording_frames(frame_id)` has an index in `debug-tool/backend/src/db/schema.ts`.
- Stopped recording retention is capped at 10 in `debug-tool/backend/src/db/queries.ts`.
- Pipeline correlation in `debug-tool/backend/src/api/can.ts` uses binary-search-assisted timestamp lookup instead of per-trigger full-array `find` scans.
- CAN monitor filter/expand state lives in `debug-tool/ui/src/stores/monitor.ts`.
- Fault watcher state is scoped inside `initFaultWatcher()` in `debug-tool/ui/src/stores/faults.ts`.
- `sendZeroFrames()` respects `selectedBus` in `debug-tool/ui/src/components/Controller.svelte`.

## Files Changed in the Final Cleanup Pass

- `shared/can/generate_can_index.py`
- `debug-tool/ui/src/lib/can-index.ts`
- `debug-tool/ui/src/components/Emulator.svelte`
- `debug-tool/ui/src/components/EcuTopology.svelte`
- `debug-tool/ui/src/components/Stats.svelte`
- `debug-tool/ui/src/components/Topbar.svelte`
- `debug-tool/ui/src/components/MessageCard.svelte`
- `debug-tool/ui/src/lib/ws.ts`

## Known Remaining Work / Follow-Up Checks

No automated failures are currently known. The following are still recommended before calling the broader bug list fully closed:

- Run an end-to-end manual debug-tool session with real or simulated CAN traffic:
  - Start backend and UI.
  - Confirm WebSocket frames stream.
  - Confirm REST polling resumes after WebSocket disconnect.
  - Confirm stale bus stats clear the UI when the backend marks buses inactive.

- Add an explicit test that simulator/injection DLC values match the YAML-backed CAN index. The generator is now the UI source of truth, but there is not yet a dedicated cross-check that every simulator/injection profile stays aligned with the YAML DLC values.

- Add a CI step for:

```powershell
python shared/can/generate_can_index.py --check
```

This prevents future edits to `can_high.yaml` or `can_low.yaml` from leaving `can-index.ts` stale.

- Consider adding a generator unit test for duplicate `bus:id` handling. The generator now fails on duplicates, but the behavior is only exercised manually unless wired into CI/tests.

- Manually verify BUG-23 behavior with live bridge traffic: bus detection should broadcast only when high confidence transitions occur, not on every frame. Static checks/tests pass, but live behavior is the best confirmation for broadcast flood fixes.

- Manually verify transport hot-swap and ESTOP confirmation flows in the browser. Typecheck/tests pass, but these are interaction-heavy paths.

## How to Regenerate CAN Index

After editing either YAML file:

```powershell
python shared/can/generate_can_index.py
python shared/can/generate_can_index.py --check
```

Then run:

```powershell
cd debug-tool/ui
npm run check
npm test
```

Do not hand-edit `debug-tool/ui/src/lib/can-index.ts`.

## How to Run the App Locally

Backend:

```powershell
cd debug-tool/backend
npm run dev
```

UI:

```powershell
cd debug-tool/ui
npm run dev
```

The UI dev server uses Vite on `127.0.0.1`; check the terminal output for the exact port.

## Suggested Next-Agent Starting Point

1. Run `git status --short` and inspect the current diff.
2. Run the verification commands listed in `Current Status`.
3. If continuing implementation, prioritize the `Known Remaining Work / Follow-Up Checks` section.
4. If editing CAN definitions, modify only `shared/can/can_high.yaml` or `shared/can/can_low.yaml`, regenerate `can-index.ts`, and run the generator check.
