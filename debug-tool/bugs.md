# Debug Tool — Unsolved Known Bugs

> Severity: **P0** = data loss / safety confusion, **P1** = wrong behavior, **P2** = cosmetic / edge case.
> Note: Solved bugs have been cleaned from this list. Refer to `checklist.md` for the full historical checklist.

---

## P0 — Critical

No active P0 bugs currently tracked here.

## P1 — Wrong Behavior

No active P1 bugs currently tracked here.

## P2 — Cosmetic / Edge Cases

- **BUG-18:** Emulator `simMode` toggle resets when switching tabs.
- **BUG-29:** `stopRecording()` does not prevent double-stop.
- **BUG-30:** `normalizeBus()` falls through to `"high"` silently.
- **BUG-34:** `normalizeFilter` strips bus from bare IDs.
- **BUG-36:** Controller `tick()` reads `heldNow` via Svelte reactive assignment (stale closure edge case).
- **BUG-54:** `Topbar.svelte` reports the USB port state as "open" when disconnected instead of "closed" or "offline".
- **BUG-55:** `Dashboard.svelte` hardcodes the obsolete `"EPS-C"` string for steering instead of the updated `"SES"`.

## Extras / Artifacts

- **GAP-02:** Improved Trike Kinematic Model has been saved to `c:\projects\etrike\tem\improved_trike_kinematic.md` so that it faces upward by default. It can be integrated into the main `TrikeViz.svelte` UI in a future iteration.

## Feature Gaps / Missing Telemetry

### GAP-01: Controller UI lacks Actual Feedback and Odometer

**Severity:** Feature Request / Usability Gap  
**Files:** `ui/src/components/Controller.svelte`, `backend/src/sim/engine.ts`, `ui/src/stores/telemetry.ts`

**Symptom:** 
1. The Controller UI displays only the *commanded* (target) speed, steering angle, and brake pressure. It does not display the *actual* feedback coming back from the SES (steering) and SEB (brake) ECUs. This makes it impossible for an operator to verify if the physical or simulated ECUs are actually following the commands.
2. The UI does not display an odometer or "distance moved".

**Root cause:** 
The `Controller.svelte` component hardcodes the display to the reactive variables it is transmitting (`yaw`, `speed`, `brake`), rather than subscribing to the `$telemetry` store to render `steerAngleDeg` and `brakePressureMpa`. Furthermore, neither the backend `SimulationEngine` nor the frontend `telemetry.ts` integrates the vehicle's speed over time to calculate a position or distance scalar (odometer).

**Fix direction:** 
1. Update `Controller.svelte` to subscribe to the `$telemetry` store and display actual feedback (e.g., `t.steerAngleDeg` and `t.brakePressureMpa`) alongside the commanded targets.
2. Add a `distance_m` or `odometer_m` field to the `SimEngineState` in `engine.ts` by integrating `speedMmps` * `dt` on every tick.
3. Expose this odometer reading to the UI (either via a new CAN ID like `0x207` or through the `/api/sim/state` REST endpoint) and display it on the Dashboard and Controller tabs.
