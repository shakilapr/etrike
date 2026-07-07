# Debug Tool — Unsolved Known Bugs

> Severity: **P0** = data loss / safety confusion, **P1** = wrong behavior, **P2** = cosmetic / edge case.
> Note: Solved bugs have been cleaned from this list. Refer to `checklist.md` for the full historical checklist.

---

## P0 — Critical

### BUG-58: Simulation Engine Deaf to Physical Frames (Hybrid Mode Broken)

**Severity:** P0 (Simulation Critical)  
**Files:** `backend/src/serial/reader.ts:187`, `backend/src/sim/engine.ts`

**Symptom:** In Hybrid mode, software ECUs do not react to physical hardware inputs (e.g. physical ESTOP or Drive commands).

**Root cause:** `SerialBridge` and `CanalystBridge` insert frames into the DB and broadcast them to the UI, but they never pass them into `simEngine.injectExternal(frame)`. As a result, the simulated virtual CAN bus never sees physical frames.

**Fix direction:** In `index.ts`, bind a listener to `bridge.onFrame(...)` (or similar event) that pipes incoming physical frames into `simEngine.injectExternal(frame)`.

---

### BUG-59: EPS-C / SES Steering Angle Snap-to-Death

**Severity:** P0 (Safety Critical)  
**Files:** `backend/src/sim/ecus/epsc-model.ts:25`, `backend/src/types/can.ts:278`

**Symptom:** The moment simulation starts, the UI (or hardware if connected) receives a steering angle of 3000 degrees, causing the steering graphic (and virtual vehicle) to violently snap to maximum negative angle.

**Root cause:** The `epsc-model.ts` internally initializes its angle and target to `30000`, assuming a 30000 offset for 0 degrees. However, the system's DBC decoder (`types/can.ts`) interprets `str_angle` as a signed 16-bit integer (INT16). So when `epsc-model` transmits `30000`, it is decoded literally as 30000 * 0.1 = 3000 degrees.

**Fix direction:** Change `epsc-model.ts` to initialize `this.angle = 0` and `this.targetAngle = 0`, matching the signed INT16 implementation of the actual SES ECU.

---

## P1 — Wrong Behavior

### BUG-60: "EPS-C" vs "SES" Naming Schism across Architecture

**Severity:** P1 (Architecture Consistency)  
**Files:** `backend/src/sim/work-mode.ts:26`, `backend/src/sim/ecus/epsc-model.ts`, `backend/src/index.ts`

**Symptom:** Inconsistent behavior when configuring bypasses or parsing logs due to mixed naming of the steering ECU.

**Root cause:** The steer-by-wire system was renamed from "EPS-C" to "SES". While some UI elements were updated, the backend heavily relies on `epscSync` (in WorkModeConfig), `EpscModel`, and the `epsc-model.ts` file name. This breaks schema parsing and introduces confusion when cross-referencing logs with the new "SES" identifiers.

**Fix direction:** Rename `epscSync` to `sesSync` in `work-mode.ts` schema and defaults. Refactor `EpscModel` to `SesModel` and rename `epsc-model.ts` to `ses-model.ts`.

---

### BUG-61: Health Bar "SYS" ECU Permanently Lost (Wrong Bus)

**Severity:** P1 (UI/Health Critical)  
**Files:** `ui/src/stores/telemetry.ts:86`

**Symptom:** In the Topbar Health Bar, the `SYS` ECU indicator permanently displays as "lost" (red) even when the system is healthy.

**Root cause:** The `ecuPresence` derived store logic checks for `0x011` (SYS_SAFETY_STS) and `0x7FE` (SYS_HEARTBEAT) on the **low** bus (`$latest["low:0x7FE"]`). However, the CAN database defines these frames solely on the **high** bus.

**Fix direction:** Change the checks in `telemetry.ts` to look at the high bus: `recent($latest["high:0x7FE"], $now) || recent($latest["high:0x011"], $now)`.

---

### BUG-62: Dashboard Telemetry Missing Scaling & Units (Raw Value Leak)

**Severity:** P1 (UI Critical)  
**Files:** `ui/src/components/Dashboard.svelte:41-42`

**Symptom:** The main Dashboard screen displays unscaled raw integer values for steering (e.g. `450` instead of `45.0`) and braking (e.g. `25000` instead of `25.0`), and completely omits unit labels.

**Root cause:** `Dashboard.svelte` bypasses the centralized `telemetry.ts` store logic (which correctly handles scaling, clamping, and units). Instead, it pulls raw values like `str_angle` (which is in `0.1 deg` units) and `brake_pressure_kpa` straight from the raw CAN decoder payload.

**Fix direction:** Refactor `Dashboard.svelte` to subscribe to the centralized `$telemetry` store (just like `Topbar.svelte`) and use `t.steerAngleDeg` and `t.brakePressureMpa` to ensure all UI components share consistent scaling, clamping, and units. Add `deg` and `MPa` labels to the HTML.

---

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
