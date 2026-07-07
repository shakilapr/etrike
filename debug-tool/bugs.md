# Debug Tool — Unsolved Known Bugs

> Severity: **P0** = data loss / safety confusion, **P1** = wrong behavior, **P2** = cosmetic / edge case.
> Note: Solved bugs have been cleaned from this list. Refer to `checklist.md` for the full historical checklist.

---

## P0 — Critical

No active P0 bugs currently tracked here.

## P1 — Wrong Behavior

### BUG-65: Controller causes Speed Oscillation (Split-Brain) in Full Sim Mode

**Severity:** P1 (Wrong Behavior)  
**Files:** `ui/src/components/Controller.svelte:103-104`

**Symptom:** When a user commands a continuous speed via the UI's Controller tab while in "Full Simulation" mode, the vehicle's speed oscillates repeatedly between the commanded target speed and zero.

**Root cause:** The `Controller.svelte` component unconditionally calls the `sendFrame` API to directly inject raw `0x300` CAN frames onto the bus. However, in Full Simulation mode, the backend's `host-model.ts` ECU is actively running and automatically generating its own `0x300` frames at 50Hz. Because the Controller UI fails to call the `/api/sim/controller` endpoint (`simControllerInput`), the `host-model` remains entirely unaware of the user's intent and continues to broadcast its default state (speed = 0). This creates a split-brain collision on the virtual bus where the UI and the `host-model` alternate sending conflicting drive commands.

**Fix direction:** Update `Controller.svelte` to subscribe to the `$workMode` store. When in `full-sim` mode (or when the `host` ECU is being simulated), route drive inputs through `simControllerInput` rather than `sendFrame`.

### BUG-66: Missing UI/E2E Testing Infrastructure (Blind Spots)

**Severity:** P1 (Infrastructure / QA)  
**Files:** `ui/package.json`, `.github/workflows/` (or equivalent CI config)

**Symptom:** Critical integration bugs (like BUG-65) and UI layout bugs (like BUG-64) easily slip into the codebase without failing any checks, and existing backend tests (like those for BUG-63) can be bypassed if not strictly enforced.

**Root cause:** The frontend `debug-tool/ui` has no testing framework configured (no Vitest for unit tests, no Playwright for E2E tests). There is no automated way to simulate user interactions or verify UI-to-Backend API contracts. Furthermore, backend test failures are not aggressively gating commits (CI enforcement gap).

**Fix direction:** 1. Install and configure Playwright to run end-to-end tests covering critical UI workflows (like the Controller). 2. Configure Vitest in `ui/package.json` for component-level tests. 3. Enforce a strict CI pipeline that blocks merges if `npm run test` fails.

## P2 — Cosmetic / Edge Cases

- **BUG-05:** Serial port fails silently (UI shows error, just no backend console log).
- **BUG-16:** CANalyst-II bridge abandoned after detection failure can't be reused.
- **BUG-18:** Emulator `simMode` toggle resets when switching tabs.
- **BUG-26:** `SerialBridge.start()` called twice on reconnect (Double open error).
- **BUG-29:** `stopRecording()` does not prevent double-stop.
- **BUG-30:** `normalizeBus()` falls through to `"high"` silently.
- **BUG-34:** `normalizeFilter` strips bus from bare IDs.
- **BUG-36:** Controller `tick()` reads `heldNow` via Svelte reactive assignment (stale closure edge case).
- **BUG-54:** `Topbar.svelte` reports the USB port state as "open" when disconnected instead of "closed" or "offline".
- **BUG-55:** `Dashboard.svelte` hardcodes the obsolete `"EPS-C"` string for steering instead of the updated `"SES"`.

### BUG-64: Topbar Brand Title Shrinks and Wraps on Mode Selection

**Severity:** P2 (Cosmetic / UI Layout)  
**Files:** `ui/src/components/Topbar.svelte:174-175`, `ui/src/styles.css:426-429`

**Symptom:** When a user clicks the Work Mode dropdown in the Topbar and selects an option with a longer character count (such as "Full Simulation" or "Monitor Only"), the main "E-Trike" brand title shrinks, and the text is forced to either wrap to a new line or get horizontally squished.

**Root cause:** The `.tb-brand` container in `styles.css` is defined as a Flexbox (`display: flex`) and prevents the entire container from shrinking via `flex-shrink: 0`. However, the children inside this container (the `<span>` containing the "E-Trike" text and the `<select>` element) use the browser's default `flex-shrink: 1`. When the dropdown is populated with a longer string, its intrinsic width increases. Because there is limited horizontal space in the `.tb-row-main` flex container, the Flexbox layout algorithm compensates by forcibly shrinking the adjacent `<span>`.

**Fix direction:** Add `white-space: nowrap;` and `flex-shrink: 0;` to the CSS rules for the `<span>` inside `.tb-brand`. This forces the title to retain its intrinsic text width and pushes any overflow constraints to be handled gracefully by the overall layout (or truncating the dropdown instead).

---

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
