# E-Trike Control UI Logic Specification

**Status:** Behavioral design for implementation

**Purpose:** Define how every major part of the bench controller behaves: what it receives, what state it owns, what decisions it makes, what it outputs, and how its result is verified.

**Related documents:** `scope.md`, `control-ui-achitecture.md`, `hmi.md`, and the shared CAN YAML files.

## 1. System boundary

The Control UI is a CAN bench-testing system for RT, SYS, MTR, and supporting units. Driving-like inputs exist to exercise firmware code paths on a controlled bench or stationary integration setup. The application is not used to drive the E-Trike.

The backend is authoritative for CAN communication, protocol interpretation, timing, test state, scheduled transmission, validation, and recording. The browser displays backend state and sends test intent.

## 2. Core identities

Every operation uses stable identities so data from different buses, sessions, and sources cannot be mixed accidentally.

| Identity | Meaning |
|---|---|
| Backend session | One backend process lifetime |
| Adapter epoch | One successful opening of a physical or virtual adapter |
| Test session | One explicitly started bench or software test configuration |
| Frame sequence | Monotonic order assigned to every accepted RX/TX observation |
| Channel sequence | Per-channel receive order |
| Test step | One stimulus-and-assertion operation within a test case |
| Command correlation | One requested one-shot or periodic stimulus operation |
| Source owner | The UI, test runner, HMI scheduler, synthetic ECU, or other producer that owns a bus/ID |

An adapter reconnect creates a new adapter epoch. Old scheduled jobs and ownership never cross into the new epoch.

## 3. Shared operating state

### 3.1 Work profile

The profile is one of:

- **Full Vehicle:** both physical buses, complete stationary network, passive by default, explicit test stimuli permitted.
- **Bench Test:** selected physical ECUs and buses, missing peers may be synthesized.
- **Pure Software:** two virtual buses and software peers; no physical transmission.

### 3.2 Bench TX state

Bench TX is either Disabled or Enabled.

- Connecting an adapter leaves it Disabled.
- Starting a test that needs physical transmission requests Enabled.
- Stopping the test, changing profile, disconnecting, or shutting down returns Disabled.
- Reconnection never restores Enabled.
- Passive monitoring and recording work while Disabled.

### 3.3 Test-session state

```text
Stopped
  → Preparing
  → Listening
  → Running
  → Stopping
  → Completed | Failed | Inconclusive
```

- **Preparing:** validate protocol hash, hardware, configuration, test definition, and TX manifest.
- **Listening:** observe the bus before transmitting and detect already-present producers.
- **Running:** execute stimuli and assertions.
- **Stopping:** cancel jobs, perform defined cleanup, and finalize evidence.
- **Completed:** all required assertions passed.
- **Failed:** an assertion proved incorrect ECU behavior.
- **Inconclusive:** infrastructure loss, conflicting producer, hidden data loss, or insufficient evidence prevents a valid conclusion.

## 4. Startup logic

1. Load and validate application configuration.
2. Load generated Python protocol artifacts and semantic hash.
3. Start storage, router, scheduler, and WebSocket services with no active adapter.
4. Discover CANalyst-II devices.
5. If the selected profile is physical, show discovered devices and open only the configured selection.
6. If the profile is Pure Software, create named virtual High and Low buses.
7. Create a new adapter epoch and timestamp mapping.
8. Start receive processing with Bench TX Disabled.
9. Observe traffic and build initial latest-state/topology data.
10. Accept UI connections and compare frontend/backend protocol semantic hashes.

If the YAML-generated frontend and backend hashes differ, the UI enters Protocol Mismatch. Raw monitoring remains available, but decoded stimulus is disabled.

Physical adapter failure does not silently switch to Pure Software. The engineer explicitly changes profile.

## 5. CAN receive logic

For every frame returned by `python-can`:

1. Capture adapter epoch, device channel, device timestamp, and backend arrival time.
2. Convert Channel 0 to High and Channel 1 to Low using the explicit configuration.
3. Preserve CAN ID, standard/extended flag, remote/data flag, DLC, and exactly the DLC payload bytes.
4. Assign per-channel sequence.
5. Enqueue the raw envelope immediately; do not decode in the receive callback.
6. If the queue is full, emit an observable overflow event and increment a lost-frame counter.

The router then:

1. Validates the raw CAN envelope.
2. Maps the device-relative timestamp into the backend session timebase.
3. Detects device timestamp reset or wrap and starts a new mapping segment.
4. Assigns the global frame sequence.
5. Looks up the definition by `bus + CAN ID`.
6. Decodes known messages using YAML-generated logic.
7. Runs integrity, sequence, timing, and semantic validation.
8. Updates message/node timing state.
9. Updates the latest observation and latest valid value stores.
10. Enqueues recording data if recording is active.
11. Publishes critical transitions immediately and ordinary latest-state changes through coalescing.

Unknown frames remain visible and recordable. They do not cause guessed decoding.

## 6. Timestamp and ordering logic

The CANalyst-II device timestamp is retained as source evidence. Backend arrival uses a monotonic host clock. Mapped session time is monotonic even if the adapter timestamp wraps or resets.

Within each channel, receive order is authoritative. Across High and Low, ingestion order is not authoritative because the adapter returns channel groups separately. Cross-bus analysis uses mapped device timestamp, then channel sequence as a deterministic tie-breaker.

Every displayed value includes:

- sample timestamp;
- arrival timestamp;
- age;
- timestamp source and resolution;
- adapter epoch;
- validity.

## 7. Decode logic

The generated decoder receives bus, ID, DLC, and payload.

1. Confirm the message exists on that bus.
2. Confirm DLC matches the YAML definition.
3. Extract signals using the generated canonical Intel or Motorola bit mapping.
4. Apply signed interpretation.
5. Apply `physical = raw × factor + offset`.
6. Resolve boolean and enum labels.
7. Apply multiplexing/overlap conditions.
8. Return raw signal values, engineering values, labels, and warnings separately.

A decode failure preserves the raw frame and produces no fabricated engineering values.

## 8. Integrity and corruption logic

Validation results are structured per rule:

- known ID on expected bus;
- correct DLC;
- valid signal representation;
- enum membership;
- declared range;
- checksum/XOR/CRC where defined;
- rolling-counter continuity;
- alive-counter advancement;
- cycle-time behavior;
- mandatory enable bits;
- cross-signal rules;
- test-specific command/feedback plausibility.

Application checksum failure marks the frame Corrupt. Counter gaps are initially Sequence Discontinuity because adapter loss, queue loss, sender loss, or reordering may produce the same evidence.

Invalid frames:

- remain in raw history and recordings;
- do not overwrite the latest valid engineering value;
- update latest observation with the invalid reason;
- make dependent derived values Invalid or Degraded;
- produce a diagnostic transition on first failure and recovery.

CAN wire-CRC failures are generally rejected by CAN hardware. The UI reports wire errors only when the adapter exposes evidence; it does not claim to display payloads that hardware discarded.

## 9. Freshness logic

Each periodic YAML message defines an expected period or explicit timeout rule.

For every `bus + ID`:

- **Unseen:** no frame has arrived in the current adapter epoch.
- **Live:** latest valid frame is inside the live deadline.
- **Late:** expected cycles have been missed.
- **Missing:** offline deadline exceeded.
- **Invalid:** frames arrive, but recent observations fail validation.
- **Frozen:** frames arrive, but the alive counter does not advance.
- **Recovering:** valid advancing frames resumed but stability count is not yet satisfied.

The backend owns these transitions. A deadline scheduler updates freshness even when no CAN frames arrive.

The UI receives absolute deadlines and current state. Its local timer updates displayed ages, but it does not invent node liveness.

## 10. Adapter and connection logic

Connection evidence is separated:

1. USB device present.
2. Adapter opened.
3. High channel configured.
4. Low channel configured.
5. Channel traffic observed.
6. Expected protocol messages observed.
7. Specific ECU heartbeat valid and advancing.
8. Backend WebSocket connected and current.

A quiet channel is not called disconnected. Unsupported CANalyst-II evidence such as TEC/REC or bus-off is displayed Unknown rather than zero/healthy.

The CANalyst-II wrapper does not use `python-can Bus.state` as health evidence because the backend inherits the generic `ACTIVE` default. It monitors `Notifier.exception`, listener `on_error()`, receive-worker liveness, adapter-worker heartbeat, USB/device I/O errors, and selected-device presence instead.

While a physical adapter is selected, a low-rate device-presence check may supplement I/O errors. It must be benchmarked to prove that enumeration does not disturb the open libusb handle. If it is unreliable, adapter isolation moves to a supervised worker process and worker heartbeat plus I/O failure remain the primary loss evidence.

Initial detection targets are:

- receive/USB exception: immediate failure transition;
- adapter-worker heartbeat: Degraded after 500 ms, Failed after 1.5 s;
- USB presence probe: every 500 ms when supported safely;
- browser stream heartbeat: Degraded after 750 ms, Lost after 1.5 s;
- ECU/message loss: YAML-defined deadlines only.

### 10.1 Disconnect

When physical communication fails:

1. Disable Bench TX.
2. Cancel scheduled physical jobs.
3. End active stimulus leases.
4. Mark the running test Inconclusive unless its definition expected the disconnect.
5. Mark physical values stale while preserving last values and ages.
6. Close the adapter.
7. Begin visible reconnect attempts.

Fast reconnect attempts transition to indefinite slow discovery rather than stopping permanently. Reconnect never enables Bench TX or resumes a prior job.

### 10.2 Reconnect

1. Discover and reopen the configured device.
2. Create a new adapter epoch.
3. Clear/drain stale hardware buffers as a best effort.
4. Reset timestamp mapping and per-epoch validation state.
5. Start receive-only observation.
6. Wait for stable valid traffic.
7. Report Recovered.
8. Require the engineer to restart the test and re-enable Bench TX.

## 11. ECU topology logic

The YAML compiler generates expected sender roles, receivers, buses, and heartbeat/status messages.

For each ECU role:

1. Evaluate its defining messages independently per bus.
2. Combine liveness, integrity, fault signals, and provenance.
3. Mark it Physical, Synthetic, Virtual, Conflicted, or Unseen.
4. Show Live, Late, Missing, Invalid, Frozen, Recovering, or Fault.

Physical sender identity is an expectation from the YAML ID assignment, not proof of which electrical device transmitted the frame.

If physical traffic appears for an ID currently owned by a synthetic source, stop the synthetic job for that ID and mark the test conflict according to its rule.

## 12. WebSocket and visual update logic

On connection:

1. Authenticate local session/origin.
2. Exchange stream version and protocol semantic hash.
3. Estimate browser/backend clock offset and uncertainty.
4. Accept subscription filters.
5. Send one atomic initial snapshot with a sequence boundary.
6. Send only deltas after that sequence.

Streams are separated:

- critical events: always subscribed;
- latest state: coalesced by bus/ID and filtered to visible needs;
- raw chronological frames: subscribed only while requested;
- recording: internal backend path, never dependent on browser delivery.

The UI repaints latest values at a controlled cadence. ESTOP state, adapter loss, source conflict, test failure, and new corruption render immediately.

Every batch has a sequence. A detected gap makes the view Degraded and triggers a fresh snapshot. Slow clients have independent bounded queues and cannot block CAN RX, tests, or recording.

The default live CAN table uses latest-ID overwrite keyed by `bus + ID`, following the proven analyzer pattern. Every row retains total count, previous and latest timestamps, observed delta/rate, changed-byte mask, source, validity, and age. Chronological mode is separate and opt-in.

Each WebSocket client has its own outbound queue. Latest-state data may coalesce by key; raw-monitor batches may drop oldest data only with a visible gap counter. One slow browser cannot delay other browsers or backend processing.

## 13. CAN dictionary logic

The dictionary reads generated TypeScript metadata from YAML.

1. Search by ID, message, ECU, signal, enum text, or comment.
2. Filter High, Low, category, and transmission policy.
3. Show message identity, route, DLC, period, byte order, and provenance.
4. Render the generated canonical byte/bit map.
5. Link bit colors to signal table rows.
6. Optionally overlay latest value, validity, and age.

The dictionary never merges undocumented API fallback definitions into authoritative YAML data without an explicit warning.

## 14. Encode logic

The encoder receives a message definition and human-readable signal values.

1. Confirm the current profile/test permits that message and bus.
2. Resolve defaults and enum selections.
3. Validate required fields and ranges.
4. Apply inverse scaling to raw values.
5. Pack signals using generated Intel/Motorola mapping.
6. Apply multiplexing rules.
7. Force mandatory enable fields for positive tests.
8. Insert the current rolling counter.
9. Calculate checksum after all protected bytes are final.
10. Verify the resulting DLC and self-decode it as a round-trip check.
11. Return semantic values, raw payload preview, and automatic-field explanation.

Negative tests explicitly declare which normal validation rule is intentionally violated. All other rules remain enforced.

## 15. Generic injection logic

1. Select bus and YAML-defined message.
2. Load defaults and allowed test policy.
3. Edit engineering values.
4. Continuously show validation and encoded preview.
5. Select one-shot, finite count, or deliberately continuous mode.
6. Show the exact transmit manifest.
7. On Start, acquire source ownership for `bus + ID`.
8. Encode and submit through the backend scheduler/TX gate.
9. Display Accepted, Queued, Submitted, Rejected, Expired, Canceled, or Failed.
10. Stop/release ownership when complete.

Submitted means handed to the adapter library. It does not mean an ECU received or accepted the frame. ECU acceptance is inferred only from an expected observed response.

## 16. Periodic transmission logic

For each periodic job:

1. Bind the job to test session, adapter epoch, source owner, bus, and ID.
2. Use an absolute monotonic next deadline.
3. At each deadline, obtain current semantic values.
4. Increment the correct per-job/per-bus counter.
5. Encode and calculate checksum.
6. Reject if ownership, session, epoch, Bench TX, or deadline is invalid.
7. Submit once.
8. Measure requested-versus-actual submission jitter.
9. Schedule the next future deadline.

If execution is late by more than the allowed window, record a missed period and skip stale instances. Never burst multiple old frames to catch up.

## 17. Synthetic-peer logic

Before starting:

1. Determine physical targets and declared present peers.
2. Build required synthetic message set.
3. Listen for each claimed ID for its detection window.
4. Refuse or flag any ID already present physically.
5. Show all initial values, periods, and buses.

While running:

- generate each frame at its YAML/test-defined rate;
- keep independent counters per bus/ID;
- expose lateness and missed-period metrics;
- stop an ID immediately if conflicting physical traffic appears;
- never synthesize an undeclared node automatically.

Required startup behavior such as aligned steering status and zero virtual speed is part of the synthetic template, not hidden code.

## 18. HMI test logic

### 18.1 Mode

1. Engineer selects MANUAL, AUTO, or PURE SIM.
2. Backend stores requested mode for the current HMI job.
3. Scheduler sends `0x111` at 1 Hz and advances its alive counter.
4. Backend observes SYS/RT mode/status messages.
5. UI shows requested versus observed mode and transition latency.
6. A formal test passes only when the expected state appears within its defined window and remains stable for the required duration.

### 18.2 Power

The same process applies to `0x112`: requested OFF/ON, periodic alive counter, observed SYS state, transition timing, and result.

### 18.3 ESTOP

1. Inject the DLC=0 `0x001` event once.
2. Record submission time and bus.
3. Observe RT/SYS ESTOP state, propagation, command suppression, and diagnostic effects.
4. Apply the test’s defined recovery inputs.
5. Verify the latch and recovery sequence.

This is a protocol test, not the physical bench emergency control.

## 19. Keyboard/gamepad test logic

Keyboard/gamepad remains required to provide continuous test input conveniently.

1. The browser converts key/axis state into a target speed, yaw/steering, gear, brake, or selected direct-test value.
2. It sends target intent plus monotonic client sequence at a bounded rate.
3. The backend rejects stale/out-of-order intent.
4. The backend renews the stimulus lease while intent remains current.
5. Backend shaping applies YAML/test-defined deadband, acceleration, deceleration, steering-rate, and direction-change rules using measured `dt`.
6. The scheduler produces the selected CAN command at its required period.
7. The UI shows raw input, target, shaped command, encoded command, and ECU feedback separately.

Key/controller state clears on key release, blur, tab hiding, controller disconnect, workspace change, WebSocket degradation, or Stop.

On intent loss, the backend follows the selected test’s end behavior and ends the lease. A formal test records the resulting RT/SYS/MTR timeout response.

## 20. Kinematics-mode logic

1. Select RT as the physical target and Host as the mimicked role.
2. Ensure `0x300` is not produced by a physical Host.
3. Acquire High-bus `0x300` source ownership.
4. Generate Host speed/yaw/gear command from preset, sequence, keyboard, or gamepad input.
5. Observe RT state and Low-bus motor/steer/brake requests.
6. Compare outputs with expected kinematics, limits, route, period, checksum, and counter rules.
7. Correlate actuator feedback when physical or synthetic units are present.

## 21. Direct-actuator test logic

1. Select the target unit and Low-bus command definition.
2. Confirm RT or another physical producer is not sending the same ID.
3. Acquire source ownership.
4. Set engineering input or run a defined step/ramp/boundary sequence.
5. Force required enable bits for positive tests.
6. Generate counters/checksums per frame.
7. Observe matching feedback/status/diagnostic frames.
8. Compare requested and measured values with timing and tolerance.
9. Stop the command job and release ownership.

## 22. Virtual-encoder logic

1. Select a constant, ramp, recorded trace, or model-derived speed.
2. Convert it to YAML-defined `0x206 MTR_MOTOR_FBK` values.
3. Generate at the required period with source marked Synthetic MTR.
4. Observe RT/SYS plausibility, state, and fault outputs.
5. Record commanded virtual speed versus ECU interpretation.

The topology must never display the synthetic MTR as physical.

## 23. Test-runner logic

Each test definition contains preconditions, stimuli, assertions, timeouts, cleanup, and evidence requirements.

### 23.1 Preparation

1. Validate schema and protocol hash.
2. Check target hardware and bus mapping.
3. Check expected physical/synthetic roles.
4. Build the TX manifest.
5. Listen for source conflicts.
6. Start recording before the first stimulus.

### 23.2 Step execution

For each step:

1. Capture the pre-step state boundary.
2. Apply one stimulus or change one scheduled job.
3. Start assertion timing from actual submission or defined observation.
4. Evaluate all required messages/signals continuously.
5. Pass when the required condition is valid and stable.
6. Fail when contradictory valid evidence proves incorrect behavior.
7. Mark Inconclusive when evidence is missing because of infrastructure loss or capture gaps.
8. Store the exact frames and events supporting the result.

### 23.3 Completion

1. Execute cleanup.
2. Stop all test-owned periodic jobs.
3. Release source ownership.
4. Disable Bench TX when no other test owns it.
5. Finalize recording integrity.
6. Produce result summary with firmware/protocol versions and evidence links.

## 24. Assertion logic

Supported assertion types include:

- message observed/not observed;
- signal equals enum/boolean;
- numeric value inside tolerance;
- transition occurs within time window;
- frame period/frequency within tolerance;
- counter advances correctly;
- checksum valid/invalid as expected;
- message routed to correct bus;
- message suppressed after timeout/ESTOP;
- diagnostic fault appears/clears;
- command and feedback difference within tolerance;
- no unexpected source conflict, overflow, or capture gap.

Assertions evaluate only valid frames unless the test explicitly targets invalid-frame handling.

## 25. Recording logic

Recording starts before stimuli and stores:

- raw RX and observed TX frames;
- device and mapped timestamps;
- bus, channel, direction, and provenance;
- adapter and test epochs;
- transport events and queue loss;
- protocol semantic/source hashes;
- test step boundaries and dispositions;
- configuration and software/firmware version metadata.

Decoded projections may be stored for query speed but are disposable. Raw observations remain authoritative.

If storage cannot keep up, mark the recording Incomplete immediately and make affected formal tests Inconclusive. Never silently drop evidence.

## 26. Diagnostic logic

1. Classify diagnostic messages from generated metadata.
2. Decode flags and severity.
3. Track first occurrence, count, last occurrence, and recovery.
4. Link each transition to the active test step and nearby stimuli.
5. Keep diagnostic timeline separate from ordinary high-rate frames.
6. Allow export with raw evidence.

## 27. Dashboard logic

Dashboard values come from explicit source selectors, not arbitrary first-match IDs.

For every tile:

- define contributing bus/message/signal;
- show requested stimulus separately from observed ECU state;
- show unit, validity, age, and provenance;
- show Unavailable instead of zero when unseen;
- retain stale last value only with clear stale age;
- taint derived values if any dependency is stale or invalid.

Command/feedback pairs use a common mapped timebase and never compare samples from different adapter epochs.

## 28. Stop All logic

Stop All is available in every active test view.

1. Disable creation of new TX jobs.
2. Cancel all active stimulus and synthetic jobs for the session.
3. Apply any declared finite cleanup frames.
4. Release all source ownership.
5. Set Bench TX Disabled.
6. Keep RX, diagnostics, and recording active long enough to observe ECU timeout/recovery behavior.
7. Mark the reason and timestamp in evidence.

Stop All is a deterministic test-control function, not a physical emergency disconnect.

## 29. Shutdown logic

1. Stop accepting new tests and stimuli.
2. Run Stop All.
3. Stop scheduler after jobs are canceled.
4. Stop receive notifier/worker.
5. Drain or mark recording incomplete.
6. Close the CAN bus and adapter.
7. Close WebSocket clients.
8. Stop backend services.

Shutdown is idempotent and reports incomplete cleanup rather than hanging indefinitely.

## 30. Logic verification strategy

Every logic unit has three test levels:

1. **Pure unit:** generated vectors, fake clock, fake queue, and no hardware.
2. **Virtual integration:** two virtual buses, router, scheduler, WebSocket, and software ECU peers.
3. **Hardware characterization:** CANalyst-II plus loopback or selected bench ECU.

Critical scenarios include:

- dual-channel ordering and timestamp wrap;
- silent bus versus disconnected USB;
- WebSocket loss during continuous keyboard stimulus;
- source conflict appearing after synthetic start;
- checksum/counter positive and negative cases;
- queue/storage overload with visible Inconclusive result;
- reconnect creating a new epoch without resuming TX;
- DLC=0 ESTOP event;
- HMI mode/power transition;
- Host command to RT output correlation;
- MTR feedback and virtual encoder behavior;
- Stop All during every active workflow.

## 31. Implementation rule

No UI component should contain vehicle protocol algorithms, periodic CAN scheduling, checksum logic, liveness decisions, or formal test verdicts. Those belong to generated protocol/runtime services in the backend. The UI owns presentation, filtering, local input capture, and test intent only.

## 32. Trigger and pre/post capture logic

1. Continuously append raw envelopes and transport events to a time-and-size-bounded backend ring.
2. Evaluate compiled predicates after decode/validation while retaining the raw sequence reference.
3. On the first match, atomically record the trigger boundary and protect the configured pre-trigger window from eviction.
4. Continue raw capture until the post-trigger deadline or explicit stop.
5. Coalesce repeated matches into the same capture unless the trigger definition requests retriggering.
6. Finalize with trigger details, sequence bounds, adapter epoch, protocol hash, loss counters, and evidence quality.

If the protected data cannot be retained or written, the capture becomes `Incomplete` immediately. Browser disconnection does not stop it.

## 33. Predicate logic

Filters, triggers, and assertions share a typed predicate schema. Resolution binds semantic paths to an exact generated protocol hash before execution. Predicates may inspect bus, ID/name, signal value, enum, raw bytes, validity reason, direction, provenance, freshness, count/rate, and bounded temporal relationships.

Unknown signals, unit mismatches, invalid operators, or a changed protocol hash are compile errors. Missing data evaluates `Unknown`, not `False`; each consumer explicitly decides whether Unknown means wait, no visual match, or Inconclusive test evidence.

## 34. Replay logic

1. Open and integrity-check the immutable capture and its protocol metadata.
2. Create a replay epoch distinct from adapter epochs.
3. Drive the normal router/decoder/validator/freshness/assertion pipeline from a virtual monotonic clock.
4. Preserve recorded device/arrival timestamps as evidence while deriving replay presentation time separately.
5. On seek, restore the nearest indexed checkpoint, clear transient projections, and replay forward to the target time.
6. Mark every resulting value `Replay`; keep physical Bench TX disabled.

Pause freezes virtual time, so freshness deadlines do not expire while paused. Speed changes presentation time only and cannot change recorded timing assertions.

## 35. Baseline comparison logic

1. Verify protocol semantic compatibility, required topology, test identity/input manifest, and evidence quality.
2. Align runs by declared test-step boundaries; use mapped time only within each epoch.
3. Compare message presence, period distribution, response latency, signal distribution/range, validity transitions, and diagnostics.
4. Apply per-test tolerances rather than global guessed thresholds.
5. Link every reported difference to evidence in both runs.

Incompatible semantics/topology yields `Not comparable`. Capture gaps or incomplete recording yields `Inconclusive`; neither is reported as an ECU regression.

## 36. Evidence-quality logic

Evidence quality is maintained separately from Pass/Fail:

- any relevant adapter epoch change, RX/router/recording loss, unknown timestamp interval, or assertion-worker failure makes it `Incomplete`;
- browser-only coalescing/drop may mark presentation `Degraded` but does not affect backend evidence;
- an unsupported adapter metric remains `Unknown` and affects quality only when a test explicitly requires it;
- a formal test may Fail with complete evidence, but may Pass only with complete evidence;
- infrastructure-caused missing proof yields Inconclusive.

The result report states the first degrading event, all affected intervals, and which assertions depended on them.

## 37. Adapter conformance logic

Characterization binds results to adapter identity, driver/library versions, OS/USB fingerprint, channel mapping, bitrate, and poll configuration. Each test produces measured evidence and a supported/unsupported/unreliable capability outcome. Required cases cover frame formats/DLCs, timestamps, ordering, echo, overflow, sustained load, unplug/replug, reopen, and submission jitter.

A fingerprint change invalidates the approval. Physical formal tests show a blocking `Characterization outdated` state until required P0 cases pass; raw exploratory monitoring may continue with a visible uncharacterized badge.

## 38. Overload degradation logic

Every stage publishes rate, capacity, depth, high-water mark, oldest age, processing latency, and loss/coalescing counters. Threshold transitions are events in the recording.

When a budget is exceeded, disable hidden computations first, reduce plot retention/resolution second, coalesce display state third, and shed opt-in raw browser delivery last. Backend validation, active assertions, adapter supervision, scheduler deadlines, and critical events remain active. If raw recording or assertion input cannot remain complete, stop claiming valid formal results and mark active tests Inconclusive.

## 39. Session annotation logic

Manual bookmarks and notes bind to session/replay epoch, mapped timestamp, per-channel sequence boundary, active test step, and author/session identity. Editing annotations never edits raw evidence. Selecting an annotation, failed assertion, corruption event, or diagnostic transition opens the same synchronized time window across raw frames, decoded signals, topology events, and test steps.

## 40. Vehicle projection logic

The backend creates one atomic `VehicleProjection` from the latest valid source records. It contains two parallel state trees:

- `actuation`: commands actually observed on CAN or emitted by an active backend job toward motor, steering, brake, gear, lamps, and other hardware;
- `sensors`: feedback/status actually observed from motor, encoder, steering, brake, SYS/RT state, and other sensors/actuators.

Each field contains value, unit, semantic source, bus/ID/signal, provenance, sample time, age/deadline, validity, adapter/replay epoch, and whether it is actuation, sensor, derived, or projected. No field is populated directly from a keyboard or gamepad event.

1. Resolve the configured primary actuation source and primary sensor source for every visual property independently.
2. Use a declared fallback only if its compatibility rule permits it; report fallback use explicitly.
3. Refuse to combine epochs or incompatible timestamps.
4. Calculate actuation-sensor deltas from time-aligned valid samples within the declared alignment window.
5. Calculate curvature and turn radius from physical wheelbase and observed steering convention.
6. Calculate a path only when speed, steering, dimensions, and direction are valid.
7. Publish the projection as one versioned snapshot/delta so the browser cannot draw a half-old, half-new vehicle state.

Derived fields retain dependency references. If a dependency becomes stale, invalid, missing, or changes epoch, the dependent result transitions immediately and is not recomputed from a fabricated zero. Loss of one state tree does not invalidate independent fields in the other tree.

Browser input follows a separate path: input intent → backend shaping/scheduler → encoded or observed CAN command → actuation projection. A raw key press is never vehicle state and is never painted as actuator motion.

## 41. Vehicle preview rendering logic

The browser renders actuation and sensor layers independently on the same vehicle. The default overlay uses outlined/translucent actuation geometry and solid sensor geometry, with selectable `Overlay`, `Actuation only`, and `Sensors only` presentation modes. These modes affect drawing only, not subscription, capture, or projection state.

It may interpolate geometry between consecutive valid projection samples for smoothness, but interpolation cannot alter numeric values or test evidence. It must not extrapolate past the declared visual horizon.

The render transform obeys a center-lock invariant:

1. Define a fixed vehicle anchor at the Canvas center.
2. Integrate the selected valid sensor speed and steering over mapped monotonic time into a relative projected pose.
3. Draw the grid, projection origin, sensor trail, and world-relative marks using the inverse projected translation.
4. Rotate the vehicle around the fixed anchor for projected heading; in heading-locked mode, apply the inverse rotation to the background instead.
5. Draw the actuation-derived predicted path as a dashed layer from the same center without using it to move the measured background.
6. Never modify the vehicle anchor during resize, zoom, source changes, or motion.

Only a declared sensor projection source moves the default background. Command/keyboard intent cannot substitute for missing sensor feedback. A separate explicitly selected `Actuation projection` diagnostic mode may animate from commands, but it is visibly labelled and never described as measured response.

When data ages out:

1. Stop interpolation for the affected property.
2. Freeze the last valid geometry.
3. Fade/hatch it and show its age and state.
4. Remove dependent ICR/path geometry.
5. Preserve actuation-versus-sensor distinction and continue rendering whichever side remains valid.

Model projection has its own epoch and reset boundary. Browser tab suspension, a large animation `dt`, source change, adapter epoch change, replay seek, or stream resnapshot starts or restores a deterministic projection segment rather than jumping the vehicle. The UI provides Reset Projection without resetting CAN state. Projected distance/heading are never labelled observed unless a future CAN pose source supplies them directly.

## 42. Visual preview verification

Golden snapshot and behavior tests cover straight, left, right, reverse, neutral, braking, indicators, ESTOP, command-feedback disagreement, source fallback, stale/missing/corrupt inputs, replay/synthetic provenance, adapter epoch change, resize/high-DPI behavior, hidden-tab resume, and reduced motion. They also prove that the vehicle anchor remains fixed while the background translates, that heading rotates about that anchor, and that command-only data cannot move a sensor-driven background.

Numeric checks prove steering sign, unit conversion, curvature, ICR side, turn radius, and projected path against the shared E-Trike kinematics vectors. A visual test must also prove that invalid/out-of-range data is disclosed rather than silently clamped into a healthy-looking vehicle.

## 43. Shared API request logic

1. React, LLM tools, or a thin CLI creates the same versioned Pydantic request.
2. FastAPI authenticates the client capability and records caller identity for audit.
3. The shared application service validates profile, protocol hash, adapter epoch, session revision, ownership, values, and state.
4. A query returns an atomic snapshot; a mutation returns disposition and new session revision, plus job ID when asynchronous.
5. Backend-owned jobs execute timing, waits, assertions, recording, and cleanup independently of client connection lifetime.
6. Every client receives the same structured warnings, errors, evidence references, and final disposition.

No domain decision examines whether the caller is UI, LLM, or CLI. A client timeout does not imply job cancellation; explicit cancel and Stop All remain idempotent.

## 44. Shared session and physical TX logic

A session records caller/run audit identity, capabilities, profile, protocol hash, adapter epoch, revision, leases, jobs, evidence, and expiry. Pure Software sessions may be created unattended. A physical session uses the same flow for all clients:

1. Verify adapter and channel mapping.
2. Grant the session the required supported capabilities.
3. Explicitly enable Bench TX for a finite TTL.
4. Resolve and preview semantic traffic through the normal encoder/validator.
5. Revalidate protocol, adapter epoch, topology, ownership, revision, and TTL immediately before starting.
6. Bind created jobs to session, adapter epoch, source ownership, and finite leases.
7. Disable/cancel on mismatch, reconnect, expiry, Stop All, or session close.

Full LLM API access does not bypass these invariants, just as full React access does not. Client termination cannot own cleanup; backend leases and the test runner do.

## 45. Headless test logic

1. Allocate isolated loopback ports and artifact directory.
2. Start backend in virtual mode and wait on structured readiness.
3. Start frontend and verify protocol hash/stream handshake.
4. Load the declared deterministic fixture and reset the test clock/session state.
5. Run API/WebSocket tests or Playwright interactions.
6. Continuously collect browser console errors, failed requests, backend exceptions, stream gaps, and evidence quality.
7. On failure, capture screenshot, trace, DOM/accessibility snapshot, backend state, and relevant CAN window.
8. Run Stop All, close sessions, stop child processes, and detect leaked jobs/ports.
9. Return structured Pass, Fail, or Inconclusive with artifact paths.

Headless tests never depend on fixed startup sleeps, pixel coordinates, or keyboard state as vehicle feedback. They wait on readiness/predicates and verify the same actuation/sensor projections and test verdicts used by the UI.
