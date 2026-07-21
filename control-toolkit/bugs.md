# Control Toolkit Frontend Issues Report

Based on the requirement that this control-toolbox is specifically for **testing and fault injection** (and explicitly not safety-critical), the previous assumptions about safety restrictions are inverted. The toolkit currently imposes artificial safety constraints and UX limitations that actively prevent engineers from testing the system's failure modes and edge cases.

## 1. Artificial Input Clamping & Missing Fault Injection 
The UI actively prevents sending out-of-bounds or invalid data to the firmware, making it impossible to test ECU boundary conditions:
- **Hardcoded Input Limits:** In the Numeric Inject panel, inputs are clamped using HTML attributes (e.g., `min="-500" max="3000"` for speed, `min="-3000" max="3000"` for yaw). 
- **Restricted Enums:** Gear selection uses a `<select>` dropdown hardcoded to `0, 1, 2, 3`. There is no way to inject an invalid gear (e.g., `4` or `255`) to test firmware rejection logic.
- **Drive Console Clamping:** `DriveConsole.tsx` enforces strict `Math.min(1, maxSpeedMmps / 3000)` clamping and normalizes throttle/steer to `[-1, 1]`. There is no "raw value" override to simulate a broken pedal sensor that outputs values >1.0 or extreme noise.

## 2. Unexposed Generic Injection API
The backend possesses a powerful generic `/injections` endpoint (`api/injections.py`) capable of injecting arbitrary CAN frames, scheduling periodic bursts, and mocking missing ECUs. However:
- **No UI Exists:** The frontend `api.ts` does not wrap `/injections`, and the UI has no generic "Message Injector" panel.
- **Hardcoded Testing:** Testing is currently limited strictly to `HOST_DRIVE_CMD` and a few low-bus direct actuators. Engineers cannot use the UI to simulate a failing BMS, a rogue safety node, or arbitrary bus collisions.

## 3. "Auto-Healing" Obscures Test Failures
When testing how a system handles dropped sessions or missing hosts, the UI's aggressive state management gets in the way:
- **Forced Session Creation:** Functions like `runVerification`, `startRec`, and keyboard arming silently call `ensureSessionReady()`. If an engineer deliberately crashes the backend session to observe ECU timeout behavior, clicking a button in the UI will silently recreate a `pure_software` session in the background, masking the failure and resetting the system state without the engineer's explicit consent.

## 4. Missing "Stop" Mechanisms for Continuous Streams
A major testing UX issue exists where actions can be started but lack a localized stop mechanism, forcing users to dump all state to recover:
- **Periodic Inject:** The "Host kinematics" panel has a "Start periodic inject" button, but no "Stop periodic inject" button. To stop it, users are forced to click the global "Stop all motion TX".
- **HMI Requests (Mode & Power):** The HMI panel allows users to begin transmitting `MANUAL`/`AUTO` and `ON`/`OFF` requests. However, there is no "Disable" button within the panel to cease sending these frames (the backend `api.hmiMode` takes an `enabled` flag, but the UI never sets it to false).
- **Verification Runner:** Clicking "Run zero-speed verification" starts a backend test and sets the panel to `busy=true`. If a test is deliberately forced to hang, there is no "Cancel" button, leaving the section permanently disabled.

## 5. UI State De-Syncs During Network Failures
When network requests fail (a common occurrence when stress-testing), the UI fails to reflect the reality of the failure:
- **E-Stop State Trap:** In `DriveConsole.tsx`, `fireEstop()` attempts to disarm the UI only *after* a successful network request. If the network request fails (e.g., due to testing load), the UI remains armed and in gear, failing to reflect the local E-Stop intent.
- **Silent Control Release Failures:** Throughout the frontend, `api.controlRelease` and `api.stopAnalysis` are heavily chained with `.catch(() => undefined)`. While this prevents JS unhandled rejections, it hides critical state de-syncs from the tester. If a release fails, the backend still thinks the UI owns the stream, leading to confusing behavior that the tester cannot diagnose because the error was swallowed.

## 6. Unclickable Chrono View Rows
- **Broken History Inspection:** In the `LiveCan` table, switching the view to "Chrono" removes the `onClick` handler from the rows. Clicking a historical frame does not update the "Message detail" side drawer, breaking the ability to inspect the exact signal values of a frame leading up to a failure.

## 7. Data Pipeline & Control Loop Flaws
The data pipelines that feed the UI and push commands have critical blind spots:
- **Blind Intent Loop (Silent 409 Drops):** In `DriveConsole.tsx`, the 50ms intent loop explicitly ignores `409 Conflict` errors (`if (!/stale_sequence|409/i.test(msg)) setLog(msg)`). If the backend session resets, the lease expires, or bench TX is disabled, the backend will correctly return `409` and drop the intent frames. However, the UI swallows this error, remains `armed`, and continues pumping dead intents into the void without notifying the tester that control has been lost.
- **Undecoded Chrono History Pipeline:** The `/history` API returns raw `data_hex` without decoded signals, and the frontend lacks the capability to decode historical frames on the fly. This renders the "Chrono" view almost entirely useless for diagnosing complex failures, as engineers cannot visually inspect what signal values precipitated a fault.
- **Blocking Verification API:** The `api.runTest()` call for the Verification Runner is a fully blocking synchronous `await` that can wait up to 30,000ms. There is no asynchronous SSE or WebSocket pipeline to stream test progress or allow early cancellation, which entirely locks up that portion of the UI if the backend hangs during a test.

## 8. Number Input UX & State Bugs
Every `<input type="number">` field across the application suffers from a critical React controlled-component bug:
- **Negative Number Block:** Because the state is updated via `onChange={(e) => setSpeed(Number(e.target.value))}`, typing a minus sign (`-`) produces an empty value `""` in HTML5. `Number("")` evaluates to `0`, which instantly overrides the input with `"0"`. It is practically impossible to type negative numbers directly.
- **Backspace Auto-fill:** If an engineer highlights the existing number and presses Backspace or Delete, the value becomes `""`, which again evaluates to `0`, instantly refilling the input with `"0"`.

## 9. Tab-Specific State & Logic Bugs
Several individual tabs exhibit localized bugs that mislead the user:
- **Network Tab (Fake Topology Placeholder):** When the backend topology array is empty (due to a disconnected stream or quiet bus), the UI arbitrarily falls back to rendering a hardcoded list containing `Host` and `RT_high` marked as `offline`. This lies to the user by masking the true absence of topology data, presenting it as a known offline network instead.
- **Control Tab (HMI Request Spoofing):** In the HMI panel, there is a button labeled `PURE_SIM (UI only)`. However, clicking it triggers `api.hmiMode(mode === 'AUTO' ? 1 : 0)`. Because `PURE_SIM` is not `'AUTO'`, the ternary evaluates to `0`, causing the UI to silently transmit a `MANUAL` hardware mode request to the backend. It actively sends unintended hardware intents under the guise of being "UI only".

---

# Evidence-based reassessment and repair plan (2026-07-20)

## Correct product boundaries

The original report incorrectly treats “test tool” as equivalent to “all controls bypass validation.” Four surfaces must remain distinct:

1. **Normal engineering controls** encode named protocol messages and retain contract limits, required bits, counters, and checksums.
2. **Raw/fault injection** is an explicitly labelled expert surface that may submit malformed payloads or out-of-range raw values.
3. **Computer mode** owns virtual CAN and optional software-in-the-loop processes.
4. **Real mode** owns CANalyst-II and physical buses; it never silently falls back to virtual CAN or starts software ECUs.

The repository currently has three software facilities which the UI must not conflate:

- `NativeSilBridge`: managed RT physics + generated codecs; the only simulator connected to Toolkit virtual CAN.
- `simulation/`: an offline TypeScript multi-ECU scenario/test framework, not a managed live process.
- `native-test`: host-compiled firmware tests. Its interactive engine is RT-only; full RT tasks and SYS integration are explicitly incomplete.

Indicators must therefore report RT SIL as running/stopped/error and SYS SIL as unavailable/not integrated. Virtual bus activity is not proof that RT, SYS, MTR, or a complete vehicle is running.

## Reclassified findings

| ID | Reassessment | Root cause and required outcome |
|---|---|---|
| B01 Input clamping | **Partly incorrect** | Contract clamping belongs in normal controls. Add a separate expert raw/fault injector; do not weaken Drive or typed encoding. |
| B02 Generic injection UI | **Confirmed** | Add preview/submit/cancel clients and named-signal plus explicit raw-frame UI modes. |
| B03 Auto-healing | **Confirmed, high** | Mutations silently create `pure_software`. Only explicit Start actions may create sessions; other actions report missing prerequisites. |
| B04 Missing local stops | **Confirmed** | Periodic HostDrive needs job cancellation, HMI needs Disable actions, and verification needs async cancellation. |
| B05 Network de-sync | **Confirmed** | Release failures are swallowed and ESTOP disarms only after HTTP success. Disarm locally first, report cleanup failure, then reconcile backend state. |
| B06 Chrono inspection | **Confirmed** | Rows lack selection and frames lack per-frame decode. Add a frame-specific detail model backed by generated codecs. |
| B07a Silent 409 loss | **Confirmed, critical UX** | The client suppresses all `409` text. Only stale sequence races are ignorable; ownership/session/TX conflicts must disarm and display control loss. |
| B07b History decode | **Confirmed** | Keep raw evidence but add backend decode projection or endpoint using generated codecs. |
| B07c Verification blocking | **Confirmed** | Convert create to async create/poll/cancel while preserving one-test ownership. |
| B08 Number editing | **Confirmed** | Store draft text, parse on submit/blur, and preserve intermediate `''` and `'-'`. |
| B09 Real toggle | **New, confirmed** | Settings disables Real; topbar allows it but hides failure in a tooltip. Use one transition action and persistent result/error. Failed physical open preserves Computer visibly. |
| B10 Simulation lifecycle | **New, confirmed** | RT SIL auto-starts during virtual transport open, has no status API, and cannot be independently started/stopped. Add Computer-only lifecycle API and controls. |
| B11 Framework health | **New, confirmed** | Add explicit backend, virtual transport, RT SIL, SYS SIL, router, protocol, and browser-stream indicators. SYS reports unavailable until integrated. |
| B12 Mode-inappropriate actions | **New, confirmed** | Add a centralized profile capability matrix. Hide software controls in Real and bench-bypass/teleop controls in `full_vehicle` by default. |
| B13 Header ESTOP | **New, confirmed** | It requires session + Bench TX and reports failure only to console. Computer may explicitly establish virtual prerequisites; Real requires operator-enabled physical TX and displays rejection. |
| B14 Real tests | **New, confirmed** | Existing E2E intentionally skips Real activation. Add fake-CANalyst backend and Playwright success/failure/rollback tests. |
| B15 ESTOP state semantics | **New, design gap** | `session.estop_active` is a UI injection latch, not evidence that a physical ESTOP/reset circuit is active. Label and reset/acknowledge it accurately. |
| B16 Fake topology fallback | **Confirmed** | Empty topology must render an honest empty/unknown state; never invent Host/RT nodes. |
| B17 PURE_SIM sends MANUAL | **Already fixed in current source** | Current `setMode` calls `hmiMode` only for MANUAL/AUTO and keeps PURE_SIM UI-only. Retain this as a regression test rather than an open implementation bug. |

## Mode/action matrix

| Surface | Computer (`pure_software`) | Real Bench (`bench_test`) | Real Vehicle (`full_vehicle`) |
|---|---|---|---|
| Observe, Live CAN, logs, recording | Show | Show | Show |
| Virtual CAN status | Show | Hide | Hide |
| RT SIL Start/Stop | Show | Hide | Hide |
| Software framework indicators | Show truthful states | Replace with physical ECU heartbeats | Replace with physical ECU heartbeats |
| Synthetic missing peers | Show for software analysis | Show, capability scoped | Hide by default |
| Keyboard/Drive teleop | Show | Explicit isolated-bench capability only | Hide by default |
| Low direct actuator bypass | Show | Explicit isolated-bench capability only | Hide by default |
| Named message injection | Show | Show | Show behind physical TX gate |
| Raw/fault injection | Expert opt-in | Expert opt-in | Expert opt-in + physical confirmation |
| Bench TX | Label as virtual TX gate | Physical TX gate | Physical TX gate + strongest destination warning |
| Software ESTOP injection | May explicitly establish virtual prerequisites | Requires physical TX enabled | Requires physical TX enabled |

## Implementation sequence

### P0 — truthful modes, runtime lifecycle, visible failures

1. Add typed `GET /simulation`, `POST /simulation/start`, and `POST /simulation/stop` returning mode/profile, virtual transport, RT SIL configured/executable/running/error/PID, SYS availability, router, and protocol hash.
2. Make RT SIL lifecycle independent of opening virtual CAN. Start/stop are idempotent, reject Real mode, emit diagnostics, and stop on profile change/shutdown.
3. Include simulation status in `/settings` and frontend types.
4. Replace separate mode handlers with one shared transition function. Real remains inspectable when unavailable; activation visibly reports the adapter reason and leaves Computer active.
5. Add Computer-only Start/Stop Simulation controls and read-only framework indicators. Never render them in Real mode.
6. Add reusable visible action results. Header ESTOP, mode switch, release, and stop errors cannot remain console-only.
7. Add a centralized profile capability matrix and apply it to Control, Drive, Bench, and Settings.
8. Remove fake topology fallback and separate PURE_SIM local state from MANUAL/AUTO HMI transmission.

### P1 — action correctness and reconciliation

1. Remove implicit session creation from Drive, Control, Bench, Diagnostics, and recording.
2. Disarm Drive locally before awaiting ESTOP. Classify stale sequence separately; authoritative conflicts disarm and refresh status.
3. Replace swallowed cleanup errors with visible best-effort cleanup results and backend status refresh.
4. Expose periodic job IDs and localized Stop actions.
5. Add HMI Disable Mode and Disable Power.

### P2 — complete test-tool surfaces

1. Build generated-dictionary Message Injector with preview, one-shot, periodic, and cancel.
2. Add a separately gated Raw/Fault Injector; retain named-message validation elsewhere.
3. Convert numeric fields to draft-text parsing with inline errors.
4. Add chronological frame selection and generated-codec decode.
5. Convert verification to asynchronous create/status/cancel.

### P3 — coverage

1. Backend: lifecycle idempotency, Real rejection, process crash status, physical-switch rollback, profile capabilities, injection, and verification cancellation.
2. Playwright: all mode-sensitive controls in three profiles; fake-adapter Real success; missing-adapter failure; simulation start/stop; RT/SYS indicators; ESTOP gate paths; localized stops; network reconciliation; numeric drafts; chrono decode.
3. Add a button-wiring test requiring every visible button to cause UI state, an API request, navigation, or a documented result.
4. Do not mark SYS SIL running until a managed SYS implementation emits independently verifiable SYS frames and health on the same virtual buses.

## Acceptance criteria

- Failed Real activation explains why and leaves Computer selected without ambiguity.
- Fake/real CANalyst activation atomically changes profile, destination, adapter, channel map, and visible controls.
- Computer has working Start/Stop Simulation controls; Real has none.
- Indicators distinguish backend, virtual CAN, RT SIL, SYS SIL, protocol, and browser stream.
- Every mutation reports success/failure; critical cleanup/control errors are not swallowed.
- Full Vehicle hides software simulation, isolated-bench direct actuator, synthetic-peer, and teleoperation controls by default.
- Normal controls retain protocol limits; malformed testing is only through Raw/Fault Injector.
- All behavior has backend and Playwright positive and negative coverage.

## Implementation status after repair pass (2026-07-20)

### Completed in this pass

- B03: routine Control, Drive, Bench, recording, and verification actions no longer silently create sessions or enable physical TX. Explicit Computer ESTOP is the documented exception and establishes virtual prerequisites visibly.
- B04: periodic HostDrive, HMI mode, and HMI power now have local stop/disable actions. Verification cancellation remains open because the backend test API is still synchronous.
- B05/B07a: Drive disarms locally on ESTOP, reports cleanup failures, and treats only stale-sequence races as ignorable. A duplicate keyboard-release race that could cancel newly started Low streams was also removed.
- B06/B07b: Chrono rows select the exact historical frame and decode it through the generated protocol codec.
- B08: editable numeric controls preserve blank and minus-sign drafts and validate on commit.
- B09/B10/B11/B13: Computer/Real use one transition path; unavailable Real activation is visible and rolls back; Computer has RT SIL Start/Stop and truthful backend, virtual CAN, router, RT, SYS, and protocol indicators; header ESTOP reports gate failures.
- B12/B16/B17: Full Vehicle hides software/direct/teleoperation surfaces, empty topology is honest, and PURE_SIM remains UI-only.

### Still open before production-ready classification

- B01/B02: build a generated-dictionary Message Injector and a separately gated raw/malformed-frame injector. The backend currently supports validated named-message injection, not arbitrary raw payload transmission.
- B04/B07c: convert verification to asynchronous create/status/cancel with progress reporting.
- B05: finish replacing non-critical swallowed best-effort cleanup calls with a unified visible action/result log.
- B12/B14: enforce profile capabilities in the backend as well as the UI, and add fake-CANalyst Playwright coverage for successful Real activation and rollback.
- B15: separate the software ESTOP-injection latch from observed physical ESTOP/reset state.
- Integrate a managed SYS SIL process. Current indicators intentionally report SYS as unavailable; the connected native simulator is RT-only.
- Run the physical characterization suite with `CTK_PHYSICAL=1` after connecting CANalyst-II and the intended bench hardware.

Validation for this repair pass: backend `191 passed, 1 skipped` (physical USB characterization only); frontend production build passed; Playwright `27 passed` in two isolated groups. The aggregate one-command Playwright wrapper intermittently hangs during Windows web-server teardown, while both deterministic groups complete cleanly.
