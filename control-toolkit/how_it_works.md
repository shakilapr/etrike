# Control Toolkit: How It Works

Each section below is a **user-facing feature**. For every feature, this document traces the exact logic from the moment the user clicks something, through every conditional branch the code takes, including what happens when things fail.

---

## Feature 1 — Switching to Real Mode (CANalyst-II)

The user is currently in Computer mode. They look at the Topbar and click **"Real"**.

The UI immediately locks both toggle buttons by setting `modeBusy = true`. This prevents any double-click. It clears any previous error banner visible below the topbar. It then maps the label "real" to the profile string `bench_test` and fires a POST to the backend.

**On the backend**, before doing anything with hardware, the `SessionManager` acquires its threading lock. It checks the `expected_revision` the UI sent matches the current session revision — if they don't match it means another client already modified the session, and it returns a `409 Conflict` immediately without touching hardware. Assuming the revision matches, it next confirms `confirm=true` was sent in the request body — the API refuses profile changes unless the caller explicitly confirmed, so automated scripts can't accidentally switch modes. It then deep-copies the current "Computer" state to memory as a rollback target.

Now it neutralizes the bus: `bench_tx` is forced to `DISABLED`, all running `Scheduler` jobs are deleted, and the `OwnershipTable` is cleared, revoking any active drive or injection leases. The FSM phase moves to `PREPARING`. The lock is released, and the `Lifecycle` service is called.

**In Lifecycle**, the request routes to `open_physical_transport()`. A new `CanalystTransportAdapter` is created as a *candidate* — it is not yet assigned as the active transport. Then `candidate.open()` is called.

**Inside the adapter**, it checks its own health state first — if already open, it exits early. Otherwise it sets health to `OPENING`, drains the internal frame queue, and calls `_try_make_bus()`. This function temporarily mutes the `can.bus` Python logger (because `python-can` logs a confusing "not properly shut down" warning when a constructor fails), then calls `can.Bus(interface="canalystii", channel=(0,1), bitrate=500000, device=0)`.

**If the USB adapter is not plugged in**, the OS driver throws an exception — typically a `ValueError` with a message like "device not found" or a `CanInitializationError`. `_try_make_bus` catches this, restores the logger level, and returns `(None, error_string)`. The adapter sees `bus is None`, sets its internal health to `ABSENT`, stores the error string in `_last_error`, and raises a `RuntimeError("CANalyst-II open failed: [error detail]")`.

This exception bubbles back to `open_physical_transport()`. Because the candidate failed before `_tear_down_transport()` was ever called, **the virtual transport is completely untouched**. The exception continues to `_on_profile_change()`, where Lifecycle catches it, emits a `transport.profile_switch_failed` diagnostic event, and re-raises it.

Back in `SessionManager`, the re-raised exception is caught. The lock is re-acquired. The saved rollback state is restored: `self._state = previous`. Even though the profile rolled back, `bench_tx` is explicitly forced back to `DISABLED` — the code intentionally does not restore whatever TX state existed before, because it was neutralized for safety and must stay off. The revision counter increments. A `SessionError` with HTTP 503 is raised.

FastAPI catches this and returns a `503 Service Unavailable` JSON response containing the error detail string. On the frontend, the `fetch` call throws. The `catch` block strips the `"Error: "` prefix, trims the message to 120 characters, and calls `setModeErr()`, which renders a red error banner below the topbar. The UI then fires a silent `api.status()` to reconcile its local `useAppStore` state with what the backend says is actually true. Finally, `setModeBusy(false)` unlocks the buttons. The toggle is back to "Computer". The user can read the error, plug in the USB adapter, and try again.

**If the USB adapter is plugged in** and `_try_make_bus()` succeeds: the adapter assigns the `can.Bus` handle to `_bus`, increments `_epoch` by 1 (so old frames from any previous session are ignored), sets health to `OPEN`, records `_worker_heartbeat_ns`, then spawns two daemon threads: `canalyst-rx` (the frame reader) and `canalyst-health` (the watchdog monitor). Back in Lifecycle, `_tear_down_transport()` is now called — the virtual transport is cleanly closed, and if the Native SIL process was running it is sent a `SIGTERM` first, then waited on for 2 seconds, then `SIGKILL`ed if still alive. The new adapter becomes `self.transport`. A new `Router` task is created and attached to the physical transport. The `SessionManager` advances the session to `LISTENING` then immediately to `RUNNING`, incrementing the revision at each step. The new state is returned to the UI, the toggle highlights "Real", and live CAN traffic begins appearing.

---

## Feature 2 — Switching Back to Computer Mode

The user is in Real mode and clicks **"Computer"**.

The frontend does the same lock/clear/send as before, but maps "computer" to `pure_software`. The `SessionManager` neutralizes the bus identically — TX off, jobs cancelled, leases cleared. Lifecycle routes to `open_virtual_transport()`.

`open_virtual_transport()` checks if the current transport is already a `VirtualTransportAdapter` — if so, it exits immediately (idempotent). Otherwise it creates a new `VirtualTransportAdapter`, calls `candidate.open()`, which for virtual buses simply initializes in-memory queues (no USB, cannot fail). Then `_tear_down_transport()` closes the physical `CanalystTransportAdapter`. The physical adapter's `close()` method sets a `_stop` threading event, wakes the `_rx_loop` thread which then breaks its receive loop and exits, and joins both worker threads with a timeout. The USB handle is released. The virtual adapter becomes the active transport, the Router restarts against it.

If `CTK_NATIVE_SIL_EXE` is configured, Lifecycle then calls `start_native_sil()`. This is covered in detail below. Once everything is ready, `SessionManager` moves to `RUNNING`, and the UI toggles back to "Computer".

---

## Feature 3 — Arming Bench TX

Before any frame can be transmitted, the user must explicitly click **"Arm Bench TX"** in the Session panel.

The UI sends a POST to `/sessions/{id}/bench-tx` with `enabled: true`.

The `SessionManager` checks the session is in `RUNNING` phase — it refuses to arm if in `PREPARING`, `STOPPING`, or a terminal phase. Then, if the active profile is `bench_test` or `full_vehicle`, it calls `physical_available()` which re-runs `discover_canalyst()`. This probe attempts to open CANalyst USB channel 0 briefly — it has a 5-second cache so it doesn't slam the driver repeatedly. If the adapter is missing, it raises a 503 immediately with "physical Bench TX unavailable: [reason]". Even if the adapter is physically present, it then calls `_get_transport_open()` — if the adapter is in a `DEGRADED` or `RECOVERING` state (e.g. just reconnected but not yet stable), TX is still refused. Only when the transport reports `OPEN`, `ACTIVE`, or `QUIET` health does the arm succeed.

If all checks pass, `bench_tx` is set to `ENABLED`, revision increments, and the UI chip changes to "Armed". From this point on, any transmit request through `TxGate` will proceed.

If the profile is `pure_software`, there is no physical adapter check — the arm succeeds immediately as long as the session is `RUNNING`.

---

## Feature 4 — Driving the Vehicle (W Key / Keyboard)

The user opens the Drive tab and holds the **W key** to apply forward throttle.

The UI continuously fires POST requests to `/control/intent` with `throttle: 1.0, steer: 0.0, gear: null, mode: "kinematics"`, each with a monotonically increasing `sequence` number.

The `ControlIntentService` first calls `require_bench_tx()` — if Bench TX is disabled it raises a `409` immediately and the UI shows an error. Assuming it's armed, the service acquires its own lock.

It checks for stale sequence: if this is the same source stream as the active intent and the incoming `sequence` is *lower* than the current one, it rejects with `stale_sequence`. This prevents old in-flight HTTP requests from overwriting newer ones (e.g. if the network delivers them out of order).

It checks for mutual exclusion: if the user previously started a "direct actuator" control stream (steering or brake directly), the incoming kinematics intent cancels all direct jobs first. The two control modes cannot coexist.

The incoming `throttle=1.0` is clamped to `[-1.0, 1.0]`. A deadband check removes micro-values below `0.05` to eliminate keyboard noise. Then `_shape_locked()` runs:
- Since `throttle > 0` and `gear` is `None` (no explicit gear), the gear is automatically promoted from `N` to `D`.
- `shaped_speed = int(round(1.0 * 3000)) = 3000 mm/s` (the forward limit from `shared_config.h`).
- `shaped_yaw = int(round(0.0 * 3000)) = 0 mrad/s`.
- If the user had pressed `S` (Sport mode) and speed is positive, it gets an extra 20% boost, clamped back to the max.

Then `_ensure_job_locked()` runs. If no job exists yet, the `Scheduler` is asked to create a `PeriodicJob` for `HOST_DRIVE_CMD` on the High bus, with period 10ms. The resulting `job_id` is stored. If a job already exists (user is still holding W while a new intent arrives), `scheduler.update_values()` is called to hot-update the values in-place — no job recreation, no ownership lease churn.

The Scheduler's async loop wakes every 2ms. Each time it checks all jobs. When `HOST_DRIVE_CMD`'s `next_deadline <= now`, it submits via `TxGate`. If the system fell behind (CPU spike), the while loop advances `next_deadline` without firing burst catches — it silently skips missed ticks to avoid flooding the bus. Then the values are submitted: `TxGate` verifies Bench TX is still armed, calls the encoder, claims the `(high, 0x300)` ownership lease with a TTL of 30ms (3× period), and calls `transport.send(env)`. The CANalyst adapter packages this as a `can.Message` and hands it to the USB driver out-queue. `next_deadline += 0.010`.

**The watchdog**: A separate async loop in Lifecycle calls `control.tick_watchdog()` every 50ms. If `time.monotonic() - last_mono > 0.5s` (the firmware's host stale timeout), the watchdog kills the scheduler job, sets `loss_reason = "stale_intent"`, and transmits one final `HOST_DRIVE_CMD` with `speed=0, yaw=0, gear=N` — so the RT firmware sees a clean zero command before the host goes silent, rather than just seeing the command disappear.

When the user releases W, the UI stops sending intents. After 500ms the watchdog fires, zeroes the vehicle, kills the job, and releases the lease.

---

## Feature 5 — Direct Actuator Control (Steering / Brake)

The user opens the Control tab and enables the **Steering** direct channel with a target angle.

A POST goes to `/control/direct` with `channel: "steering", enabled: true, values: {target_angle_raw: 150}`.

`ControlIntentService.set_direct()` first checks the channel name is one of `motor`, `steering`, or `brake`. It calls `require_bench_tx()`. Then it checks mutual exclusion: if a kinematics job (`job_id` in state) is currently running, it is cancelled immediately and `active` is set to false. Direct actuators and kinematics cannot run simultaneously.

For steering, `_normalize_direct_values()` runs. It clamps `target_angle_raw=150` to `[-450, 450]` (representing ±45°). `alignment_enable` and `control_enable` are hardcoded to `True` — the toolkit bypasses the ECU's normal alignment checks for direct bench control. `target_speed_raw` defaults to 328 if not provided, clamped to `[125, 525]`. The values dict is built.

If a steering job already exists, `scheduler.update_values()` hot-updates it. Otherwise a new `PeriodicJob` is created for `VCU_SES_REQ` on the Low bus at 20ms period, with `counter_field="rolling_counter"`. The Scheduler's loop will automatically increment the rolling counter 0–15 on every tick. This counter is required by the SES actuator's wire protocol — if it stops incrementing, the actuator interprets it as a stale command and stops responding.

To stop the steering channel, the user toggles it off. The service cancels the job. If no other direct jobs remain, `mode` returns to `"none"` and `active` to false. If the user also had a brake job running, it continues independently.

---

## Feature 6 — Injecting an E-STOP

The user clicks the red **"Inject ESTOP"** button in the Topbar.

The UI first checks: if in Computer mode, does a session exist? If not, it creates one automatically with `pure_software`. If Bench TX is not armed, it arms it automatically. Then it POSTs to `/control/intent` with `estop: true, throttle: 0, steer: 0`.

`apply_intent()` runs. After the sequence and mode checks, it sees `estop=true`. It calls `_zero_locked()` (sets shaped speed and yaw to 0), `_cancel_job_locked()` (kills any kinematics job), `_cancel_direct_locked()` (kills any steering/brake/motor jobs). All ownership leases for all control owners are dropped via `release_owner()`. It returns immediately — no new job is scheduled, no periodic frame runs.

The actual ESTOP frame (`0x001 SAFETY_ESTOP`, DLC=0) is transmitted separately: the API handler calls `injectEstop()` which builds a `RawFrameEnvelope` with an empty data payload (`b""`) and sends it on both High and Low buses. The transmission on both buses is immediate and does not go through the Scheduler — it is a one-shot direct call to `transport.send()` for each channel.

The `estop_active` flag is set in the session state. The UI's E-STOP chip turns red. Every subsequent `TxGate.submit()` call will fail because Bench TX was zeroed. The operator must explicitly clear the E-STOP by re-arming Bench TX.

---

## Feature 7 — Starting a Recording

The user clicks **"Start Recording"** in the Logging tab.

A POST goes to `/recordings`. The `RecordingService.start()` is called. If a recording is already active (state is `RECORDING`), it raises `RuntimeError("recording already active")` and the API returns 409. Otherwise a new `RecordingSession` is created with a unique `recording_id`, a ring buffer capped at 50,000 frames, and timestamps anchored to both `time.monotonic_ns()` (for relative timing) and `time.time_ns()` (for wall-clock export alignment). The recording state is set to `RECORDING`.

From this point, every frame that passes through the Router's `on_frame` callback is passed to `RecordingService.observe_frame()`. Inside, the service acquires its lock, checks there's an active recording in `RECORDING` state, and checks if the ring buffer is at capacity. If the buffer is full, `dropped` is incremented and `evidence_quality` is downgraded to `INCOMPLETE` immediately. If there's space, the frame is appended as a `RecordedFrame` with the bus, CAN ID, DLC, hex data, direction (`rx` or `tx`), source (physical/injection/synthetic), and the `backend_arrival_ns` monotonic timestamp.

The UI shows "Rec: On" in the Topbar meta row.

---

## Feature 8 — Stopping a Recording and Checking Quality

The user clicks **"Stop Recording"**.

A DELETE goes to `/recordings/{id}`. `RecordingService.stop()` is called. It checks the `recording_id` matches. The state changes to `STOPPED`, `stopped_mono` is recorded, the session is moved to the history list, and `_active` is cleared.

The `evidence_quality` field tells the user how reliable the recording is:
- `COMPLETE` means no frames were dropped.
- `DEGRADED` means the transport had a failure during recording (the `mark_degraded()` callback fires when the CANalyst adapter signals a failure, adding a note to the recording).
- `INCOMPLETE` means the ring buffer hit its 50,000 frame cap and frames were silently dropped.
- `NOT_COMPARABLE` is reserved for future cross-session comparison failures.

If any frames were dropped at stop-time, a note `"frame drops while recording"` is added to the notes list.

---

## Feature 9 — Exporting a Recording as BLF

The user clicks **"Export BLF"** for a stopped recording.

The API calls `RecordingService.export_blf(recording_id)`. The service finds the session (searching active then history list). It copies the frames out of the ring buffer under the lock, then releases the lock — the BLF encoding happens outside the lock so it doesn't block incoming frame writes.

A `_ExportBuffer` (a subclass of `io.BytesIO` with `close()` overridden to be a no-op) is created as the write target. A `can.BLFWriter` from `python-can` is initialized against it. For each `RecordedFrame`, the nanosecond timestamp is reconstructed: `started_wall_ns + (frame.backend_arrival_ns - started_mono_ns)`. This anchors the monotonic clock to the wall clock at the exact moment recording started, so the BLF file has correct absolute timestamps. The channel is mapped: High bus → CH0, Low bus → CH1. `writer.on_message_received()` encodes each frame into the BLF binary format. When done, `writer.stop()` is called (which would normally close the buffer, but the override prevents that), and the raw bytes are extracted with `output.getvalue()`.

A sidecar JSON is returned alongside the BLF. It documents the clock anchor, channel map, bitrate, and known limitations (USB grouping jitter, TX delivery not guaranteed). The UI bundles both into a ZIP for download.

---

## Feature 10 — Viewing the Network Topology (Live ECU Map)

The user opens the **Network** tab. They see boxes for Host, RT (High), RT (Low), SYS, and MTR, each with a liveness indicator.

The `TopologyTracker` maintains state for exactly 5 nodes, each keyed by `(bus, can_id)`:
- `Host` = High bus, `0x7FC`
- `RT_high` = High bus, `0x7FD`
- `RT_low` = Low bus, `0x7FD`
- `SYS` = Low bus, `0x7FE`
- `MTR` = Low bus, `0x206`

Every time the `Router` processes a frame and upserts a `MessageState` to the `LatestStore`, it also calls `TopologyTracker.observe(state)`. The tracker checks if the message bus and CAN ID match any heartbeat node. If yes, it updates that node's `last_seen_ns`, `freshness`, and `validation_status`, and maps it to a `NodeLiveness` enum value.

A separate async loop in Lifecycle calls `topology.reclassify()` every 100ms. This loop independently re-evaluates liveness for all 5 nodes based on current age, even if no new frames have arrived. The freshness thresholds are per-message: the `FreshnessAger` uses `max(150ms, 2 × cycle_ms)` for `LATE` and `max(500ms, 5 × cycle_ms)` for `MISSING`. So for a 500ms heartbeat like `RT_HEARTBEAT`, it becomes `LATE` after 1 second of silence and `MISSING` after 2.5 seconds.

The liveness mapping is: `UNSEEN → OFFLINE`, `LIVE → LIVE`, `LATE → LATE`, `MISSING → MISSING`, `INVALID → FAULT`. The UI polls `/topology` and renders each node's color accordingly — grey for offline, green for live, yellow for late, red for missing or fault.

---

## Feature 11 — Computer Mode with Native SIL (Physics Simulation)

The user is in Computer mode. If `CTK_NATIVE_SIL_EXE` is set to a compiled RT physics binary, Lifecycle automatically starts it when switching to Computer mode.

`start_native_sil()` in Lifecycle first checks the profile is `PURE_SOFTWARE` — if called in Real mode it raises 409. It checks the transport is virtual — if not it raises 409. It checks a SIL is not already running. It reads the executable path from config, resolves it to an absolute path, and verifies the file exists. If the file doesn't exist, `FileNotFoundError` is raised with the full resolved path, and the error propagates back as a `SessionError 503`.

If the executable exists, `NativeSilBridge.start()` launches it as a subprocess with `stdin` and `stdout` as pipes, `stderr` discarded. It registers an `_on_tx` listener on the `VirtualTransportAdapter` — this listener fires every time any frame is transmitted over the virtual bus.

Two daemon threads start:

The **write thread** (`native-sil-tx`) blocks on an internal command queue. The `_on_tx` listener fires whenever `HOST_DRIVE_CMD` (`0x300`) is transmitted on the High bus. Any other frame is ignored. When the listener receives a matching frame, it puts the `RawFrameEnvelope` onto the command queue (max 256 items — if full it logs an error and drops). The write thread pops it, checks the subprocess is still alive, then writes two JSON lines to the process stdin: one `{"type":"frame","bus":"high","id":"0x300","data":[...]}` containing the drive command bytes, and one `{"type":"tick","dt_ms":10}` triggering a physics step. The pipe is flushed. If the pipe is broken (`BrokenPipeError`), the thread records the error and exits.

The **read thread** (`native-sil-rx`) reads lines from the subprocess stdout in a blocking `for line in process.stdout` loop. Each line is JSON-decoded. Lines that aren't `{"type": "frame"}` are ignored. Valid frame lines must have `bus` in `("high", "low")`, `id` as a hex string, and `data` as a list of integers. If the frame is well-formed, `transport.inject()` is called on the virtual adapter — this injects a synthetic RX frame directly into the virtual bus, as if the RT ECU had physically sent it. If stdout closes without the stop event being set, the thread records `"native SIL process exited"` as an error.

When switching away from Computer mode, `_tear_down_transport()` calls `native_sil.stop()`. The stop event fires, the listener is unregistered, a `None` sentinel is pushed to the command queue to unblock the write thread, `stdin` is closed, and the subprocess is waited on for 2 seconds. If still alive, `SIGTERM`, then another 2-second wait, then `SIGKILL`.

---

## Feature 12 — The Audit Log

The **Audit Log** is a ring buffer of up to 10,000 entries covering everything that has happened: session changes, transport events, control actions, safety events, recordings, and more.

Every service in the backend, when something notable happens, calls `diagnostics.emit()`. The `DiagnosticsService` categorizes the event (checking if `code` starts with `session.`, `transport.`, `control.`, `safety.`, etc.) and routes it to `AuditLogService.log()`. This creates an `AuditEntry` with a unique `log_id`, monotonic and wall-clock timestamps, severity (`info`/`warning`/`error`), a structured `code` string (e.g. `transport.canalyst_failed`), a human title, and a detail string. The entry is prepended to a `deque(maxlen=10000)` under a lock. The deque automatically evicts the oldest entries when full.

When the user opens the Logging tab, the UI calls `/logs?limit=200`. The backend iterates the deque (newest first) and applies filters: exact match on `category`, `severity`, `code`, and substring search across `code + title + detail + category` for the `q` parameter. Up to `limit` entries are returned. The UI shows them as a scrollable, filterable table.

The user can also call `/logs` with `DELETE` to wipe the entire log. `AuditLogService.clear()` acquires the lock, clears the deque, and returns the count of cleared entries.

---

## Feature 13 — The Scheduler: What Happens When a Periodic Job Falls Behind

This is not a direct user click, but it matters. If the system is under CPU load and the Scheduler's async loop misses its 2ms wakeup, multiple deadlines may be past when it finally runs.

The loop finds jobs with `next_deadline <= now`. For each one, before firing, it runs a recovery loop:
```
while job.next_deadline + job.period_s <= now:
    job.next_deadline += job.period_s
    job.missed += 1
```
This advances `next_deadline` forward until it's only one period behind `now`, without actually transmitting the skipped frames. The `missed` counter records how many periods were skipped. This means even under severe load, the bus is never flooded with queued-up frames. The next transmission fires for the *current* period only, and the job resumes normally from there. If a job is rejected due to ownership conflict, it is deleted entirely — the Scheduler does not retry.

---

## Feature 14 — The Settings Page

The user opens the **Settings** workspace. The UI calls `/settings`.

The `settings.py` handler builds a full snapshot of the live system state in a single call. It calls `physical_discovery()` which probes (or reads from cache) the CANalyst USB availability. The result determines whether the "Real" transport mode shows as `available: true` or `available: false` with a reason string. It pulls the active transport's `AdapterStatus` (health, epoch, channel states, tx/rx counts, queue high-water mark). It pulls the session snapshot (profile, phase, bench_tx, leases, jobs). It reads the protocol catalog (number of messages, wire hash, semantic hash). It reads the full `ToolkitConfig` object to show runtime parameters like `canalyst_poll_ms`, `canalyst_reconnect_initial_ms`, `history_capacity`, etc. It includes the control service snapshot, running synthetic peers, diagnostic episodes, active recording summary, and simulation status. All of this is assembled into one JSON response and returned. The Settings tab is a live read of real backend state — not a static config page.

---

## Feature 15 — Manual One-Shot Frame Injection

The user opens the Injection panel, picks a message (e.g. `VCU_SES_REQ`), fills in the signal values, and clicks **"Inject Once"**.

The UI sends a POST to `/injections` with `bus`, `key`, and a `values` dict. No `period_ms` is set, so the backend routes this as a one-shot. `require_bench_tx_enabled()` is called first — if TX is not armed, a 403 is returned immediately. Then `life.tx_gate.submit()` is called, which runs the full ownership + encoding pipeline. If the TxGate returns `"rejected"` for any reason (Bench TX off, ownership conflict, encode failure), the API catches it, writes an `inject.rejected` warning entry to the `AuditLog`, and returns a 409 to the UI with the exact rejection reason. If successful, a `inject.submitted` info entry is written to the AuditLog including the `data_hex` that was actually put on the wire, the `request_id`, and the owner. The UI shows the hex payload in the response card.

---

## Feature 16 — Periodic Manual Injection

The user opens the Injection panel, picks a message, and sets `period_ms: 50`. They click **"Inject Periodic"**.

The backend receives a POST to `/injections` with `period_ms: 50`. Since `period_ms > 0`, it does **not** call `TxGate.submit()`. Instead it calls `life.scheduler.schedule()` directly with the provided `bus`, `key`, `values`, and optional `counter_field`. A `PeriodicJob` is created and its `job_id` is returned to the UI. The AuditLog gets an `inject.scheduled` info entry. The frame will now fire every 50ms via the Scheduler's micro-loop, going through `TxGate` as normal.

To cancel it, the user clicks **"Stop"**. The UI sends a DELETE to `/injections/{job_id}`. The backend calls `scheduler.cancel(job_id)`. If the job doesn't exist (already expired or bad ID), a 404 is returned. If found, the job is removed from the Scheduler's registry and will never fire again. The ownership lease will expire naturally after its TTL.

---

## Feature 17 — Injection Preview (Dry Run)

Before injecting, the user clicks **"Preview"** to see what bytes will be put on the wire.

The UI sends a POST to `/injections/preview` with the same body. This endpoint does **not** call `TxGate` at all — it only calls `encode_message()`. No ownership check, no Bench TX check, no physical transmission. The encoder looks up the protocol catalog by `key`, finds the message instance matching the requested `bus`, resolves the CAN ID, packs the engineering values into a raw byte array via `proto.encode()`, and then immediately calls `proto.decode()` on the result as a round-trip self-check. If the decoded values don't match (e.g. a codec has a quantization rounding issue), a `"roundtrip_decode:{status}"` warning is added to the `warnings` list. The response returns the `data_hex`, `dlc`, `can_id`, decoded `signals`, and any `warnings`. The user sees exactly what bytes would hit the wire before committing.

---

## Feature 18 — Stop All (Emergency Neutral)

The user clicks the **"Stop All"** button. This is the catch-all safety reset that doesn't inject an E-STOP frame but does kill all active transmission.

The UI sends a POST to `/sessions/{id}/stop-all`. The `SessionManager.stop_all()` method acquires the lock and calls `_neutralize_locked()`: all Scheduler jobs are cancelled, all ownership leases are cleared, Bench TX is disabled. It does not change the session profile or phase — the session stays `RUNNING`, the transport stays connected. The revision increments. The UI receives the updated session state and all active "armed" indicators go dark. If the user was driving and hit Stop All, the `HOST_DRIVE_CMD` stream stops immediately. No zeroing frame is sent — this is a hard cut, not a graceful ramp-down. For graceful ramp-down, the watchdog path (which sends one final zero command) is used instead.

---

## Feature 19 — Closing a Session

The user closes the browser tab or explicitly clicks **"Close Session"**.

A DELETE is sent to `/sessions/{id}`. `SessionManager.close()` is called with an optional `expected_revision`. If the revision check fails (stale client), it returns 409. Otherwise it calls `_neutralize_locked()` to kill all jobs and leases, then advances the session phase to `STOPPED` (or whatever `outcome` was specified, e.g. `ABORTED`). The session is permanently in a terminal phase. Any future REST calls to this session ID will return 404. The `Lifecycle` service does not automatically destroy the transport — the transport continues running and listening. A new session can be created via POST to `/sessions` without needing to restart the backend.

---

## Feature 20 — Lease Management (Explicit API Leases)

An automated script (or LLM tool) wants to reserve exclusive control of CAN ID `0x300` on the High bus before starting a long test. It POSTs to `/sessions/{id}/leases` with `bus: "high", can_id: 0x300, owner: "test:script_001", ttl_s: 30`.

The `OwnershipTable.claim()` is called. It first runs `_expire_locked()` to evict any TTL-expired entries. It checks if `(high, 0x300)` is already held by a different owner. If yes, an `OwnershipConflict` is raised and a 409 is returned — the script knows it can't proceed safely. If the lease is held by the same `owner`, it refreshes the expiry instead of creating a duplicate. If the slot is free, a new `Lease` object is created with the generated `lease_id`, stored in two indices (`_by_key` and `_by_id`), and returned.

To renew a lease before it expires, a POST to `/sessions/{id}/leases/renew` with the `lease_id` and optional new `ttl_s`. The `OwnershipTable.renew()` looks the lease up by ID, updates its `expires_at_mono`, and returns it. If the `lease_id` is not found (it expired and was evicted), a 404 is returned — the script knows it lost the reservation and must re-claim.

To release early, a DELETE to `/sessions/{id}/leases/{lease_id}`. The lease is immediately removed from both indices without waiting for expiry.

---

## Feature 21 — HMI Mode and Power Control

The physical HMI board (buttons for AUTO/MANUAL and ON/OFF) may not be present on a bench. The user needs to command the vehicle's operating mode over CAN instead. They click **"Set AUTO"** in the HMI panel.

A POST goes to `/hmi/mode` with `req_mode: 1` (AUTO), `enabled: true`. `require_bench_tx_enabled()` is called first. If a previous HMI mode job was already running (stored in `life._hmi_jobs["hmi_mode"]`), it is cancelled in the Scheduler first, so there are never two competing mode-request streams. Then a new `PeriodicJob` is created for the `HMI_MODE_REQ` message on the High bus at 1 Hz (1000ms period), with `counter_field="rolling_counter"` so the Scheduler auto-increments it. The `SessionManager.update_vehicle_view(requested_mode="AUTO")` is called to reflect the *request* in the session state — this is not a confirmation that the vehicle responded. The job ID is stored. The vehicle's RT firmware will see the 1 Hz message and (if in the appropriate firmware state) transition to AUTO mode.

To stop sending the mode request, the user clicks **"Stop HMI Mode"**. POST to `/hmi/mode` with `enabled: false`. The existing job is cancelled, `_hmi_jobs` is cleared, no new job is created. The vehicle's last seen mode request fades away.

Power control (`/hmi/power`) works identically but targets `HMI_PWR_REQ` with `req_start: 0` (OFF) or `1` (ON) at 1 Hz.

---

## Feature 22 — Sequential Message Verification (Stimulus-Assert)

The user is running a bench test and wants to verify that sending `VCU_SEB_REQ` with `pressure_request_raw=50` causes the brake actuator to respond with `SEB_STATUS` containing `actual_pressure > 40`. They click **"Run Verification Step"** in the Test panel.

A POST goes to `/tests` with a body like:
```json
{
  "name": "Brake pressure response",
  "stimulus": { "type": "inject", "bus": "low", "key": "seb:vcu_seb_req", "values": { "pressure_request_raw": 50 } },
  "expect": { "type": "signal_in_range", "bus": "low", "name": "SEB_STATUS", "signal": "actual_pressure", "min": 40, "timeout_ms": 500 }
}
```

The `VerificationService.run()` first checks if another test is already `RUNNING`. If yes, it returns 409 — only one step can run at a time. A new `TestStepResult` is created in `RUNNING` state, and `_execute()` begins.

`require_bench_tx()` is checked. Then the **evidence gate** runs: it checks if a `RecordingSession` is currently active. If a recording is active but its `evidence_quality` is not `COMPLETE` (i.e. frames were already dropped), the test is immediately marked `INCONCLUSIVE` without even sending the stimulus. A formal `PASS` requires complete, uninterrupted evidence.

If evidence is acceptable, a **pre-step snapshot** is taken from the `LatestStore` — this captures the state of `SEB_STATUS` *before* the stimulus fires, stored in the result for comparison.

The stimulus is submitted via `TxGate.submit()`. If the TxGate rejects it (TX off, encode error, ownership conflict), the test is immediately marked `FAIL` with the rejection reason.

If the stimulus was accepted, a polling loop starts. Every 10ms it calls `_snapshot_message()` to read `SEB_STATUS` from the `LatestStore`. It runs `_matches()` against the expectation:
- For `signal_in_range`: it reads `actual_pressure`'s `engineering_value`, converts to float, checks `>= min` and `<= max`. If it matches, the test is marked `PASS` and the observed state is saved.
- The loop continues until the signal matches or the `timeout_ms` (500ms) elapses. On timeout, the test is marked `FAIL` with `"timeout after 500 ms waiting for signal_in_range"` and the last observed state is saved as evidence.

After completion (pass, fail, or error), the result is moved to the history list and `_active` is cleared. The UI can poll `/tests/{test_id}` to get the full result including `stimulus`, `expect`, `evidence`, `observed`, and `duration_ms`.

---

## Feature 23 — Protocol Browser and Bit-Grid Layout

The user opens the **CAN Dictionary** tab to understand what signals are inside `HOST_DRIVE_CMD`.

A GET to `/protocol/messages/high/0x300` is sent. The backend calls `_resolve_message("high", "0x300")`, which parses the hex string to int `0x300 = 768`, then calls `proto.message_key_for("high", 768)` to look up the catalog key (e.g. `"host:host_drive_cmd"`). If the message doesn't exist on that bus, a 404 is returned. If found, it retrieves the full catalog entry (name, DLC, instances, all signal definitions) and calls `build_bit_grid()`.

`build_bit_grid()` takes the message metadata and produces a 64-entry array (one entry per bit across 8 bytes). Each entry specifies which signal occupies that bit, the bit's role (start bit, middle, end), the endianness, whether it overlaps with another signal, and display metadata. This is what the UI renders as the bit-layout grid — the colored boxes that visually show where each field sits within the 8-byte CAN payload.

The `/protocol/messages/{bus}/{can_id}/layout` endpoint adds a **live overlay** on top: it takes the same bit-grid but also reads the current `LatestStore` for that message and overlays the live engineering values, enum labels, and raw values per signal — so the bit-grid shows not just the layout but also the current value highlighted in real time.

If the YAML protocol files are regenerated (e.g. new firmware adds a signal), the user can click **"Refresh Protocol"**. A POST to `/protocol/dictionary/refresh` dynamically reloads the `etrike_protocol` Python package via `importlib.reload()`, updates all cached hashes in `proto`, and clears the LRU cache on `_bus_id_index`. This updates the entire catalog without restarting the backend.

---

## Feature 24 — The WebSocket Stream (How the UI Stays Live)

The user opens any live view. The React app calls `useStream()`, which opens a WebSocket to `/stream`.

The backend's `stream()` handler immediately calls `ws.accept()`. A `send_lock` (an `asyncio.Lock`) is created per-connection — this serializes all outgoing sends so two concurrent async tasks (the event pump and a ping handler) cannot race and corrupt the WebSocket frame framing.

The handler sends a `{"type": "hello", "wire_hash": "..."}` message. The `wire_hash` is a hash of the compiled protocol binary. If the frontend was built against a different protocol version, it can detect the mismatch and warn the user.

Immediately after, a full state snapshot is taken from the `LatestStore` and sent as `{"type": "state", "initial": true, "messages": [...]}`. This means the UI shows real data from the very first render, before the Lifecycle's broadcast loop fires.

Two async tasks are then created:
- `pump_events`: blocks on the per-client `asyncio.Queue` (max 64 items). Whenever the Lifecycle broadcasts a new state batch, this task wakes up and sends it to the browser.
- `pump_client`: owns the sole `ws.receive()` call for this connection. The comment in the code is explicit: there must never be a concurrent `receive()` call on the same socket because Uvicorn keeps the old ASGI future alive after `Task.cancel()`, causing a `RuntimeError: Concurrent call to receive()` that tears down the handler. This task handles client pings (text messages echoed back as `ack`) and detects the `websocket.disconnect` message type to break the loop.

Both tasks run concurrently via `asyncio.wait(return_when=FIRST_COMPLETED)`. If either ends (the browser closed, or the server stopped pumping), both tasks are cancelled. In the `finally` block, `lifecycle.events.unsubscribe(sid)` removes the per-client queue from the `EventBus`.

---

## Feature 25 — The EventBus: Slow Client Isolation

If a browser tab is throttled (e.g. the user put the tab in the background and the browser suspended it), the WebSocket still exists but the `pump_events` task stops consuming from its queue. The EventBus must never block waiting for a slow client.

When the Lifecycle publishes a new state batch, `EventBus.publish()` iterates over all subscriber queues and calls `q.put_nowait(event)`. If a queue is full (the client is slow), instead of blocking or skipping, it executes a "drop-oldest-retry" pattern: it calls `q.get_nowait()` to evict the oldest (most stale) state batch, then calls `q.put_nowait(event)` again with the newest state. This means a slow client always receives the most recent state when it wakes up — it just misses intermediate frames — rather than accumulating a backlog that causes memory growth and eventually an OOM crash.

---

## Feature 26 — The LatestStore: Observed Rate Tracking

Every time a frame arrives and the Router upserts a `MessageState`, the `LatestStore` does more than just overwrite the old state. It also tracks the **inter-arrival time** for each `(bus, can_id)` pair.

On `upsert()`, it checks `_prev_seen_ns` for the previous arrival timestamp. If it exists and the new `last_seen_ns` is strictly later, it computes `dt_s = (new_ns - prev_ns) / 1e9` and sets `observed_rate_hz = 1.0 / dt_s`. This is stored in the `MessageState` and broadcast to the UI. The UI shows both `expected_rate_hz` (from the YAML `cycle_ms` field) and `observed_rate_hz` (measured live) side by side — if they diverge significantly, the operator knows the bus is under stress or the ECU is misbehaving.

---

## Feature 27 — The FrameHistory: Overflow Accounting

The `FrameHistory` is a fixed-capacity ring buffer (default 4096 frames) that stores every raw `RawFrameEnvelope` in chronological order for the raw frame monitor panel.

Unlike a plain `deque(maxlen=...)` which silently evicts old items, `FrameHistory` tracks evictions explicitly. When `append()` is called and `len(self._ring) == self._capacity`, it increments `self._dropped` *before* appending (because the append will evict the oldest). This means the `metrics()` response always shows exactly how many frames were silently overwritten — the operator knows whether the chronological view is complete or has gaps.

---

## Feature 28 — Diagnostics: Episode Aggregation and Anti-Chatter

The `DiagnosticsService` has two layers: **events** (individual occurrences) and **episodes** (aggregated fault conditions).

When `emit()` is called with severity `warning`, `error`, or `critical`, two things happen. First, the event is prepended to the `_events` deque (max 2000 entries). Second, `_touch_episode_locked()` creates or updates an `Episode` keyed by `"{code}|{scope}"` (scope is the bus name, or `"global"` if no bus). If no episode exists, a new one starts. If an episode exists but was previously `recovered`, a fresh episode is created (the old recovered one is replaced). If an episode exists and is not recovered, `count` is incremented and `last_mono` is updated.

When the fault clears (e.g. the CANalyst adapter reconnects), `recover()` is called. It does **not** immediately mark the episode recovered. Instead it records a `pending_recover` timestamp. On the next call, if `time.monotonic() - pending_recover >= recovery_hysteresis_s` (default 0.5s), the episode is finally marked `recovered=True`. This anti-chatter mechanism prevents a flapping connection (plug/unplug/plug in rapid succession) from creating and recovering dozens of episodes per second.

Critically, if a new `emit()` with `warning/error/critical` fires for the same code while a recovery is pending, `_pending_recover.pop(key)` is called — the recovery is cancelled, and the episode count increments again. The episode only recovers once it has genuinely been stable for 500ms.

---

## Feature 29 — The Encoder: Round-Trip Self-Check

Every time a frame is encoded — whether for one-shot injection, periodic scheduling, or verification stimulus — the `encode_message()` function performs a **round-trip self-check**.

After `proto.encode(key, values)` succeeds, it immediately calls `proto.decode(key, frame)` on the result. If the decode status is not `"ok"`, a warning string `"roundtrip_decode:{status}"` is appended to the `warnings` list in the `EncodeResult`. The frame is still transmitted — the check is non-blocking — but the warning is visible in the `/injections/preview` response and in the Audit Log. This catches codec bugs (e.g. a signal whose scaled encoding produces a slightly different decoded value due to integer quantization) before they silently corrupt data on the wire.

---

## Feature 30 — The Virtual Transport: Dual-Bus Emitter Architecture

When the system runs in Computer mode, the `VirtualTransportAdapter` sets up an unusual dual-bus architecture per channel using `python-can`'s `virtual` interface.

For each of the two channels (High, Low), two separate `can.Bus` instances are created on the same named channel (e.g. `"etrike_high_a3b1c2f4"`). One is the **receiver bus** (`rx`, created with `receive_own_messages=False`). The other is the **emitter bus** (`tx`, also with `receive_own_messages=False`). A `can.Notifier` is attached to the receiver bus with a `_QueueListener`. 

When a frame is sent via `transport.send()`, the adapter calls the **emitter bus** to publish the frame. Because both buses share the same named channel, the emitter's send appears on the receiver's notifier — the listener fires, the frame is queued, and the Router picks it up as an RX frame. This means injected frames (from the Scheduler or TxGate) are also observed by the Router, correctly appearing in the live frame monitor and `LatestStore` as `source=VIRTUAL`.

The `tx_listeners` list supports the NativeSilBridge: when any frame is sent on the virtual High bus, the SIL bridge's `_on_tx` listener checks if it's `HOST_DRIVE_CMD` and, if so, forwards it to the physics simulator process. The frame completes its send first, then listeners are notified — so listeners cannot interfere with the transmission itself.
