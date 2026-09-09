# Control Toolbox Plan

## Current architecture

- **Backend** — FastAPI, one worker, lifecycle-managed services. Virtual transport in Computer mode; CANalyst-II CH0=High, CH1=Low in Real profiles. TX always passes through the TxGate, ownership table, Bench TX gate, generated codec, and scheduler.
- **Observation** — transport RX queue → router → decode/validate/freshness → latest store → WebSocket stream. High and Low bus frames are visible continuously once their producer is present.
- **High-bus control** — keyboard/gamepad `/control/intent` in kinematics mode shapes Host `HOST_DRIVE_CMD` (0x300), plus Host steering/heartbeat streams. In the native simulator, this resolves to RT `RT_DRIVE_CMD` (0x204) and injectable motion/feedback frames.
- **Low-bus control** — bench/operator direct channels for motor (`RT_DRIVE_CMD` 0x204), steering (`VCU_SES_REQ` 0x169), and brake (`VCU_SEB_REQ` 0x7B9), mutually exclusive with High-bus kinematics.
- **HMI** — mode/power stream only. It is not a motion-control method.
- **Safety** — stale-intent watchdog, ownership leases, Bench TX arming, mode authority for High-bus kinematics, full-profile blocking of Host kinematics, and explicit stop-all cleanup.

## Bugs fixed

1. The native RT simulator bridge listened only to High `HOST_DRIVE_CMD`. Low direct commands now reach the simulator input path as well.
2. The bridge accidentally treated simulator-authored Low `RT_DRIVE_CMD` as a High input and later briefly mapped High 0x300 to High 0x204. Input routing is now explicit.
3. The direct-control API ignored unknown fields, so the legacy `{"actuator": ...}` request shape could pass as an empty request and produce an unintended motor stream. It now rejects unknown body fields with 422.
4. Direct feedback chips selected feedback without a bus, so a message that exists on one bus could hide the intended peer on another. They now explicitly show High `MTR_MOTOR_FBK` and Low SES/SEB status and expose their freshness.
5. The default backend test fixture autodetected a locally built simulator binary, making the test truthfulness test environment-dependent. Tests now use an explicit simulator configuration only where the assertion requires it.
6. Protocol catalog tests pinned stale message/instance counts and the old wire hash. They now assert catalog invariants and current platform hashes rather than dead historical constants.

## Backend gaps that remain

1. The native simulator currently consumes Low motor command and High kinematics. Direct steering (0x169) and brake (0x7B9) still need native SES/SEB feedback models for true closed-loop motion.
2. Low direct control is bench-only by intent; arming state in Real profiles should be more explicit about which physical High or Low frames the user is claiming.
3. The UI has separate Control and Drive workspaces but one backend control lease model. A single method/ownership summary should be surfaced in both.
4. Replay and observer mode exist through injection APIs but there is no first-class "observe-only, TX disabled" workflow badge/action.
5. High-bus simulated-command mode is implemented for Host kinematics, but there is no generic arbitrary High command authoring assistant that maps fields from the dictionary safely.
6. Native simulator should expose its current accepted command and signal route in `/simulation` status for easier diagnosis.

## Implementation plan

### Phase 1 — Solidify bus command modes

1. Model an explicit control plane enum: `observe`, `high_drive`, `low_motor`, `low_steer`, `low_brake`, `low_full`, `hmi`.
2. Persist the active mode method in `control/status` and expose High/Low lease ownership per frame.
3. Make method switching transactional: new mode is accepted only after the previous mode is fully stopped.

### Phase 2 — Close the native simulation loop

1. Decode Low 0x169 steering and 0x7B9 brake in the native SIM engine.
2. Add SES and SEB feedback producers, routing only through the shared protocol codecs and virtual transport injection.
3. Drive the visualization from `RT_MOTION_RPT`/wheel-speed feedback rather than only the last command.

### Phase 3 — Unified user interface

1. Merge method selection, Bench TX, and the current lease/actions into one persistent command header.
2. Add an Observe/Control toggle that makes observer mode explicit (no TX jobs, no ownership claim).
3. Add bus-scoped signal cards for High and Low commands, feedback, and freshness in the same panel.
4. Add a High command builder that supports dictionary-driven safe field construction with preview, then explicit arm + send.

### Phase 4 — Physical mode safeguards

1. Before arming in Real profiles, show the exact bus/frame ownership the action will claim.
2. Require confirmation when switching between High and Low control or between Computer and Real.
3. Rate-limit arbitrary/raw High or Low frame injection in Real profiles by default.

### Phase 5 — Contract testing

1. Add pairwise matrix tests covering observe, High kinematics, each Low direct channel, method switching, and Bench TX off.
2. Add simulator feedback tests that assert decoded motion follows both High and Low commands.
3. Keep protocol catalog assertions dynamic to avoid stale counts when new messages enter the contract.

## Verification

- `cmake --build native-test/build-sil --target sim_engine_native --config Release`
- `python -m pytest control-toolkit/backend/tests -q`
