# E-Trike Control UI Architecture

**Status:** Product and system design concept (no implementation code)

**Detailed behavior:** See `control-ui-logic.md` for state machines, decisions, timers, failure handling, test execution, and evidence rules.

**Analyzer comparison:** See `can-analyzer-research.md` for the local `python-can`, SavvyCAN, CANgaroo, and CANviz source audit and the connection-loss conclusions.

**Purpose:** Define how the E-Trike CAN bench controller should observe, emulate, stimulate, and verify RT, SYS, MTR, and related hardware while fulfilling the complete requirements in `scope.md`.

**Protocol source of truth:** `protocol/contracts/can_high.yaml` and `protocol/contracts/can_low.yaml`, consumed through generated runtime catalogs, codecs, validators, documentation, and firmware definitions. DBC is an optional export for third-party tools, not an application dependency.

**Implementation dependency:** Control UI synthetic peers, controller/HIL
sessions, and physical Bench TX are late integration stages. They are blocked
until RT/SYS unit enable/disable policy, output permissions, encoder/PID
configuration, production-core host tests, configuration matrices, manifests,
and pure-software safety scenarios pass as defined in
`../docs/rt-sys-feature-configuration-and-test-plan.md` and its dependency-ordered
[`implementation work plan`](../docs/rt-sys-configuration-implementation-work-plan.md).
The Control UI must not be used to compensate for missing firmware configuration
or software tests.

## 1. Product role

The Control UI is a bench-engineering application for the E-Trike. It combines five jobs in one coherent interface:

1. Observe both vehicle CAN buses in real time.
2. Understand the vehicle state without reading raw frames.
3. Generate HMI, keyboard/gamepad, kinematics, direct-actuator, ESTOP, and individual-message stimuli required to exercise ECU code paths.
4. Replace missing ECUs with controlled synthetic peers during bench testing.
5. Diagnose, verify, and record CAN behavior.

All features exist for testing. The application is not an in-vehicle controller, driver interface, autonomous-driving component, or production safety system, and it is not used to drive the E-Trike. “Teleoperation,” “control,” “mode,” “power,” “brake,” and “ESTOP” in this document describe CAN stimuli used on a controlled bench or stationary integration setup to verify that RT, SYS, MTR, and connected units behave as implemented.

This boundary does not remove any requirement from `scope.md`. Full Vehicle, Bench Test, Pure Software, HMI, keyboard/gamepad input, kinematics commands, direct actuator commands, synthetic peers, virtual encoders, ESTOP injection, logging, and sequential message verification all remain required as test capabilities.

## 2. Design priorities

- **State before traffic:** The default screen answers “Is the vehicle safe, connected, and behaving correctly?” before showing a wall of frames.
- **Human values before bytes:** Show `1.4 m/s`, `32°`, `AUTO`, or `Aligned`; raw bytes remain one click away.
- **One CAN definition:** IDs, names, buses, senders, receivers, DLCs, rates, endianness, scaling, enums, and bounds come from generated protocol metadata.
- **Clear provenance:** Every displayed frame identifies its bus, direction, and source: physical, HMI, operator injection, synthetic peer, or virtual bus.
- **Freshness is visible:** A value without age is unsafe and misleading. Every live value has a live, stale, missing, or fault state.
- **Control is deliberate:** Monitoring is always easy; transmission is visually distinct, validated, and acknowledged.
- **Progressive detail:** Dashboard → component → message → signal → raw bytes. Operators see only the detail needed for the current task.
- **Bounded live views:** High-rate data updates the latest visible state without creating an ever-growing browser history.

## 3. Operating profiles

The selected profile is permanently visible in the application header.

| Profile | Physical buses | Synthetic traffic | Operator transmission | Primary use |
|---|---|---|---|---|
| Full Vehicle | High and Low | Off by default | Explicit test stimuli only | Stationary integration test with the complete network/harness |
| Bench Test | Selected bus/ECU | Missing peers only | Enabled for the selected target | Test one or a few physical ECUs in isolation |
| Pure Software | Virtual High and Low | Full virtual vehicle | Virtual bus only | UI development and hardware-free testing |

Changing profile is a controlled transition. Periodic transmissions stop, active controls return to neutral, and the destination is shown for confirmation before the new profile becomes active. Loss of the adapter never silently changes a physical session into a virtual one; the UI reports the loss and offers an explicit move to Pure Software.

## 4. High-level system architecture

```mermaid
flowchart LR
    CH[CANalyst-II\nChannel 0 High] --> HB[CAN transport]
    CL[CANalyst-II\nChannel 1 Low] --> LB[CAN transport]
    VB[Virtual CAN\nHigh and Low] --> RX
    HB --> RX[RX envelope validation]
    LB --> RX
    RX --> DEC[YAML-generated decode and validation]
    DEC --> R[Observation router]
    R --> L[Latest-value state]
    R --> H[Bounded frame history]
    R --> G[Recording and diagnostics]
    L --> WS[Subscription stream]
    H --> WS
    WS --> UI[React Control UI]
    UI --> P[Command policy]
    E[Synthetic peer scheduler] --> P
    P --> A[Bench TX, stimulus lease, deadline and source ownership]
    A --> ENC[YAML-generated encoder]
    ENC --> TX[Physical or virtual TX gate]
    TX --> HB
    TX --> LB
    TX --> VB
    TX --> O[TX observation and audit]
    O --> R
```

The hardware bridge, protocol interpretation, periodic transmission, and presentation are separate responsibilities:

- **Transport layer:** Opens CANalyst-II channels or virtual buses and moves raw frames. It does not decide vehicle behavior.
- **Protocol layer:** Uses YAML-generated codecs and validators to decode and encode messages, including endianness, checksums, counters, overlaps, DLC=0 events, and limits. It does not parse DBC at runtime.
- **Observation router:** Gives physical RX, virtual frames, synthetic peers, HMI commands, and manual injections one observable audit path without feeding observed TX frames back into transmission.
- **Latest-value state:** Keeps the newest value and timing health for every bus/message/signal combination.
- **Bounded history:** Keeps a recent, limited frame window for the monitor. Recording is a separate opt-in durable stream.
- **Synthetic peer scheduler:** Owns periodic bench traffic and rolling counters. It is separate from the transparent transport bridge.
- **Command policy and TX gate:** Validates destination, test session, Bench TX state, stimulus ownership, deadline, source conflicts, and signal values before encoding and transmission.
- **Frontend:** Visualizes state and expresses engineer test intent. It does not independently construct protocol frames.

This separation resolves the apparent conflict between a stateless CAN bridge and stateful emulation: the bridge remains a pipe, while the scheduler and protocol services own the state required for periodic peers, counters, and checksums.

## 4.1 Test-session and transmission state

Operating profile, test transmission state, and stimulus ownership are separate so results remain reproducible:

- **Profile:** Full Vehicle, Bench Test, or Pure Software.
- **Bench TX:** Disabled or Enabled for an explicitly started test session. Connecting hardware never starts transmission.
- **Stimulus lease:** Exclusive, expiring ownership of a resource such as steering, motor, brake, HMI, or a periodic CAN ID.
- **Source ownership:** One permitted producer for each `bus + CAN ID` during a session.

Physical transmission is possible only when the selected profile permits it, the adapter/channel is healthy, Bench TX is Enabled, the test source owns the required lease and CAN ID, the stimulus is current, and YAML-generated protocol validation passes. Explicit negative tests may override selected validation rules only when the test definition names the intended violation.

Bench TX and stimulus leases belong to one test session and adapter epoch. Adapter loss, backend restart, profile change, session stop, or browser control-heartbeat expiry disables active stimulus jobs. Reconnection returns with Bench TX Disabled. These rules protect test integrity and prevent stale traffic; they are not a vehicle safety authorization system.

ESTOP injection is a named test action that bypasses ordinary stimulus ownership so its RT/SYS behavior can be verified. It is labeled **Inject ESTOP Frame** and is not presented as the physical emergency stop for the bench.

## 4.2 Backend-owned stimulus scheduler

The browser expresses test intent; it does not own CAN timing. A dedicated backend worker owns periodic transmission, rolling counters, checksum calculation, deadlines, and jitter measurements.

Interactive keyboard/gamepad tests have a short validity deadline and renew an exclusive stimulus lease. If focus, controller, WebSocket, or lease renewal is lost, the backend stops that stimulus or applies the test definition’s declared end sequence. This avoids an uncontrolled stale test input and makes timeout behavior measurable. The desktop stack is soft real-time; RT and SYS behavior under missed or late stimuli is part of the result being tested.

## 4.3 Local control security

The control API binds to loopback by default. Desktop packaging uses a per-session capability token delivered directly to the local UI, validates WebSocket origin, rejects cross-site requests, and keeps test-session/TX ownership server-side. Remote multi-user operation is not required for this bench tool.

## 4.4 CANalyst-II communication architecture

The existing debug-tool CANalyst-II implementation is a characterization and setup baseline, not the new low-level driver. The preferred transport is the maintained `python-can` CANalyst-II interface (`CANalystIIBus`) configured for both channels. It already wraps the same unofficial reverse-engineered `canalystii`/PyUSB backend, exposes the standard `python-can` message model, provides device receive timestamps, and allows the virtual interface to use the same application-facing API.

Do not accept all `python-can` defaults unchanged. The current backend uses a 20 ms receive poll delay, stores its optional bounded RX queue in `deque(maxlen=...)` where old entries can disappear without an exposed counter, and converts a device-relative 100 μs timestamp into seconds. The Control UI adapter wrapper must configure or patch these behaviors and lock the validated dependency version.

Reuse the debug tool’s proven Windows driver procedure, USB identification, wiring/setup guidance, failure examples, and hardware tests. Reuse its low-level frame behavior only as characterization vectors against `python-can`; do not fork or copy the reverse-engineered USB protocol unless a measured blocker in `python-can` cannot be fixed upstream.

Because the new backend is already Python, it should not preserve the debug tool’s Node → child Python → JSON-lines path. The primary path is:

```text
CANalyst-II
  → WinUSB/libusbK
  → PyUSB + unofficial canalystii backend
  → python-can CANalystIIBus for channels 0 and 1
  → dedicated receive worker / python-can Notifier
  → bounded typed frame queue
  → canonical RX validation/router
  → state, recording, and subscribed WebSocket clients
```

This removes per-frame JSON serialization, stdout parsing, child-process lifecycle ambiguity, and an extra timestamp boundary while retaining a standard adapter abstraction. Adapter isolation may later use a supervised process if soak tests show that the unofficial USB backend can stall or destabilize the FastAPI process, but the IPC must then be a versioned bounded batch protocol with sequence numbers—not unstructured one-line JSON.

### 4.4.1 Transport interface

All physical and virtual adapters implement the same backend contract:

- discover and describe devices;
- open with explicit device identity and channel configuration;
- start RX in listen-only mode where supported;
- return bounded batches of immutable raw frames;
- submit one-shot or scheduled TX requests through the central TX gate;
- report capabilities and unsupported evidence explicitly;
- emit adapter/channel/error/overflow/timestamp-reset events;
- stop, cancel pending TX, drain/cancel queues, and close idempotently.

The CANalyst-II adapter and the Python virtual CAN adapter share this interface, but a failed physical open never silently selects virtual CAN. The profile transition must be explicit.

### 4.4.2 Device discovery and channel mapping

Discovery reports USB VID/PID, device index, serial number when available, driver/backend, firmware/library version, and whether the device is already in use. If stable serial identity is unavailable, the UI makes the selected device index visible and requires review when multiple adapters exist.

The E-Trike default mapping is fixed and generated/configured as:

- CANalyst-II Channel 0 → High Bus, 500 kbit/s;
- CANalyst-II Channel 1 → Low Bus, 500 kbit/s.

The debug-tool script currently defaults these channels in the opposite direction in code, despite its setup documentation describing the required mapping. The new adapter must correct this and test it. A custom mapping is permitted only as an explicit bench configuration, displayed persistently in the header and recording metadata.

Bus identity is taken from the configured physical channel, never guessed from observed CAN IDs. ID-based bus detection may be shown as a wiring consistency check; disagreement creates `CHANNEL MAPPING SUSPECT`, not an automatic remap.

### 4.4.3 Receive handling

Create one `python-can` bus for both CANalyst-II channels and consume it through a dedicated blocking receive worker or `can.Notifier`; do not add a second application polling loop around it. `python-can` may use a receive thread for interfaces without an event-loop file descriptor, so the callback must remain constant-time and immediately enqueue into the bounded router queue.

Use an unbounded `python-can` internal deque only as the short-lived device-batch buffer, then immediately drain into the application’s bounded and instrumented RX queue. Do not set `rx_queue_size` until the backend exposes an observable drop counter. Hardware soak tests must prove that the internal deque remains small; if not, patch/subclass the backend to expose loss rather than accepting silent eviction.

The CANalyst-II USB protocol returns frames grouped by channel. Official `python-can` documentation states that order is preserved within a channel but frames from Channel 0 and Channel 1 may be delivered out of order relative to one another; the hardware timestamp is the correct cross-channel timing evidence. Therefore the application maintains per-channel sequence plus hardware timestamp. It does not treat backend ingestion order as vehicle-wide CAN order.

Required behavior:

- preserve standard/extended, data/remote, channel, DLC, and exactly `data[0:DLC]` rather than padded trailing bytes;
- preserve the CANalyst-II device receive timestamp at its 100 μs resolution, unwrap/reset-detect it, and map it into the session timebase while retaining the raw device value;
- retain per-channel order and assign a backend sequence for deterministic merging;
- order each channel by its preserved order and use hardware timestamp for cross-channel analysis;
- bound RX queues and emit an overflow event with lost-count evidence;
- keep High and Low statistics independent;
- perform YAML decoding, integrity validation, recording, and UI publication downstream—not in the USB poll loop.

Application code must not adapt to a 50 ms sleep while 20 ms messages or control deadlines exist. The wrapper makes the CANalyst-II backend poll delay configurable and starts with a measured 1–2 ms candidate rather than its 20 ms default; CPU usage, USB errors, batch size, and receive latency decide the final value. Receive timeout and observed batch delay are measured. A custom asynchronous libusb path is considered only after profiling proves the tuned standard backend misses the declared workload; libusb supports asynchronous bulk I/O, but bypassing `python-can` would add substantial driver and recovery complexity.

### 4.4.4 Transmit handling

Only the central backend TX gate can submit to the adapter. The adapter receives already-authorized encoded frames with command ID, lease ID, destination channel, deadline, requested send time, and source owner.

The adapter distinguishes these results:

- accepted by command policy;
- queued for adapter;
- submitted to USB library;
- library reported failure;
- expired before submission;
- canceled by Bench TX disable, test stop, or profile change;
- optionally observed through loopback/feedback if the hardware exposes reliable confirmation.

A normal CANalyst-II `send()` return or lack of exception is not proof that another ECU received the frame. UI acknowledgments use the precise state `submitted`, not `delivered`.

Periodic tasks are owned by the backend scheduler, not by arbitrary raw adapter commands. Each period re-encodes from current semantic values so rolling counters and checksums advance correctly. Missed deadlines are not caught up by bursting multiple stale frames in a loop; the scheduler records missed periods, skips stale instances, and schedules the next future deadline.

### 4.4.5 Adapter health and capability honesty

The adapter publishes a capability record stating whether it can provide hardware timestamps, TX echo, listen-only mode, CAN error frames, TEC/REC, bus load, bus-off, hardware overflow, and channel reset. Unsupported values are `Unknown`, never synthetic zeroes. In particular, the debug bridge’s placeholder `load_pct = 0`, `tec = 0`, and `rec = 0` must not be interpreted as healthy evidence.

Health states are based on available evidence:

- `Absent`: USB device not present;
- `Opening`: driver/device initialization in progress;
- `Open`: USB and both configured channels initialized;
- `Active`: valid frames recently observed on that channel;
- `Quiet`: channel open but no recent frames;
- `Degraded`: overflow, repeated USB error, excessive batch delay, or supported controller warning;
- `BusOff`: only when the adapter can positively report it;
- `Recovering`: reopened but awaiting stable valid traffic;
- `Closed`: intentionally stopped.

Silence is not adapter disconnection. Channel traffic freshness, expected message freshness, and USB health remain separate.

### 4.4.6 Failure and reconnection sequence

On adapter exception, device removal, worker death, or supported bus-off evidence, the backend first disables Bench TX, ends stimulus leases, cancels periodic physical jobs, and marks affected values stale. It then closes the transport and begins bounded exponential reconnect attempts with jitter.

Reconnect never restores Bench TX, leases, periodic TX, or prior command state. After reopening, the adapter clears/drains stale hardware buffers as a best effort, starts in receive-only behavior, creates a new adapter epoch, resets timestamp mapping, and observes a stability window. Only then does it become `Recovered`; the operator must explicitly enable Bench TX and restart transmission.

After fast retries are exhausted, slow discovery continues indefinitely while the application is open. Retry count, next attempt time, and last error remain visible. A user can cancel retries or select another adapter.

### 4.4.7 Internal communication messages

Even inside one Python process, transport boundaries use explicit typed records rather than loose dictionaries:

- `RawFrameBatch` with adapter epoch, channel, timestamp quality, first/last sequence, and frames;
- `TransportEvent` with severity, channel, evidence, and monotonic timestamp;
- `TxRequest` with correlation, ownership, deadline, and encoded frame;
- `TxDisposition` with the precise submission state and timing;
- `AdapterStatus` with identity, configuration, capabilities, queue metrics, and per-channel state.

These records are versioned at any process or WebSocket boundary. Unknown message versions are rejected clearly rather than partially interpreted.

## 4.5 Runtime and concurrency model

Run exactly one backend hardware-owner process. Do not start FastAPI/Uvicorn with multiple worker processes: each process would have separate in-memory Bench TX, leases, subscriptions, and adapter state and could contend for the same USB device. FastAPI’s own WebSocket guidance notes that an in-memory connection manager is single-process state.

Within that process:

- the `python-can` receive thread owns blocking adapter receive;
- its callback copies/enqueues only and never decodes, logs, or broadcasts;
- one router task drains RX batches and performs generated decode/validation/state updates;
- the recording worker receives its own bounded queue and never performs disk I/O on the router or ASGI event loop;
- one scheduler worker owns all periodic deadlines, counters, checksums, leases, and TX submission;
- the ASGI event loop owns HTTP/WebSocket work and reads immutable snapshots/events from backend services;
- per-client sender tasks isolate slow WebSocket clients.

Mutable operational state has one owner service and is changed through serialized commands. Threads never directly mutate React-facing dictionaries. Shutdown order is: disable Bench TX → end stimulus leases → stop scheduler → stop RX notifier → drain/close recording → close CAN bus → close clients.

This is a local single-machine tool, so Redis, Kafka, and a distributed event bus add failure modes without benefit. If hardware isolation later requires a second process, only the adapter boundary moves; one supervisor remains the authority for Bench TX, leases, and routing.

## 5. Canonical data presented to the UI

Every live frame should arrive with enough context to explain what it means:

- session-relative timestamp and receive age;
- High or Low bus;
- CAN ID and message name;
- DLC and raw payload;
- RX or TX direction;
- source: physical, HMI, manual injection, synthetic peer, or virtual;
- sender and receivers from the CAN dictionary;
- decoded signal values with units and enum labels;
- expected cycle time and observed frequency;
- validation warnings such as wrong DLC, decode failure, stale counter, or checksum failure.

The UI should keep raw observation separate from decoded interpretation. A raw frame remains viewable even if decoding fails, and the failure is shown rather than replacing the payload with guessed values.

## 5.1 Real-time data contract

Real-time behavior is an end-to-end contract, not simply a fast WebSocket. Each stage must preserve timing evidence so the UI can distinguish a quiet vehicle from a stalled application.

Every streamed update carries:

- backend session ID and monotonically increasing sequence number;
- frame receive timestamp from the best available monotonic clock;
- backend publish timestamp on the same mapped backend timebase;
- bus, direction, and source;
- protocol hash used for decoding;
- validation result and warnings;
- stream batch sequence and server heartbeat timestamp.

Backend and browser monotonic clocks have different origins. A ping/pong exchange estimates browser/backend clock offset, round-trip time, and uncertainty; timestamps are never directly subtracted across clocks without that mapping. The frontend records its own arrival and render times. This provides four separately visible measurements:

1. **Frame age:** now minus CAN receive time.
2. **Transport delay estimate:** mapped browser arrival minus backend publish time, reported with uncertainty.
3. **Render delay:** visual commit minus browser arrival time.
4. **End-to-end visual age:** now minus CAN receive time for the value currently on screen.

The final measurement is the important operator truth. A WebSocket can be connected while the screen is seconds behind, so “connected” must never be inferred from the socket state alone.

The backend provides two complementary subscription types:

- **Raw monitor subscription:** bounded batches of accepted frames only while a client explicitly opens the chronological monitor or a diagnostic capture view.
- **Latest-state subscription:** coalesced by `bus + CAN ID`, optionally filtered to visible systems/messages, carrying the newest frame plus skipped-update counts.

Recording is an internal router-to-storage path and never depends on delivering raw frames to a browser. Critical safety, connection, and corruption transitions use a small always-on event subscription.

Coalescing may discard intermediate visual states but must never conceal that it happened. Each update reports the number of frames seen, coalesced, dropped, or invalid since the previous update. Commands, recordings, and corruption checks operate before UI coalescing.

## 5.2 Visual update strategy

The browser should receive live data continuously but repaint at a controlled cadence suitable for human perception and device performance. Critical state changes such as ESTOP, connection loss, bus-off, or a new fault bypass ordinary visual batching and render immediately.

Different visuals use different policies:

| Visual | Data policy | Presentation policy |
|---|---|---|
| Safety/mode state | Every state transition | Immediate render |
| ECU and bus health | Latest state plus deadlines | Immediate on degradation; periodic age refresh |
| Numeric dashboard values | Newest valid sample | Frame-aligned/coalesced repaint |
| Gauges | Newest valid sample | No long easing animation; animation must not add visible lag |
| Sparklines | Time-window downsample | Preserve peaks/min/max, not only averages |
| Latest CAN table | One row per bus/ID | Update in place and briefly mark changed cells |
| Chronological monitor | Bounded raw batches | Virtualized rows; pause rendering independently of capture |

The UI runs an independent freshness clock. Even if no new messages arrive, ages continue increasing and values transition from Live to Late to Missing. Loss detection must therefore not depend on receiving a final “disconnected” event.

## 5.3 Backpressure and overload visibility

Every queue between adapter, decoder, router, WebSocket, and browser has a fixed bound and exposes depth, high-water mark, dropped count, and oldest-item age. The system may shed presentation work to remain responsive, but it must announce degraded fidelity.

The header displays a `LIVE`, `DELAYED`, or `DROPPING` stream-quality badge derived from end-to-end visual age and queue metrics. A delayed UI must never continue displaying a green connection indicator as though its values were current.

Recording has a stricter contract than visual presentation. If lossless recording cannot keep up, it stops or marks the recording incomplete; it does not silently omit frames.

## 5.4 Backend-to-UI communication protocol

REST handles configuration, snapshots, recordings, dictionary queries, Bench TX/stimulus-lease operations, and deliberate commands. WebSocket carries subscribed live state and events. Command success is returned by REST/WebSocket correlation acknowledgment and is never inferred from seeing a similar CAN frame later.

WebSocket startup follows a defined sequence:

1. authenticate the local session capability;
2. exchange protocol semantic hash and stream protocol version;
3. establish clock-offset/RTT estimate;
4. request subscriptions and receive their accepted limits;
5. receive an atomic initial snapshot with snapshot sequence;
6. apply deltas strictly after that sequence;
7. detect gaps from batch sequence numbers.

Without an atomic snapshot boundary, frames arriving during initial page load can be lost or applied out of order. If a gap occurs, the UI marks affected subscriptions degraded and requests a fresh snapshot; it does not pretend that later deltas repaired missing state.

The always-on event subscription carries stream heartbeat, adapter/channel transition, ECU liveness transition, corruption transition, Bench TX/lease change, source conflict, recording failure, and ESTOP-stimulus disposition. Latest-state and raw-monitor subscriptions can be added or removed without reconnecting the socket.

Each client has independent bounded queues. A slow diagnostic browser cannot delay adapter RX, command scheduling, recording, or other clients. The server first coalesces latest state, then drops old raw-monitor batches with explicit gap counts, and finally disconnects persistently slow clients with a reason.

Use batched JSON for control, status, events, snapshots, and latest-state updates initially. It is inspectable and sufficient after coalescing. Raw chronological frames are batched rather than sent as one WebSocket message per CAN frame. A binary batch format such as MessagePack/CBOR is adopted only if benchmarks show JSON parsing or bandwidth violates the workload budget; the versioned message envelope and golden stream fixtures make that change possible without redesign. Compression is off by default for tiny high-frequency batches because CPU cost and added buffering can exceed the byte savings on localhost.

## 5.5 Data handling and state ownership

The backend is authoritative for adapter state, timestamps, frame validation, latest-good values, liveness, integrity counters, Bench TX state, stimulus leases, source ownership, periodic jobs, and recording integrity. The frontend may cache and format these values but cannot create a competing operational state machine.

Data is separated into four stores:

- **Raw observation:** immutable received/transmitted frame envelope and transport evidence.
- **Validation projection:** decoded values, rule results, and protocol semantic hash; reproducible from raw data.
- **Latest operational state:** bounded newest valid/invalid observations and freshness deadlines keyed by bus/message/signal.
- **Recording:** opt-in durable raw observations, transport events, configuration, adapter epoch, and protocol/source hashes.

On protocol metadata change, raw recordings remain valid evidence and can be re-decoded. Derived projections are treated as disposable. On reconnect or adapter epoch change, old latest values are retained only as historical last-known values; they never become fresh in the new epoch.

## 5.6 Configuration handling

Configuration has typed schema, defaults, validation, and provenance. Safety-relevant runtime configuration—channel mapping, bitrate, physical profile, interlock requirement, rate bounds, command-loss behavior, and adapter selection—is shown in the session review and stored with recordings.

Environment variables may provide development defaults, but the application does not silently accept contradictory channel mappings or invalid rates. YAML protocol values remain authoritative unless the YAML explicitly marks a setting bench-configurable. Configuration changes that affect transport or transmission require Bench TX Disabled, a controlled restart, and an audit event.

## 6. Application shell and navigation

The desktop layout has a persistent status header, a compact left navigation rail, and one main workspace.

### 6.1 Persistent status header

The header remains visible on every screen and shows:

- active profile: Full Vehicle, Bench Test, or Pure Software;
- USB adapter state;
- High Bus and Low Bus activity independently;
- physical/virtual destination indicator;
- vehicle power request and confirmed vehicle state;
- requested and confirmed vehicle mode;
- ESTOP state;
- recording state;
- prominent ESTOP action.

Requested state and observed state must not be collapsed into one label. For example, after selecting AUTO, the header can show `Requested: AUTO` and `Vehicle: MANUAL` until feedback confirms the transition.

### 6.2 Primary workspaces

1. **Overview** — vehicle state and immediate health.
2. **Network** — ECU topology, buses, and node liveness.
3. **Live CAN** — real-time traffic and decoded signals.
4. **Control** — HMI, teleoperation, and direct actuator control.
5. **Bench** — synthetic peers and isolated ECU setup.
6. **CAN Dictionary** — protocol reference derived from YAML.
7. **Diagnostics** — faults, verification sequences, and recordings.

The CAN Dictionary is a reference workspace; Live CAN is an observation workspace. They may share visual components, but their purpose and default density are different.

## 7. Overview workspace

The Overview should be understandable from several feet away and should not resemble a raw debug terminal.

### 7.1 Safety and mode strip

The top row communicates, in order of importance:

- ESTOP clear/active;
- vehicle OFF/ON request and confirmed power state;
- MANUAL/AUTO/PURE SIM request and confirmed mode;
- selected control path: none, kinematics, or direct actuator;
- overall CAN health.

Red is reserved for active hazards and failures. Amber means stale, degraded, or awaiting confirmation. Green means observed healthy state, not merely a button that was clicked.

### 7.2 Vehicle status cards

Show a small set of operational values rather than every signal:

- requested speed versus measured speed;
- requested steering/yaw versus measured steering angle;
- brake request versus brake status/pressure;
- gear and direction;
- obstacle distance or relevant safety input;
- motor and actuator readiness;
- current fault count.

Each value includes its unit and freshness. Clicking a card opens its contributing CAN messages and signals.

### 7.3 Command/feedback pairs

Use paired rows to make control-loop behavior obvious:

| System | Command | Feedback | Difference | Health |
|---|---|---|---|---|
| Drive | `HOST_DRIVE_CMD` or motor request | `MTR_MOTOR_FBK` | speed error | Live/Stale/Fault |
| Steering | yaw/steer request | `SES_STATUS` | angle error | Live/Stale/Fault |
| Brake | brake request | `SEB_STATUS` | pressure/stroke error | Live/Stale/Fault |

This retains a useful pattern from the debug tool while presenting engineering values rather than JSON.

## 8. Network workspace

The Network view explains what is physically or virtually participating.

### 8.1 Topology map

Display two horizontal bus lines, High and Low, with the RT gateway bridging their domains. Attach nodes to their actual bus:

- High: Host, RT, HMI/Control UI;
- Low: RT, SYS, PWT/MTR, steering unit, brake unit, and other defined nodes.

Each node has a state:

- **Live:** expected heartbeat or defining status frame is arriving on time;
- **Late:** one or more expected periods missed;
- **Offline:** no qualifying traffic within the offline threshold;
- **Simulated:** supplied by the synthetic-peer engine;
- **Unknown traffic:** frames attributed to the node are present but its heartbeat is absent;
- **Fault:** heartbeat/status is present with health faults.

The node tooltip or detail drawer shows last seen time, observed rate, expected rate, source, relevant IDs, bus, and active fault flags.

### 8.2 Heartbeat rules

Liveness is message-aware rather than based on any traffic:

- Host: High `0x7FC`;
- RT: High and Low `0x7FD`, evaluated independently;
- SYS: Low `0x7FE`;
- PWT: Low `0x7FB`;
- other nodes: their defined heartbeat or primary periodic status message.

Freshness thresholds should derive from expected message periods. A practical display policy is Live up to roughly two expected cycles, Late after missed cycles, and Offline after a larger bounded timeout. The exact thresholds remain configurable per message.

### 8.3 Bus health

For each bus show adapter/channel, configured bitrate, active/offline state, RX/TX frame rate, unknown IDs, decode errors, dropped frames, and last transport error. High and Low must never be combined into a single “CAN connected” lamp.

### 8.4 Layered connection-loss detection

Connection state is evaluated independently at five layers:

| Layer | Evidence | Failure meaning |
|---|---|---|
| USB adapter | Device/driver open state and adapter errors | CANalyst-II is unavailable or failed |
| CAN channel | Channel open/configuration state and adapter-supported error evidence | High or Low controller is unavailable or degraded |
| Backend stream | Server heartbeat and WebSocket batch sequence | Browser has lost or stalled its backend connection |
| ECU/node | YAML-defined heartbeat/status deadline and advancing alive counter | A specific node is absent or frozen |
| Signal/message | YAML-defined cycle time, last valid frame, and validation state | A displayed value is stale, missing, or corrupt |

These states must not be collapsed. For example, the USB adapter can be connected while Low Bus is silent, or RT can be alive on High Bus but missing on Low Bus.

Channel-open, electrical activity, expected protocol traffic, and ECU liveness are separate. A correctly opened but intentionally quiet bus is not called disconnected. If CANalyst-II cannot expose bus-off, error-passive, or hardware-overflow evidence, that health field is shown as `Unknown`; it is never inferred as healthy from an open USB handle.

The backend sends a lightweight stream heartbeat even when there is no CAN traffic. The browser declares the backend stream lost after a bounded missed-heartbeat interval, immediately marks all physical live values stale, disables motion controls, and preserves last values with their ages for diagnosis.

Reconnect behavior is explicit:

- reconnect uses exponential backoff with a visible attempt count;
- the UI never restores physical transmission or active teleoperation automatically;
- a new backend session ID resets sequence expectations and is shown as a session change;
- message freshness is rebuilt from new traffic rather than inherited from before the outage;
- gaps in stream or frame sequence are counted and shown;
- a recovered node passes through `Recovering` until valid, advancing frames have been observed for a small stability window.

Node liveness uses advancing alive counters when they exist, not frame arrival alone. Receiving the same heartbeat payload repeatedly may indicate a frozen producer or replayed/corrupted traffic and must be reported separately from healthy liveness. For physical traffic, the displayed sender is the **YAML-defined expected sender**, not proof of which physical ECU transmitted it; ordinary CAN frames contain no sender identity.

## 9. Live CAN workspace

The Live CAN view answers “What is happening now?” It should support two complementary presentations.

### 9.1 Latest-by-message view

This is the default and most readable mode. Each bus/ID appears once and updates in place. Rows show:

- activity/freshness indicator;
- bus;
- CAN ID and message name;
- sender;
- direction and source;
- observed rate versus expected rate;
- raw bytes;
- compact decoded values;
- age since last frame.

Updating in place prevents high-frequency messages from pushing useful information off screen. A changed value briefly highlights without flashing the entire row.

### 9.2 Chronological stream view

This view shows individual frames for timing and sequence diagnosis. It has pause, resume, clear-visible-history, and bounded-history behavior. Pausing freezes rendering, not capture or recording.

### 9.3 Filters

Provide fast filters for:

- High, Low, or both buses;
- CAN ID/name;
- sender/receiver ECU;
- signal name;
- RX/TX direction;
- physical/synthetic/HMI/manual/virtual source;
- command, status, heartbeat, diagnostic, or event category;
- known, unknown, warning, or fault frames;
- changed-only values.

Search should match CAN ID, message name, ECU, signal label/key, comment, and decoded enum text, following the useful behavior of the existing debug tool.

### 9.4 Message detail drawer

Selecting a live message opens one detail drawer with:

1. identity: ID, name, bus, sender → receivers;
2. contract: DLC, expected period, byte order, source authority;
3. live health: last seen, observed frequency, direction, and freshness;
4. decoded signal table: current value, enum label, unit, raw value, min/max, and state;
5. raw frame: timestamp and hexadecimal bytes;
6. byte/bit map: the same color-linked concept used by the debug tool;
7. recent mini-history for selected numeric signals;
8. warnings: wrong DLC, bounds, checksum, counter, multiplexing, or unknown bits.

The raw decoded JSON used by the debug tool may remain available under an “Advanced” disclosure, but it is not the primary presentation.

## 10. CAN Dictionary workspace

The dictionary explains what messages *mean*, independent of whether the vehicle is connected.

### 10.1 Catalog browsing

Use searchable message cards grouped or filtered by High/Low bus. Each card header contains:

- CAN ID and message name;
- bus;
- sender → receiver route;
- DLC;
- cycle/event rate;
- Motorola/Intel byte order;
- signal count;
- generated transmission-policy classification;
- generated-from-YAML provenance.

Do not silently merge undocumented fallback definitions into the authoritative catalog. If a fallback is ever needed, label it visibly as non-authoritative.

### 10.2 Byte and bit layout

Reuse the debug tool’s byte-grid idea because it makes overlapping and mixed-endian layouts understandable:

- render B0 through B7 as bit cells;
- give each signal a consistent color in both the grid and table;
- hover/focus a bit to show byte.bit address, owning signal, width, type, scale, unit, enum meaning, and multiplexing condition;
- distinguish unused, reserved, checksum, rolling-counter, and overlapping/multiplexed bits;
- explain DLC=0 messages as event frames where the ID itself is the event;
- label bit numbering and endianness explicitly to avoid a misleading linear map.

The new view must correct a limitation in simplistic bit grids: Intel and Motorola fields cannot always be visualized by merely coloring consecutive linear bits. The display must use the generated canonical bit mapping.

### 10.3 Signal table

The table should include:

| Column | Meaning |
|---|---|
| Signal | Protocol signal name and friendly label |
| Start | Canonical start bit and byte.bit form |
| Length | Width in bits |
| Type | Signed, unsigned, boolean, or enum |
| Byte order | Intel or Motorola |
| Scale/offset | Raw-to-physical conversion |
| Min/Max | Defined and safety-constrained bounds |
| Unit | Engineering unit |
| Values | Enum/flag meanings |
| Multiplexing | When the signal is active or overlaps another field |
| Automation | Auto checksum/counter/read-only/forced-enable behavior |
| Description | Protocol comment and operational meaning |

Selecting a signal highlights its bits; selecting bits highlights the matching table row. A “Show live values” toggle can overlay the newest value and age without turning the dictionary into the traffic monitor.

## 11. Control workspace

Control is divided into HMI, kinematics control, and direct actuator control. Only one driving control modality may own active motion commands at a time.

### 11.1 HMI panel

The UI acts as the HMI node and broadcasts:

- `0x111 HMI_MODE_REQ` at 1 Hz with requested MANUAL, AUTO, or PURE SIM mode and its rolling alive counter;
- `0x112 HMI_PWR_REQ` at 1 Hz with requested OFF/ON state and its rolling alive counter.

The panel shows request, transmission status, last transmitted counter, and independently observed SYS/RT state. A request is never displayed as confirmed merely because the frame was sent.

ESTOP testing is separate from routine HMI controls and transmits the DLC=0 `0x001 SAFETY_ESTOP` event. **Inject ESTOP Frame** remains reachable from every test workspace when Bench TX is enabled. The UI records submission, RT/SYS observations, propagation, latch, and recovery behavior. It is a protocol test action, not the bench’s physical emergency control.

### 11.2 Kinematics control

Kinematics mode targets the High bus and mimics Host intent through `0x300 HOST_DRIVE_CMD`. The RT remains responsible for kinematics and downstream safety behavior.

The interface shows speed, yaw/steering intent, gear, input source, keyboard/gamepad connection, command age, transmit rate, and RT-derived actuator feedback. Releasing input or losing focus stops browser lease renewal. Losing the controller, WebSocket, or renewal deadline causes the backend watchdog to apply the YAML-defined command-loss behavior; it does not rely on the disconnected browser to send a final command.

### 11.3 Direct actuator control

Direct mode targets selected Low-bus actuator messages for isolated testing. Steering, brake, and motor controls are separate cards with:

- target ECU and bus;
- enable/readiness prerequisites;
- engineering-value input;
- allowed range and current value;
- generated raw preview;
- checksum/counter status;
- explicit start/stop command stream;
- matching feedback and error;
- last command acknowledgment or rejection reason.

Mandatory enable flags are locked to their required values and explained, not exposed as casual toggles. Counters and checksums are visibly marked “automatic.” Safety bounds come from the YAML constants and cannot be bypassed by ordinary UI input.

### 11.4 Keyboard and gamepad behavior

Controls are inactive until the workspace has focus and the operator explicitly enables control. Show a small always-visible input legend and live input positions. Dedicated Hard Brake and ESTOP bindings are visually distinct. ESTOP must not depend on ownership of a steering, motor, or brake control session.

## 12. Bench workspace

The Bench workspace configures which physical ECU is under test and which absent peers are synthesized.

### 12.1 Bench setup

The setup flow asks for:

1. physical target ECU(s);
2. connected bus/channel;
3. peers known to be present;
4. missing peers to emulate;
5. control path: kinematics or direct actuator;
6. review of every periodic frame that will be transmitted.

A topology preview distinguishes physical nodes from synthetic nodes before activation.

### 12.2 Synthetic peer matrix

Show each scheduled message as a row:

| Enabled | Synthetic node | Message | Bus | Period | Key startup values | TX health |
|---|---|---|---|---|---|---|
| Yes | EPS-C | `0x201 SES_STATUS` | Low | 100 ms | angle 0, aligned | On time |
| Yes | SEB | `0x721 SEB_STATUS` | Low | 100 ms | safe/default | On time |
| Yes | MTR | `0x206 MTR_MOTOR_FBK` | Low | 50 ms | speed 0 | On time |
| Yes | SYS | `0x7FE SYS_HEARTBEAT` | Low | 100 ms | healthy/default | On time |
| Yes | RT | `0x7FD RT_HEARTBEAT` | Both | 500 ms | per-bus counters | On time |
| Yes | Host | `0x300 HOST_DRIVE_CMD` | High | 100 ms | neutral | On time |
| Yes | Host | `0x7FC HOST_HEARTBEAT` | High | 500 ms | healthy/default | On time |

The system prevents duplicate ownership with an initial listen-before-speak window and a `bus + CAN ID` source-ownership table. Synthetic output starts only after required physical IDs remain absent for their YAML-defined detection window. If conflicting physical traffic appears, synthetic transmission for that ID stops immediately, the affected test becomes Inconclusive or Failed according to its rule, and a source-conflict event requires engineer review. RT heartbeat instances on High and Low remain distinct and use independent counters.

### 12.3 Virtual encoders

Virtual wheel/motor feedback is presented as a named bench function, not an unexplained raw injection. Show simulated speed, source model/manual input, output rate, and the safety monitor it is intended to satisfy.

## 13. CAN injection workflow

Generic injection is an engineering tool, not the main driving interface.

1. Choose High or Low bus.
2. Select a message whose generated transmission policy permits the current profile and purpose.
3. Enter signal values using controls appropriate to type: toggle, enum selection, or bounded numeric input with units.
4. Review sender, receivers, DLC, destination, encoded bytes, forced fields, automatic counter/checksum, and warnings.
5. Choose one-shot or bounded periodic transmission.
6. Confirm hazardous actions such as ESTOP separately.
7. Show an acknowledgment with accepted/rejected state and the actual transmission result.

The injector should preserve the useful debug-tool concepts of templates, raw preview, periodic interval/count, stop controls, and command acknowledgments. It should add clearer physical/virtual destination labeling and prevent arbitrary rates from violating message-specific minimum periods.

Transmission permission is not a single `injectable` boolean and is never inferred from sender name. YAML policy classifies messages as monitor-only, HMI-periodic, synthetic-peer, manual-bench, kinematics-control, direct-actuator, or safety-event, with allowed profiles, buses, rate bounds, Bench TX requirements, lease resource, and automatic fields.

## 14. Diagnostics, message verification, and logging

### 14.1 Diagnostics timeline

Diagnostic and transport events appear in a focused timeline separate from the high-volume frame stream:

- ECU diagnostic reports and fault flags;
- ESTOP and safety-state transitions;
- heartbeat late/offline/recovered events;
- bus-off, adapter disconnect, overflow, or decode errors;
- rejected command reasons;
- recording start/stop/incomplete events.

Each entry links to its raw frame and decoded signals.

High-frequency faults are represented as condition episodes, not one timeline row per failed frame. The first failure is emitted immediately; repeated observations update exact counters and bounded samples; periodic summary records report count/rate; recovery emits the final duration and count. Aggregation is keyed per code and bounded scope, so one noisy CAN message cannot suppress unrelated errors. Recovery hysteresis prevents valid/invalid alternation from flooding the timeline. Raw recordings retain frame-level evidence independently from operational logging.

### 14.1.1 Alignment with RT, SYS, MTR, and connected components

The backend must distinguish what it observes directly from what firmware knows internally. Existing `ESP_LOG*` output from RT/SYS is normally UART/console text; it is not transported in ordinary CAN frames. A CAN-only Control UI therefore cannot claim that RT or SYS emitted an internal log entry. It can report the corresponding externally visible CAN evidence, or `Unknown` when firmware does not expose the internal state.

| Component | Directly observable by the Control UI | Episode/aggregation rule | Important limitation |
|---|---|---|---|
| Control UI backend and CANalyst-II | adapter calls, worker health, backend queues, raw RX/TX, decoder results | Backend owns exact counters and condition transitions | CANalyst-II may not expose TEC/REC, bus-off, or hardware overflow; unsupported remains `Unknown` |
| RT | per-bus `0x7FD` heartbeat/counter/health, `0x210 RT_STATE_RPT`, `0x310/0x311` diagnostics, RT-originated and forwarded traffic | Heartbeat health and task/fault fields are level states; raise on state/bit transition, summarize repeats, recover after valid advancing reports | Internal `ESP_LOG`, low-level retry counts, and reset reason are not available over CAN unless separately exported |
| SYS | `0x7FE SYS_HEARTBEAT`, `0x600 SYS_DIAG_RPT`, `0x011 SYS_SAFETY_STS`, outputs and actuator requests | Treat heartbeat, brake fault, ESTOP, TEC/REC thresholds, and overflow as separate episodes | SYS task-watchdog failures and NVS boot count/reset reason are serial-only in the current design |
| MTR | `0x206 MTR_MOTOR_FBK` at 50 Hz, including gear/speed and reported flag bits | Maintain one episode per defined bit; never create 50 identical log records per second | The byte named `fault_flags` also carries `STARTUP_READY`, which is status, not a fault; firmware currently also reuses the ADC-fault bit for a DAC-write failure, so the UI must not assert the physical cause |
| EPS-C/SES and SEB | vendor status/error frames, checksum/counter fields, measured feedback | Validate each frame, but create episodes per reported fault/status field and canonical source bus | Reported vendor error bits are ECU reports, not proof of wiring or physical root cause |
| PWT | `0x7FB` and motor telemetry only when the planned implementation actually transmits them | Apply the generated YAML policy after implementation is characterized | Current PWT architecture contains TBD/unimplemented functions; do not show them as supported |

`first_occurrence_time` for an ECU-reported flag means first observed by the adapter, not the time the ECU internally detected it. The event carries `evidence_basis=reported` and both adapter arrival/device time where available.

#### Forwarded frames versus independent instances

RT forwards several Low-bus messages to High, including MTR `0x206` and SYS `0x600`. Those two observations must not become two logical ECU-fault occurrences. YAML must declare an `origin_bus` and route/forwarding metadata. The diagnostic service evaluates ECU-reported flags on the canonical origin observation; the forwarded copy remains visible as transport evidence.

RT `0x7FD` is the opposite case: High- and Low-bus heartbeats are independently generated with separate counters and are never bridged. Their liveness and counter episodes must therefore be keyed by bus and never deduplicated together. Transport counts always remain per physical bus even when a logical diagnostic is deduplicated.

#### Counter semantics

Do not assume every ECU counter is an unbounded exact total:

- RT `RT_RxOverflow` is currently a wider internal counter cast to 8 bits and can wrap.
- SYS packs `rx_overflow` into six bits and saturates it at 63; the current CAN YAML omits this packed field.
- Heartbeat counters wrap modulo 256 and only prove liveness when they advance.
- A discontinuity may be wrap, reboot, missed frames, replay, or corruption; without a boot/session identifier it is not definitive reset evidence.

Generated metadata must declare `counter_kind` (`modulo`, `saturating`, or `monotonic`), width/modulus, reset evidence, and whether a value is an exact total or a lower bound. The UI shows `63+` for the saturated SYS overflow value and never derives an exact loss total after saturation. Adapter/backend drop counters remain separate and exact within their own process epoch.

#### Firmware logging compatibility findings

The root architecture already defines the correct first-failure/count/recovery pattern for ordinary RT/SYS CAN TX paths, and the Control UI episode model matches it. The implementation is not uniform, however:

- RT command-stale and task-stall checks can emit on every 10 Hz watchdog poll while active.
- RT CAN-health warning/bus-off checks can emit on every 10 Hz health poll, and several SES fault/limit handlers log on every matching frame.
- Some RT diagnostic/heartbeat TX paths use first failure plus every 100th failure, then recovery.
- SYS ordinary TX uses first failure and recovery, but critical TX failure can log on every failed retry.
- SYS CAN-health output can repeat at 1 Hz; RX overflow logs only its first occurrence and has no firmware recovery record.
- SYS gear mismatch emits approximately every 500 ms while the mismatch persists.

These firmware logs can flood a serial collector, but they do not directly flood the CAN-only backend. If UART logs are later ingested, do not parse English strings into Control UI errors. Add a versioned structured firmware-event envelope or a small debug telemetry protocol, preserve ECU/component identity, and apply per-ECU episode aggregation before merging with backend events.

#### Protocol-source corrections required before generation

The YAML/compiler work must resolve these observed contract gaps before the generated Control UI treats the fields as authoritative:

1. Add the packed SYS `rx_overflow` field in `SYS_DIAG_RPT` byte 2 bits 1–6 with saturating semantics.
2. Define all MTR flag bits identically in High and Low catalogs; High currently documents `STARTUP_READY` while Low does not.
3. Separate MTR status bits from fault bits, and assign distinct diagnostic meaning for DAC-write versus ADC-input failure instead of sharing one bit.
4. Declare canonical `origin_bus`, forwarded routes, and independent-per-bus instances so `0x206`/`0x600` are deduplicated logically while RT `0x7FD` is not.
5. Declare health-bit semantics precisely. SYS code currently sets heartbeat `can_ok` for `TEC < 255`, although its comment says error-passive should not be OK; code, YAML, tests, and generated display must agree.
6. Declare counter wrap/saturation/reset semantics and recovery thresholds in machine-readable fields rather than comments.

Until those items are resolved, the backend exposes the raw value and a `contract_uncertain`/`capability_unknown` diagnostic rather than assigning a stronger interpretation.

### 14.2 Sequential message verification

Provide a guided verification workspace where an engineer selects a CAN message and defines or selects an expected response. Each step displays:

- message and signal values to transmit;
- required prerequisites;
- target bus/ECU;
- expected feedback message and condition;
- timeout;
- observed result;
- Pass, Fail, or Inconclusive with evidence.

Only one verification step is active at a time. Results can be exported as a test report.

### 14.3 Recording

Recording is opt-in and visibly active. A recording stores raw frames with timestamps, bus, direction, and source so it can be decoded again against a known protocol version. The UI shows duration, frame count, dropped-frame status, filename/session label, and storage health. Diagnostic-only logging may run with a lower data volume, but must never pretend to be a lossless full-bus recording.

## 15. Live data presentation rules

### 15.1 Freshness

Every live message and derived value carries one of four states:

- **Live:** arriving within its expected timing window;
- **Late:** expected periodic data has missed cycles;
- **Missing:** no valid frame has been observed or it exceeded the offline threshold;
- **Invalid:** a frame arrived but failed DLC, decode, checksum, counter, or semantic validation.

Display the numeric age (`42 ms ago`) alongside color. Color alone is insufficient.

### 15.2 Values

- Primary value: physical engineering value plus unit.
- Enum: friendly label first, raw value second when expanded.
- Boolean: semantic state such as `Enabled`, `Aligned`, or `Fault`, not just `1/0`.
- Unknown/reserved enum: show `Unknown (raw N)`.
- Unavailable: show `—`, never zero.
- Stale: retain the last value but dim it and show its age; do not silently reset it.
- Invalid: retain raw bytes and show the validation reason.

### 15.3 Update behavior

High-rate frames update the latest state at a display-friendly cadence while capture and control retain their required timing. Numeric values should not animate in ways that hide fast changes. Sparklines are useful for a few selected signals, not every field. The chronological table uses virtualization and a bounded visible window.

### 15.4 Categories

Messages may be categorized for navigation and filtering as Safety, HMI, Drive Command, Actuator Command, Status/Feedback, Heartbeat, Diagnostic, or Event. Categories are presentation metadata; CAN ID and bus remain the unique technical identity.

### 15.5 Corruption and plausibility detection

The Control UI validates every frame against the same normalized wire facts used by RT and SYS. Each message definition selects exactly one codec strategy: generated, named profile, or custom. Ordinary messages use generated codecs; exceptional vendor messages use their selected profile/custom implementation and shared conformance vectors. Validation produces structured flags rather than a single generic decode error. The current `manual-mappings.yaml` mechanism is transitional and must not become the backend plugin architecture.

CAN-layer corruption and application-layer corruption are different. The CAN controller normally rejects frames that fail the wire CRC, so their corrupted payload may never reach the backend. Adapter-reported error frames, error counters, overflow, bus-off, and controller state are transport evidence. Payload checksum/XOR, rolling counter, DLC, range, and plausibility checks are application-protocol evidence. The UI reports them separately and never claims to show wire corruption that the adapter did not expose.

| Check | Detects | UI result |
|---|---|---|
| CAN ID/bus membership | Unknown or wrong-bus message | Unknown/wrong-route warning; raw frame retained |
| DLC | Truncated or oversized payload | Invalid frame; decoded values withheld |
| Bit layout and decode | Malformed or unsupported signal representation | Decode error with affected signal |
| Enum membership | Undefined state value | `Unknown (raw N)` warning |
| Physical bounds | Out-of-range engineering value | Invalid or implausible value with expected range |
| Checksum/CRC/XOR | Payload bit corruption | Corrupt frame; expected/received checksum shown |
| Rolling counter | Duplicate, frozen, skipped, or reordered frames | Sequence discontinuity with expected/received value and likely loss layer |
| Cycle time | Missing, late, burst, or unexpected-rate traffic | Timing warning with expected/observed period |
| Alive counter | Producer frozen despite continuing traffic | Node frozen warning |
| Cross-signal rules | Mutually inconsistent fields or missing enable bits | Semantic validation warning |
| Command/feedback plausibility | Command and response diverge beyond a configured window | System-level plausibility fault |

Invalid frames remain in the chronological monitor and recording with their raw bytes. They do not overwrite the last known-good engineering value. The value card instead shows the last good value, its timestamp and increasing age, and a visible `CORRUPT INPUT` state. Invalid or stale values are tainted and cannot silently participate in derived dashboard calculations; a derived value becomes unavailable or degraded and identifies the bad input.

Counters are tracked independently by bus, CAN ID, expected source definition, and session. This is essential for `0x7FD`, whose High- and Low-bus heartbeat instances have separate counters. Counter validation allows correct modulo wraparound and runs before UI coalescing. A gap is initially classified as a sequence discontinuity because adapter overflow, backend loss, arbitration, sender failure, or actual corruption may produce similar evidence; queue and adapter metrics help determine the likely cause.

Checks that cannot be proven from one frame, such as counter continuity, rate, and command/feedback agreement, run in the backend. The browser receives their structured results and may independently enforce freshness for display, but it does not invent a second protocol validator.

### 15.6 Corruption presentation

Corruption must be visible without overwhelming normal operation:

- the global header shows the number of active corrupt streams;
- affected ECU nodes and message rows receive a red corruption marker;
- the diagnostics timeline records first occurrence, recovery, and accumulated count;
- expanding the frame shows the raw payload, failed rules, expected values, received values, and protocol hash;
- a “valid only” monitor filter helps normal observation, but invalid frames are never discarded by default;
- recovery requires subsequent valid frames, not merely elapsed time.

Warnings are severity-ranked: informational timing variation, degraded/missed cycles, corrupt frame, and safety-relevant plausibility fault. The source YAML should carry severity where the protocol knows it; UI styling must not infer safety criticality from CAN ID alone.

## 16. Test-integrity and interaction boundaries

- Physical transmission is unmistakably labeled and requires Bench TX Enabled for the current test session; connecting the adapter alone never transmits.
- Simulation and physical traffic use different visual source badges.
- A control action always shows its target bus and message.
- HMI mode/power requests transmit only at their defined 1 Hz rate.
- Periodic jobs stop on profile change, disconnect, application shutdown, or explicit Stop.
- Motion commands have backend-enforced freshness timeouts and YAML-defined loss behavior.
- Direct actuator and kinematics control cannot simultaneously own the same control path.
- ESTOP test injection bypasses ordinary stimulus ownership so the event can be tested from any active test workspace.
- Every physical `bus + CAN ID` has at most one active backend source owner.
- Simulation and replay can never reach physical TX unless a separately reviewed hardware-in-the-loop policy explicitly permits a bounded case.
- The UI enforces YAML limits for ordinary positive tests; explicit negative tests can name and record an intentional violation.
- Failed transmission never falls back silently to a different bus or virtual destination.

## 17. Visual language

The application should feel like an automotive engineering instrument rather than a generic administration dashboard.

- Dark, high-contrast base suitable for workshop use.
- High and Low bus have stable, distinct accent colors.
- Physical, synthetic, HMI, manual, and virtual sources use consistent badges.
- Monospace typography is reserved for CAN IDs, timestamps, raw bytes, and bit addresses.
- Engineering values use legible proportional numerals and visible units.
- Dense tables are available in CAN and dictionary workspaces; control screens use larger targets.
- Red is reserved for faults, ESTOP, rejected safety conditions, and destructive implications.
- Responsive behavior prioritizes desktop/laptop use; narrow layouts turn wide tables into detail drawers or stacked signal cards.

## 18. Performance expectations

“Zero-latency feel” means:

- operator controls react immediately in the interface;
- WebSocket updates are batched/coalesced enough to keep the browser responsive;
- latest-by-message state is preferred over rendering every frame;
- hidden workspaces do not perform expensive rendering;
- frame history, logs, and queues have explicit bounds;
- dropped/coalesced UI updates are measured and disclosed;
- control and logging timing are not tied to the browser render loop.

The UI may skip intermediate visual updates, but the backend must not skip required command periods or hide capture loss.

## 18.1 Measurable real-time service levels

The implementation should declare and test service levels under a defined worst-case dual-bus workload. Initial targets should include:

- maximum and percentile CAN-receive-to-browser-arrival latency;
- maximum and percentile CAN-receive-to-visible-render latency;
- time to show adapter, channel, WebSocket, and ECU loss;
- maximum latest-state age while the UI reports `LIVE`;
- zero silent drops, with all coalescing and loss counted;
- command scheduling jitter and missed-period count;
- corruption detection latency;
- UI responsiveness while the chronological monitor and recording are active.

Provisional acceptance thresholds prevent “real-time” from remaining undefined:

- backend stream heartbeat: 250 ms;
- browser stream degraded after 750 ms without a heartbeat and lost after 1500 ms;
- dashboard repaint cadence: up to 60 Hz, normally 20–30 Hz under load;
- maximum visual age while labeled `LIVE`: the greater of 150 ms or two YAML-defined message periods;
- critical state events queued ahead of ordinary telemetry and rendered on the next browser frame;
- control-intent lease: message/profile-defined and always shorter than the receiving ECU watchdog;
- raw monitor queue and WebSocket batches: bounded, with oldest age and loss counters visible;
- recording: zero silent loss; any overflow marks the session incomplete immediately.

These are initial engineering budgets, not hard-real-time guarantees. CANalyst-II measurements under the declared worst-case dual-bus load may tighten or relax them through a reviewed change, with the chosen values stored in configuration and exercised by automated soak tests.

## 18.2 YAML protocol compiler

The application does not use DBC as its internal model. YAML is used as a DBC-like static dictionary, divided by message origin/protocol family with bus instances represented explicitly. A message layout appears once; sender and receivers consume the same normalized definition. The compiler generates metadata for every message and complete codecs only for messages whose selected strategy supports generation. Unsupported vendor algorithms remain explicit named profiles or custom codecs rather than being hidden in application code. DBC may still be exported for CANalyzer, cantools interoperability, or other external tools, but no Control UI behavior depends on it.

Current implementation produces C++ codecs, TypeScript catalogs, stable error definitions, per-message hashes, `codec_manifest.json`, and `change_impact.json`. Python backend codecs, React integration, documentation exports, and complete checksum/counter algorithm generation below are target outputs unless their implementation and tests are present.

The compiler should replace the current debug-tool-specific assumptions in `protocol/generate_can_ts.py` with shared schema validation and deterministic targets:

- Python runtime catalog, encoder, decoder, and validator metadata for FastAPI;
- TypeScript runtime catalog and presentation metadata for React;
- C/C++ constants and codec/validation definitions for firmware;
- golden encode/decode/integrity vectors consumed by all languages;
- Markdown/CSV/optional DBC documentation exports.

The generated Control UI artifact should contain:

- protocol version, normalized semantic hash, and exact-source artifact hash;
- bus, ID, name, DLC, sender, receivers, cycle/event timing, and byte order;
- complete canonical bit mapping for Intel and Motorola signals;
- signal key/name, type, scale, offset, unit, enum values, min/max, and comments;
- constants and safety bounds from the YAML `constants` block;
- heartbeat/alive-counter signal identity and timeout rules;
- checksum/counter algorithm metadata and protected byte ranges;
- multiplexing/overlap conditions;
- mandatory enable values and automatically managed fields;
- message category, diagnostic severity, and structured transmission policy;
- generated lookup indexes for `bus + ID`, node, category, and signal search.

Frontend and backend expose their normalized semantic hash during connection setup. A semantic hash is calculated from canonical parsed content, so whitespace and comment-only edits do not break compatibility; the exact-source hash remains available for traceability. If semantic hashes differ, the UI enters `PROTOCOL MISMATCH`: raw monitoring may continue, but decoded control and physical injection are disabled until artifacts match.

The current YAML contains some checksum, counter, and safety meaning only in comments. Comments are useful to people but insufficient for deterministic validation. Stable repeated behavior may select a small versioned profile implementation; unique behavior selects a custom codec. YAML records the strategy and implementation ID but does not attempt to express arbitrary algorithms or component state machines. The current mapping registry is used only during migration. The target proof is a static definition plus one selected implementation/profile plus language-neutral vectors executed by C++ and Python.

Wire layout, integrity/liveness rules, and system plausibility rules are separate validated YAML sections. This keeps the CAN message format portable while allowing stateful rules such as command-feedback deadlines to share the same source-controlled compiler pipeline.

Generator output is reproducible and never hand-edited. CI runs generation in check mode and fails when generated artifacts drift from YAML.

## 18.3 Debug-tool bridge migration

Reuse is selective and test-driven:

| Debug-tool asset | Reuse | Required improvement |
|---|---|---|
| `CANALYST-II-SETUP.md` | Driver binding, VID/PID, dependency and troubleshooting guidance | Correct channel mapping, pin supported versions, add capability verification |
| `backend/canalystii_bridge.py` | Characterization vectors for DLC, IDs, channels, RX/TX, buffer cleanup, and observed failures | Replace runtime path with `python-can` CANalyst-II; retain tests and measured behavior; no JSON stdout |
| `backend/src/canalyst/bridge.ts` | Reconnect concepts and command correlation | Replace Node child-process wrapper with Python supervisor/adapter service |
| `backend/src/bridge/manager.ts` | Single active transport concept | Explicit profile transitions; no silent physical-to-virtual or CANalyst-to-serial fallback |
| Bridge/unit tests | Fake process/device cases and reconnect scenarios | Add dual-channel order, overflow, epoch, disable-TX-before-reconnect, scheduler jitter, and disconnect-under-TX tests |

The existing implementation should first be preserved behind characterization tests. Migration occurs only after tests capture current device-open, RX conversion, DLC=0, extended-ID, dual-channel, and shutdown behavior and prove equivalent or better behavior through `python-can`. Hardware-in-the-loop tests remain separately marked and never run against a vehicle with Bench TX enabled by default.

Known debug-tool behaviors that must not carry forward:

- Channel 0/1 defaults opposite to the project scope.
- One JSON object and stdout flush for every frame.
- `time.time()` assigned once to an entire receive batch without timestamp-quality metadata.
- Adaptive idle polling that can stretch to 50 ms despite 20 ms protocol periods.
- Placeholder bus load, TEC, and REC values reported as zero.
- Static periodic payloads that do not regenerate counters/checksums.
- Catch-up loops that may burst stale periodic frames after a delay.
- `send()` acknowledgment presented without distinguishing submission from delivery.
- Traffic silence treated as channel inactivity without separating open/quiet/disconnected.
- Automatic fallback between materially different transports without operator confirmation.

## 19. Delivery sequence

Delivery is staged so physical control is added only after the observation and safety foundations are measurable:

1. **Protocol foundation:** YAML schema/compiler, cross-language golden vectors, semantic hashes, checksum/counter rules, and drift checks.
2. **Read-only transport:** dual-bus CANalyst-II/virtual transport, canonical timestamps, queue metrics, recording, and connection-loss evidence.
3. **Read-only UI:** Overview, topology, latest CAN, chronological monitor, dictionary, freshness, and corruption presentation.
4. **Virtual control tests:** command policy, Bench TX/stimulus lease model, injection, HMI, keyboard/gamepad, and synthetic peers restricted to virtual buses.
5. **Physical HMI and bench:** Bench TX session control, source ownership, ESTOP test labeling, HMI requests, and bounded isolated-ECU workflows.
6. **Physical actuator control:** kinematics and direct-actuator modes only after latency, jitter, watchdog, conflict, disconnect, and corruption tests pass.
7. **Later capabilities:** replay, configurable verification suites, richer charts, and Tauri packaging.

Each stage has a usable acceptance test and does not require enabling the hazards of the next stage.

## 20. Acceptance questions

The architecture is successful when an engineer can answer these questions without opening source code:

- Is the adapter connected, and which bus is active?
- Is the display genuinely live, delayed, or dropping data, and what is its measured visual age?
- Which ECUs are physical, simulated, late, offline, or faulted?
- What mode and power state did the UI request, and what did the vehicle confirm?
- What are the current command and feedback values for drive, steering, and brake?
- Where did a frame come from, when did it arrive, and is it valid?
- How is a signal packed into the CAN payload?
- Which frames will the tool transmit before a bench session starts?
- Is Bench TX enabled, which test source owns each stimulus lease and CAN ID, and when do those jobs expire?
- Are counters, checksums, enable bits, and safety limits being handled?
- Did an injection actually transmit, and what response followed?
- Is the diagnostic or full-bus recording complete and trustworthy?

If these answers are immediately visible, the Control UI provides a trustworthy bench-testing overview and the depth required for CAN engineering.

## 21. Research basis and decisions

The transport decisions above were checked against primary project documentation and source rather than inferred from the debug tool alone:

- [python-can CANalyst-II documentation](https://python-can.readthedocs.io/en/stable/interfaces/canalystii.html): both channels are supported; per-channel order is preserved; cross-channel delivery may be out of order; device timestamps must be used for comparison; the backend is unofficial and reverse-engineered.
- [python-can CANalyst-II source](https://github.com/hardbyte/python-can/blob/main/can/interfaces/canalystii.py): current default polling delay is 20 ms; device timestamps use 100 μs units; bounded `rx_queue_size` uses an evicting deque; TX timeout cannot prove successful bus arbitration or receiver delivery.
- [python-can Notifier documentation](https://python-can.readthedocs.io/en/stable/notifier.html): interfaces without an event-loop file descriptor are received on threads and distributed to listeners; shutdown must stop/flush listeners.
- [python-can asyncio documentation](https://python-can.readthedocs.io/en/stable/asyncio.html): the application can integrate notifier delivery with an asyncio loop, but thread-backed adapters still use receive threads.
- [FastAPI WebSocket documentation](https://fastapi.tiangolo.com/advanced/websockets/): disconnects are explicit exceptions and the example in-memory connection manager is single-process, supporting the one hardware-owner process decision.
- [libusb API documentation](https://libusb.sourceforge.io/api-1.0/): asynchronous USB transfers are available but add a lower-level event and recovery implementation; they remain a measured fallback, not the initial path.

Dependency versions used for hardware validation are pinned. Upgrades rerun protocol golden vectors, transport characterization, disconnect/reconnect tests, and the full-load dual-channel soak benchmark before release.

## 22. Scope-fulfillment matrix for bench testing

This matrix is authoritative for interpreting `scope.md`: every capability remains, but its purpose is hardware/code verification rather than vehicle operation.

| Scope capability | Bench implementation | What it verifies |
|---|---|---|
| Full Vehicle mode | Connect both CANalyst-II channels to the complete stationary network; monitor by default and permit explicit test injections | End-to-end RT/SYS/MTR routing, coexistence, timing, heartbeats, and integration behavior |
| Bench Test mode | Connect selected physical ECUs and synthesize only their absent peers | Isolated ECU startup, watchdog, state-machine, routing, command, and fault behavior |
| Pure Software mode | Use two named `python-can` virtual buses with the same router, codec, scheduler, and UI | UI/test development, protocol regression, and dry-run test definitions without hardware |
| Dual-channel CANalyst-II | Channel 0 High and Channel 1 Low at 500 kbit/s, captured simultaneously | Gateway forwarding, per-bus RT heartbeat, bus-specific IDs, and cross-bus timing |
| Real-time decoding | YAML-generated Python codec produces engineering values immediately after RX | Firmware wire representation and live physical outputs |
| Motorola + Intel layouts | Generated codec and golden vectors cover both orders and canonical bit mapping | Custom ECU messages plus SES/SEB external actuator protocols |
| Checksums and rolling counters | Scheduler regenerates automatic fields for every transmitted instance; validator checks received instances | RT/SYS/MTR and external-unit integrity handling, duplicate/gap/frozen behavior |
| Mandatory enable flags | Generated test templates lock required bits for positive tests; negative tests can deliberately violate them | Firmware acceptance/rejection behavior and diagnostic reporting |
| Overlapping/multiplexed bits | YAML compiler generates explicit active conditions and golden payload vectors | Correct encoding/decoding of shared Intel byte/nibble layouts |
| DLC=0 events | Raw envelope and codec permit empty data with DLC 0 | `0x001 SAFETY_ESTOP` reception, propagation, latch, and recovery code paths |
| YAML constants/limits | Inputs show generated bounds; positive tests remain within them; named boundary/negative cases are explicit | Limit enforcement at UI encoding and ECU behavior at/beyond boundaries |
| Hardware/bus status | Separate USB, channel-open, traffic, protocol, ECU, and message freshness indicators | Cable/device loss, silent channel, missing ECU, frozen heartbeat, and UI-stream failure |
| Topology map | Physical and synthetic nodes use distinct provenance and YAML-derived expected routes | Which bench components are actually present and which are emulated |
| Live dashboard | Latest valid command/status/feedback values with timestamp, age, source, and validity | Whether RT/SYS/MTR reacts correctly in real time |
| CAN dictionary | Searchable YAML-generated message cards, byte grid, signal table, comments, and optional live overlay | Exact protocol meaning without depending on DBC or source-code reading |
| Generic injection | Select message and signals, preview bytes, send once or for bounded/continuous test duration | Individual message handling and rapid exploratory diagnosis |
| Keyboard/gamepad | Browser captures test intent; backend shapes and schedules `0x300` or selected test commands with a visible Stop | Continuous RT response, command timeout, ramps, reversals, and limits on the bench |
| Hard Brake hotkey | Sends the defined brake test stimulus and correlates expected SYS/RT/SEB response | Brake arbitration and urgent-command code paths |
| ESTOP hotkey/action | Injects `0x001`, records exact submission, propagation, ECU state, and recovery sequence | Distributed ESTOP code paths; it is not the application’s own safety mechanism |
| Kinematics mode | Generate High-bus `0x300 HOST_DRIVE_CMD`; observe RT Low-bus actuator requests | RT inverse kinematics, limits, routing, counter/checksum generation, and timing |
| Direct actuator mode | Generate selected Low-bus motor/steer/brake commands in isolated tests | Unit protocol and feedback independent of RT kinematics |
| Target individual ECU | Test manifest selects physical targets and synthetic missing peers | One ECU’s behavior without unrelated network traffic |
| Sequential verification | Execute one defined stimulus/assertion step at a time with timeout and evidence | Every YAML message’s implemented behavior and regression status |
| Heartbeat emulation | Scheduler emits selected node heartbeat at the YAML-defined period and counter behavior | Watchdog satisfaction, heartbeat-loss transitions, and recovery |
| Synthetic peer status | Named templates emit required SES, SEB, MTR, SYS, RT, and Host frames with defined startup values | RT/SYS/MTR boot and operation when real peer hardware is absent |
| HMI mode/power | Emit `0x111`/`0x112` at 1 Hz and compare requested versus observed state | SYS mode/power state machine and RT-observed state |
| Virtual encoder | Emit controlled `0x206 MTR_MOTOR_FBK` trajectories | RT/SYS speed plausibility and EGAS-related code with no rotating hardware |
| Diagnostics | Classify diagnostic IDs/signals, preserve fault transitions, and link them to stimuli | Whether tested code reports the expected fault and diagnostic evidence |
| Persistent logging | Record immutable raw RX/TX observations and transport events in the backend; export decoded views | Reproducible post-test analysis without tying evidence to UI refresh rate |
| Stateless bridge separation | CANalyst transport only moves frames; router/codec/scheduler/test runner are separate | Simple hardware I/O plus stateful testing without routing spaghetti |

### 22.1 Test case model

Every repeatable test declares:

- test ID, purpose, and target ECU/code path;
- required hardware, bus wiring, and protocol semantic hash;
- physical nodes present and synthetic roles enabled;
- preconditions and startup grace period;
- exact TX manifest with bus, ID, values, period/count, and source role;
- automatic counters/checksums/enable fields;
- expected observations with timing tolerances;
- validity, sequence, checksum, and plausibility assertions;
- stop conditions and cleanup jobs;
- Pass, Fail, or Inconclusive rules;
- captured raw evidence and software/firmware versions.

Exploratory controls and keyboard/gamepad inputs use the same observation and recording pipeline, but only defined test cases produce a formal Pass/Fail result.

### 22.2 Meaning of “safety is not needed”

The application does not need production driving authorization, road-safe fail-operational behavior, driver authentication, redundant emergency control, or responsibility for keeping an occupied vehicle safe. It still needs deterministic test handling: stale jobs, duplicate CAN producers, wrong-bus frames, hidden drops, and false acknowledgments would invalidate RT/SYS/MTR results. Bench TX enable, stimulus ownership, deadlines, provenance, and Stop All exist for repeatability and equipment protection, not as a vehicle safety case.

## 23. Analyzer-derived improvements

The reviewed analyzers are strongest when they help an engineer move from “traffic exists” to “this exact behavior changed.” The following capabilities close that workflow gap without turning the product into a general-purpose DBC editor.

### 23.1 Triggered evidence capture

Maintain a bounded backend pre-trigger ring containing raw frames and transport events. A trigger may be a message/signal predicate, checksum or counter failure, freshness transition, adapter event, test-step boundary, or manual bookmark. When it fires, freeze the configured pre-trigger interval and continue for a configured post-trigger interval. Trigger evaluation occurs on decoded backend events, never on visually coalesced browser data.

The capture identifies its trigger, protocol hash, adapter epoch, capture gaps, and evidence-quality result. This makes intermittent RT/SYS/MTR failures diagnosable without recording every bench session indefinitely.

### 23.2 Baseline and session comparison

Allow a completed recording or approved test run to become a named baseline. Compare runs using semantic identities (`bus + message + signal`) rather than table row position. Report new/missing messages, period and jitter changes, value/range changes, diagnostic transitions, response-latency changes, integrity failures, and protocol-hash differences.

A comparison does not claim a firmware regression when either side has incomplete evidence, incompatible protocol semantics, different required topology, or different test inputs. Those cases are `Not comparable` or `Inconclusive`.

### 23.3 Deterministic offline replay

Replay feeds recorded envelopes into the same router, decoder, validator, freshness engine, dashboard projection, and assertion engine using a virtual clock. It supports pause, step, speed control, and seek from indexed checkpoints. Replay is observation-only by default and cannot enter the physical TX path. Its purpose is reproducing UI/verdict behavior and investigating evidence, not pretending that replayed traffic is live hardware.

### 23.4 Server-side filter and trigger language

Use one small, versioned predicate model for monitor filters, triggers, and test assertions. It addresses YAML semantic names and typed engineering values, with explicit operators, units, validity, bus, direction, provenance, and time windows. The backend validates and compiles predicates; the frontend only builds them. Arbitrary Python/JavaScript expressions are not accepted.

The UI shows the evaluated predicate, match count, last match, and compile errors. Saved predicates include the protocol semantic hash so renamed or changed signals fail visibly instead of silently matching the wrong data.

### 23.5 Evidence-quality gate

Every formal test and capture receives an evidence-quality state independent of the ECU verdict:

- `Complete`: no relevant capture, timestamp, adapter-epoch, storage, or assertion-evaluation gap;
- `Degraded`: presentation loss only, or an explicitly tolerated limitation that does not affect assertions;
- `Incomplete`: relevant raw loss, recording failure, clock discontinuity, adapter change, or unknown interval;
- `Not comparable`: baseline/replay semantics or topology are incompatible.

Formal `Pass` is permitted only with `Complete` evidence. Presentation coalescing does not degrade evidence because assertions and recording run before WebSocket delivery.

### 23.6 Adapter conformance and calibration wizard

Before trusting a new adapter, driver, dependency version, USB port, or poll configuration, run a guided characterization suite: channel mapping, bitrate, standard/extended IDs, DLC 0–8, timestamp resolution/wrap/reset, RX ordering, echo behavior, queue overflow visibility, unplug/replug, shutdown/reopen, sustained dual-channel load, and TX submission jitter. Store the resulting capability record and measured limits against the hardware/software fingerprint.

Runtime status uses measured capabilities. A dependency or adapter fingerprint change marks characterization `Outdated`; it never silently assumes earlier results still apply.

### 23.7 Workload budgets and graceful degradation

Define a tested workload envelope: frames/second per channel, number of decoded signals, active plots, raw subscribers, recording throughput, and scheduled TX jobs. Report current utilization against that envelope.

Under pressure, shed work in this order: hidden visual projections, plot history density, latest-state visual intermediates, then raw-monitor delivery. Never shed adapter supervision, RX integrity accounting, active assertions, scheduled test stimuli, critical events, or lossless recording without declaring evidence `Incomplete`. This ordering is deterministic and testable.

### 23.8 Engineer workflow additions

Add bookmarks/annotations tied to mapped time, bus sequences, and active test step; saved workspace layouts and filters; copy/export of a selected time window; and deep links from a failed assertion or diagnostic transition to the surrounding raw and decoded evidence. These are session metadata and never modify the immutable raw capture.

### 23.9 Priorities

| Priority | Improvement | Reason |
|---|---|---|
| P0 | Evidence-quality gate, adapter conformance, workload metrics | Prevent false confidence in every other feature |
| P0 | Triggered pre/post capture and unified predicates | Makes intermittent failures observable and repeatable |
| P1 | Deterministic replay through the production analysis path | Enables hardware-free debugging and regression of the tool itself |
| P1 | Baseline/session comparison | Turns recordings into actionable RT/SYS/MTR regression evidence |
| P2 | Bookmarks, saved workspaces, time-window export, evidence deep links | Improves bench investigation speed without changing core correctness |

## 24. Vehicle visual preview

The debug tool’s `TrikeViz` and the Leadmate robot-control dashboard confirm that a top-down visual model is valuable: engineers notice reversed steering, wrong gear, implausible turn radius, actuator disagreement, and stale feedback faster in a vehicle picture than in separate numeric cards. The preview belongs in Overview and Control, with a larger synchronized investigation view available from a failed test.

It is a diagnostic schematic, not a driving display, physics authority, or proof that the bench vehicle physically moved.

### 24.1 What the preview shows

Use a responsive SVG or Canvas top-down tricycle with two independent layers drawn on the same vehicle:

1. **Actuation layer:** what RT, SYS, Host, or another active producer is commanding the hardware to do—motor request, steering target, brake request, gear request, lamps, and other actuator outputs.
2. **Sensor layer:** what the hardware reports it is doing—motor speed/encoder feedback, steering-angle feedback, brake pressure/status, reported gear, lamp/status feedback, and faults.

Neither layer is tied to keyboard controls. Keyboard, gamepad, HMI, scripted tests, synthetic peers, physical Host, RT, SYS, and replay are merely possible producers of CAN messages. The preview consumes the resulting normalized CAN projections and therefore works in receive-only Full Vehicle mode with no Control workspace open.

The picture includes:

- rear wheel/motor state and front steering wheel angle;
- actuation command and sensor feedback as distinct simultaneous layers;
- speed/gear direction arrow;
- brake request and measured brake feedback;
- headlight, brake light, left/right indicator, ESTOP, and relevant fault state;
- predicted instantaneous center of rotation, turn radius, and curvature when inputs are valid;
- short optional predicted path, clearly labelled `Model projection`;
- a compact HUD with value, unit, source, age, and validity;
- optional per-component CAN activity/fault highlighting for RT, SYS, MTR, SES, and SEB.

Actuation uses a dashed/outlined ghost layer; valid sensor feedback uses a solid layer. For example, commanded front-wheel angle appears as a translucent outline while measured wheel angle is solid, and requested versus measured speed use aligned arrows. A visible disagreement band and numeric delta replace ambiguous animation. Synthetic and replay data retain their normal provenance styling.

The default is `Overlay`, showing both on one vehicle. Engineers may temporarily choose `Actuation only` or `Sensors only` to remove clutter, but both data pipelines continue independently. If one side is unavailable, the other still renders and the missing side is labelled rather than inferred.

### 24.2 Source selection and honesty

The Leadmate dashboard explicitly selects `EKF`, `ODOM`, or `SIM`. Adopt that principle, but generate the E-Trike selectors from named YAML/dashboard projections. Each visual property has one visible source policy, for example:

| Property | Requested layer | Observed layer | Derived layer |
|---|---|---|---|
| Speed | Host/RT command | MTR feedback | acceleration and predicted path |
| Steering | RT/SES command | SES status/diagnostic | curvature, ICR, turn radius |
| Brake | Host/RT request | SEB status/diagnostic | request-feedback error |
| Gear | requested gear | reported MTR/RT gear | signed display direction |
| Lamps/ESTOP | requested test stimulus | SYS/RT status | none |

Fallback is never silent. If the primary observed source is missing and a declared fallback is used, show `Fallback: <source>` and its provenance. The operator may pin a source for comparison. Values from different adapter epochs are never combined. Actuation and sensor source selection are independent: a command frame cannot be used as sensor feedback, and feedback cannot be presented as the commanded target.

### 24.3 Projection versus observation

The existing debug preview integrates speed and steering in the browser to invent `x`, `y`, and heading, uses pixel-scale wheelbase, and labels the result as actual vehicle state. That animation is useful aesthetically but is not observed pose. The new preview separates:

- `Observed`: directly decoded CAN feedback;
- `Requested`: tool/ECU command;
- `Derived`: deterministic calculation from valid inputs;
- `Projected`: virtual pose/path integrated from a model;
- `Synthetic`, `Replay`, or `Unavailable` provenance.

### 24.3.1 Center-locked ego view

The vehicle reference point is permanently anchored at the center of the preview. Projected travel never moves the tricycle toward an edge of the Canvas. Instead, the background grid, origin, path marks, and other world-relative references translate in the opposite direction, matching the debug tool’s useful ego-view behavior. Projected heading may rotate the tricycle around its fixed center; an optional heading-locked presentation rotates the background instead. In both cases, the center anchor never moves.

The sensor layer drives the default background projection from measured speed/encoder and measured steering. The actuation layer can simultaneously draw a dashed predicted path from commanded speed and steering. This gives one picture containing actual-response projection and commanded projection without connecting the visual to keyboard state. If sensor inputs are unavailable, the measured background projection freezes while the command path may remain visible; it does not silently switch to command data.

Background movement is a visual integration from a declared reset boundary, not measured global position. A fixed `Projection origin` marker, elapsed projected distance, source badge, and Reset Projection action make this clear. Adapter epoch changes, replay seek, projection-source changes, or timestamp discontinuities start a new projection segment rather than joining unrelated motion.

Without an actual pose source, the vehicle remains center-locked and the moving background/path appears only in explicitly labelled `Model projection` mode. Heading starts at zero or a user reset boundary and is labelled relative/projected. Wheelbase, track width, steering convention, limits, and unit conversions come from generated YAML/configuration in physical units—not Canvas pixels or magic scale factors.

`Requested` means a command observed on CAN or owned by an active backend test job. It does not mean the current keyboard key position. Browser input intent may appear in a separate input widget, but it reaches the vehicle picture only after the backend has shaped and encoded it into the actuation projection. The picture therefore works identically for scripts, physical controllers, Host commands, RT outputs, replay, and passive monitoring.

### 24.4 Freshness and corrupt-data behavior

The preview consumes the backend’s atomic vehicle projection; it does not reconstruct telemetry by independently selecting latest frames in the browser. Actuation and sensor halves update independently inside that atomic projection. Every contributing signal carries source, sample time, age, validity, and adapter epoch.

- stale input freezes its affected geometry and fades/hatches it with the age shown;
- missing input removes the affected derived geometry and displays `No data`, never zero;
- corrupt input keeps the last valid geometry but adds `Corrupt input` and the failed rule;
- source disagreement displays both layers and the delta;
- actuation staleness never makes fresh sensor feedback disappear, and sensor staleness never hides a fresh actuation command;
- an incomplete WebSocket snapshot marks the entire preview `Degraded` until resynchronized;
- replay, synthetic, requested, and physical observations remain visually distinguishable.

Clamping is presentation-only and is always disclosed. An out-of-range observation remains invalid evidence; it must not be silently turned into an apparently valid maximum-angle wheel.

### 24.5 Rendering model

Borrow the useful scheduling separation from both dashboards:

- backend capture/model/assertions remain event-driven and independent of rendering;
- the browser stores one immutable atomic projection snapshot;
- numeric/status changes render with the latest-state batch;
- geometry interpolates only between two valid samples and never extrapolates beyond a small declared visual horizon;
- `requestAnimationFrame` runs only while the preview is visible;
- `ResizeObserver` handles responsive high-DPI sizing;
- background tabs and hidden workspaces stop animation without stopping capture, tests, or recording.

The preview must remain understandable with animation disabled and support a reduced-motion mode.

### 24.6 Useful lessons from Leadmate robot-control

Adopt these concepts:

- separate communication callbacks from UI repainting;
- copy an internally consistent telemetry snapshot before drawing;
- make the pose/data source explicit rather than merging it invisibly;
- render fast-changing pose, slow-changing maps, and controls at different rates;
- isolate reusable painters/visual primitives;
- show multiple estimates together when disagreement itself is diagnostic;
- preserve pan/zoom/reset and source switching for investigation views.

Do not copy these implementation weaknesses:

- shared mutable public fields as the UI data contract;
- wall-clock `time.time()` for freshness;
- one heartbeat timestamp standing in for all topics/messages;
- global fixed signal-loss timeouts unrelated to expected message period;
- GUI code publishing directly from many widgets;
- simulation updating the same fields as physical telemetry without provenance;
- duplicate subscriptions/publishers and broad monolithic UI modules;
- rendering while holding communication locks.

The Control UI’s backend projection service, generated source rules, monotonic timing, immutable snapshots, scheduler/TX gate, and provenance model already provide the safer equivalents.

## 25. Shared API for React, LLMs, and automation

The Control UI exposes one client-neutral FastAPI contract described in `control-ui-api.md`.

```text
React UI ─────────┐
LLM tool adapter ─┼→ same REST/WebSocket API → same application services → CAN
Thin CLI / CI ────┘
```

Pydantic models generate FastAPI validation and OpenAPI. OpenAPI generates the React TypeScript client and provides source schemas for any optional LLM/MCP adapter or thin CLI. OpenAPI describes the normal API; it does not itself execute requests or automatically grant Claude network access. Claude Code may use the shared Python/HTTP client through terminal permissions; an Anthropic API host executes client tools; Claude Desktop/Claude.ai can use a thin MCP translation when required. There is no separately maintained UI API, LLM API, or terminal domain contract.

Caller type is audit metadata only. Domain logic must never contain behavior such as `if client is LLM`. All behavior depends on capabilities, session/profile state, protocol semantic hash, adapter epoch, source ownership, current revision, and the requested operation. Equivalent requests from React and an LLM produce the same validation, job, state transition, evidence, and result.

REST handles snapshots, queries, deliberate commands, and jobs. The shared WebSocket carries critical events, coalesced state, test progress, projection, and optional raw batches using per-client bounded queues and sequence/gap recovery. LLMs normally use snapshot/query/wait/test operations rather than consuming every frame, but authorized clients have the same supported subscriptions.

Full LLM access means all supported application capabilities, including physical bench operations when explicitly granted. It does not expose internal Python objects, USB handles, queues, arbitrary code execution, or domain-validation bypasses. Backend jobs own timing, assertions, leases, recording, and cleanup even when the requesting client disconnects.

Pure Software remains the default unattended profile. Physical mutation requires the same explicit session capability and finite Bench TX state used by React; it is not governed by a separate AI approval path.

The API exposes the data clients need to diagnose behavior: compatibility/capabilities, adapter and per-channel status, queue/storage metrics, atomic raw/decoded state with age/validity/provenance, session/lease/job state, resolved TX manifests, scheduler timing/jitter, test verdict/evidence quality, causal error events, and bounded raw evidence. REST snapshot/query/wait/job/event endpoints cover LLM environments without WebSocket support.

## 26. Error coding and structured logging

The backend, React, LLM tools, and optional CLI use the single stable catalog in `error-codes.md`. Errors are structured events with code, severity, raised/updated/recovered state, monotonic and wall timestamps, request/session/job/test correlation, adapter epoch, CAN identity, expected/actual context, and evidence references.

Operational logs do not duplicate the raw high-rate CAN recording. They record lifecycle, degradation, integrity, ownership, test, storage, stream, and recovery transitions and link to bounded raw evidence. Repeated failures emit an immediate first event, bounded summaries, and one recovery event rather than console spam.

Control UI infrastructure errors remain distinct from ECU-reported RT/SYS/MTR/PWT/EPS-C/SEB diagnostic flags. ECU faults are logged as observed diagnostic events with their original YAML-defined code/name/raw value; they are not relabelled as backend failures.

Codes originate in backend ownership boundaries: API middleware, adapter supervisor/wrapper, instrumented queues, protocol validator, freshness/topology service, scheduler, test runner, recorder/replay, subscription hub, and projection service. A central event factory adds common fields and persists them but does not guess the domain result. Clients never derive backend error codes from display text. Every condition has a mandatory fixed catalog ID such as `CUI-ADP-007`, a mandatory readable code such as `adapter.device_removed`, a contextual message, and a unique `event_id` for the occurrence. HTTP failures use RFC 9457 Problem Details, logs use the same code as OpenTelemetry `error.type`, and native CAN/UDS/J1939 identifiers are preserved only when the relevant layer actually reports them.

The backend event store is part of the shared API. React, LLMs, Python tests, and CI use identical code-registry, event query/detail/wait/summary/export, and WebSocket event-subscription resources. An LLM therefore has direct structured access to backend failures and causal/evidence context without filesystem or shell access. Capability-based redaction controls internal diagnostics; client type does not.
