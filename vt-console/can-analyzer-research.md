# Open-Source CAN Analyzer Research

**Date:** 2026-07-13

**Local source location:** `tem/canalyzer/`

**Question:** Which proven patterns should the E-Trike bench Control UI reuse, and how can it reliably distinguish adapter loss, quiet buses, missing ECUs, corrupt frames, backend-stream loss, and overloaded presentation?

## 1. Codebases inspected

| Project | Local commit | Why selected |
|---|---|---|
| `python-can` | `b4f82abede25ff83376be793a2935c41f81c3869` | Actual transport foundation; CANalyst-II implementation, notifier, periodic sender, log formats, and terminal viewer |
| SavvyCAN | `2f683fd59bf3dfd8753f542554964fb971e39094` | Mature Qt analyzer with multiple connection types, connection validation, frame overwrite mode, capture, sending, and bulk UI refresh |
| CANgaroo | `ed3088d5393e02a01afe867af2ee8ab0e12db039` | Driver/interface abstraction, per-interface statistics, error frames, shared multi-channel readers, and hardware timestamp conversion |
| CANviz | `bbddde95ad874abce5b5952afc1c6d2a3baee252` | FastAPI + React + `python-can`, closest to the proposed Control UI stack |

These repositories are reference material only. No source is copied into the product without checking license compatibility and whether the code solves the CANalyst-II-specific problem.

## 2. Main conclusion

There is no single reliable `connected` boolean for CAN.

Connection health must be derived from independent evidence:

1. USB device presence.
2. Adapter open/worker alive.
3. Receive operation healthy.
4. High and Low channels configured.
5. Electrical CAN traffic observed per channel.
6. Expected YAML messages observed.
7. ECU heartbeat advancing.
8. Frame integrity valid.
9. Backend-to-browser stream current.
10. UI presentation not delayed or dropping.

Silence cannot prove adapter loss. An unplug exception can prove adapter loss; an expected heartbeat timeout proves an ECU/message is missing; neither proves the other.

## 3. Findings from `python-can`

### Useful patterns

- One common `BusABC` API across physical and virtual interfaces.
- `Notifier` isolates blocking receive in a thread when an interface has no selectable file descriptor.
- `Notifier.exception` captures receive-thread failure.
- Listener `on_error()` provides a direct failure callback.
- Context-managed, idempotent `shutdown()` stops periodic tasks.
- Standard raw logging/export support.
- CANalyst-II supports both channels and device timestamps.

### Important CANalyst-II limitations

The CANalyst-II backend does not provide a trustworthy connection state:

- `BusABC.state` defaults to `ACTIVE`; CANalyst-II does not override it.
- The backend polls every 20 ms by default.
- Its device timestamp is in 100 μs units and is not already a host monotonic timestamp.
- Multi-channel frames are polled channel-by-channel and can arrive out of cross-channel order.
- `rx_queue_size` creates `deque(maxlen=N)`, silently evicting old frames with no loss counter.
- The message data object contains the adapter’s padded storage; consumers must respect DLC.
- TX timeout detects device backlog, not successful arbitration or ECU reception.
- Error counters, bus-off, and hardware overflow are not exposed by this backend.

### Decision

Use `python-can` as the public transport abstraction, but wrap/subclass its CANalyst-II backend:

- tune and benchmark poll delay;
- leave its internal deque unbounded or add an observable drop counter;
- immediately move frames into our bounded instrumented queue;
- slice data to DLC;
- map/reset-detect device timestamps;
- observe `Notifier.exception` and `on_error()`;
- never use `bus.state` as CANalyst-II health evidence;
- label TX result `submitted`, never `delivered`.

## 4. Findings from SavvyCAN

### Useful patterns

- Explicit connection status rather than inferring it from frame presence.
- Adapter-specific connection implementations behind a common connection object.
- Qt SerialBus path subscribes to `errorOccurred`, `framesWritten`, and `framesReceived` separately.
- GVRET performs a connection-validation exchange, has a five-second connection timeout, and declares the connection invalid if the expected response does not arrive.
- A connection status event is emitted to the rest of the application.
- Capture can use chronological mode or overwrite-duplicate mode.
- UI work is refreshed in bulk on a timer rather than repainting directly for each received frame.
- Latest-ID overwrite mode keeps count and time delta, which is ideal for live analysis.

### Limitations for our case

- GVRET validation works because that adapter firmware supports an application-level request/response. CANalyst-II has no equivalent generic handshake.
- Some failure paths stop/disconnect but do not implement a consistent reusable reconnect state machine.
- Connection logic varies substantially by adapter.
- Native Qt signal/slot behavior cannot be copied directly into FastAPI/React.

### Decision

Reuse the concepts:

- connection state transitions are first-class events;
- adapter-specific evidence is capability-driven;
- use connect timeout and validation where the adapter supports it;
- use latest-ID overwrite plus count/delta;
- batch UI refresh independently of frame capture.

Do not invent a CANalyst-II handshake that its firmware does not support.

## 5. Findings from CANgaroo

### Useful patterns

- Clean `BusInterface` contract for open, close, is-open, send, receive, state, and statistics.
- Per-interface capability and status reporting instead of assuming all adapters expose the same data.
- RX/TX counts, RX/TX errors, overruns, dropped frames, and interface state are separate fields where supported.
- Candlelight multi-channel devices use one shared reader thread and per-channel queues.
- The shared Candlelight reader accepts normal and error frames.
- Strong hardware timestamp mapping:
  - captures device and host epoch;
  - detects 32-bit wrap using a half-range threshold;
  - rejects buffered pre-epoch frames;
  - maps device ticks to host-relative microseconds.
- File-descriptor access is protected against concurrent close/send/read races.
- Status window calculates rates from counter deltas over measured elapsed time.

### Limitations

- Some per-driver queues are unbounded.
- The Candlelight read loop treats timeout and some read failures similarly unless lower-level error state is consulted.
- Behavior and status quality vary by driver.
- Most low-level SocketCAN evidence is not available through CANalyst-II.

### Decision

Directly adopt the conceptual timestamp-epoch/wrap algorithm and capability record. Use separate counters for accepted, lost, invalid, RX, TX, error, and queue high-water marks. Do not expose unsupported health values as zero.

## 6. Findings from CANviz

### Useful patterns

- Same broad stack: FastAPI, React, `python-can`.
- Bus lifecycle is isolated in `BusManager`.
- Blocking `recv()` and `send()` run outside the ASGI event loop.
- Reader callbacks feed an async queue rather than writing WebSockets directly.
- Frontend WebSocket reconnects after closure.
- A periodic `/status` sync corrects stale frontend connection state.
- Frontend keeps latest frame per ID in a map.
- Per-ID timestamp buckets calculate observed rate.
- Plotting uses `requestAnimationFrame` rather than React rendering on every sample.
- Periodic-send UI timers stop on disconnect.
- Stale frame data is cleared at the beginning of a new session rather than erasing useful evidence immediately on disconnect.

### Problems to avoid

- The reader catches `recv()` exceptions, logs them, sleeps, and continues without clearing `_connected`; an unplugged adapter can still appear connected.
- Silence produces only an SLCAN warning after roughly 30 seconds; it does not establish adapter state.
- A soft disconnect retains the USB object. This solves a gs_usb Windows reopen problem but can mask physical removal and is not automatically appropriate for CANalyst-II.
- It overwrites hardware timestamps with `time.monotonic() - open_time`, losing adapter timing evidence.
- It manually echoes TX frames for adapters without echo; this is useful for display but can be mistaken for bus-observed TX.
- The WebSocket broadcaster has one shared 10,000-frame queue and sends each frame as JSON to each client.
- Queue-full drops are logged but are not part of a client-visible sequence/gap contract.
- A slow client can increase fan-out latency for other clients.
- The UI WebSocket retry count is finite, while the backend may remain available later.
- Five-second status polling is too slow for our real-time bench feedback.

### Decision

Reuse the thread-to-async boundary, latest-ID map, status reconciliation, and render-loop separation. Improve them with:

- receive error transitions adapter state immediately;
- hardware timestamps retained;
- per-client bounded queues;
- batched latest-state and raw subscriptions;
- stream sequence/gap detection;
- visible dropped/coalesced counts;
- continuous slow reconnect after fast retries;
- stream heartbeat around 250 ms rather than a five-second status poll.

## 7. Recommended connection-loss model

### 7.1 State dimensions

Do not create one enum that hides independent failures. Maintain these fields:

| Field | Values | Evidence |
|---|---|---|
| USB | Absent, Present, Unknown | PyUSB enumeration/device open errors |
| Adapter worker | Stopped, Starting, Running, Failed, Recovering | worker lifecycle, exception, liveness heartbeat |
| Adapter epoch | integer/UUID | increments on every successful open |
| Channel configuration | Closed, Open, Failed | initialization result per channel |
| Channel activity | Unseen, Active, Quiet | last RX timestamp; silence is not disconnect |
| Protocol activity | Unseen, Active, Late | last known YAML message per bus |
| ECU health | Unseen, Live, Late, Missing, Frozen, Invalid, Recovering | expected period, heartbeat counter, integrity |
| Backend stream | Connecting, Live, Delayed, Lost | WebSocket heartbeat and batch sequence |
| Presentation | Live, Delayed, Dropping, Paused | visual age, queue/coalescing/gap metrics |

### 7.2 Positive loss evidence

Immediately declare adapter failure on any of:

- CANalyst receive thread raises an exception;
- `Notifier.exception` becomes non-null;
- adapter worker exits unexpectedly;
- PyUSB enumeration no longer finds the selected device during an active session;
- send/receive repeatedly returns a device removal/USB I/O error;
- an explicit close/shutdown occurs.

The first positive loss event:

1. records a transport event;
2. disables Bench TX;
3. cancels periodic stimulus jobs;
4. ends stimulus leases;
5. marks current hardware test Inconclusive unless loss was expected;
6. preserves last data with increasing age;
7. closes the failed adapter best-effort;
8. creates visible reconnect attempts.

### 7.3 Ambiguous evidence

No frames for a period means only Channel Quiet. It becomes a message/ECU failure only when YAML says a periodic message should have arrived.

Examples:

- USB present + no frames + no expected nodes powered: adapter Open, channel Quiet.
- USB present + SYS heartbeat missing: adapter may be healthy; SYS Missing.
- High active + Low quiet: High Active, Low Quiet; not global disconnect.
- WebSocket live + frame timestamps old: backend connected, data stale.
- WebSocket socket open + no stream heartbeat: backend stream Delayed/Lost.

### 7.4 Detection timing

Initial targets:

- receive/USB exception: immediate;
- adapter-worker heartbeat missed: degraded after 500 ms, failed after 1.5 s;
- browser stream heartbeat: degraded after 750 ms, lost after 1.5 s;
- message/ECU: YAML-defined period/timeout, not a global constant;
- USB enumeration probe: 500 ms while a physical adapter is selected;
- presentation delay: visible when current screen value exceeds its message deadline or stream batch age budget.

USB probing must be benchmarked and must not contend with the active libusb handle. If concurrent enumeration is unreliable, isolate the adapter in a supervised worker process and use worker liveness plus I/O exceptions as primary evidence.

## 8. Optimal receive and presentation path

```text
CANalyst-II receive thread
  → minimal raw-envelope copy
  → bounded instrumented router queue
  → timestamp map + YAML decode + validation
  ├→ raw recording queue
  ├→ latest-state map by bus/ID
  ├→ freshness/corruption transitions
  └→ subscription hub
       ├→ critical events immediately
       ├→ latest-state batches at 20–30 Hz
       └→ raw monitor batches only when requested
```

Capture rate and display rate are deliberately different. The backend processes and records every accepted frame; the browser normally receives the latest state per ID plus counts, deltas, age, validity, and skipped-visual-update count.

Chronological raw view uses bounded batches and virtualization. A paused UI stops rendering, not capture or recording.

## 9. Changes recommended for the Control UI documents

The current architecture already includes most strong patterns. The research confirms and sharpens these requirements:

1. Do not use `python-can Bus.state` for CANalyst-II connection health.
2. Treat `Notifier.exception`/`on_error()` as immediate adapter-loss evidence.
3. Add an adapter-worker heartbeat and supervise thread/process liveness.
4. Add capability-based health fields with Unknown for unsupported metrics.
5. Use the CANgaroo-style timestamp epoch/wrap mapping.
6. Keep per-channel order and do not trust cross-channel ingestion order.
7. Make receive loss counters visible; never use silent `deque(maxlen)` eviction.
8. Separate bus Quiet from USB/adapter failure.
9. Add per-client WebSocket queues and sequence/gap recovery.
10. Use latest-ID overwrite mode with count and time delta as the default live table.
11. Batch GUI updates independently of capture.
12. Preserve hardware timestamps and distinguish synthetic TX echo from observed RX.
13. Continue reconnecting slowly after fast retries; never resume Bench TX automatically.
14. Make infrastructure loss produce Inconclusive rather than a false firmware Fail.

## 10. What cannot be known with CANalyst-II alone

Unless hardware/driver characterization proves otherwise, the tool cannot reliably know:

- CAN controller TEC/REC;
- error-passive state;
- bus-off state;
- exact electrical bus load from hardware counters;
- whether a submitted TX won arbitration;
- whether another ECU received or accepted TX;
- which physical ECU actually transmitted an ID;
- wire-CRC-corrupted payloads rejected before USB delivery.

These fields must be Unknown or derived with an explicit label. ECU response, rolling counter, checksum, heartbeat, and timing provide application-level evidence, not proof of physical-layer delivery.

## 11. Adoption decision

No complete analyzer codebase should be embedded into the Control UI.

- Use `python-can` as the transport API.
- Port the debug tool’s CANalyst setup and hardware characterization tests.
- Reimplement the small CANalyst wrapper needed for poll tuning, timestamp mapping, DLC slicing, and observable loss.
- Adopt SavvyCAN’s latest-ID/count/delta and connection-event concepts.
- Adopt CANgaroo’s capability/statistics model and timestamp wrap handling.
- Adopt CANviz’s thread/async split and React render-loop ideas, while replacing its connection and WebSocket loss behavior.

This keeps the codebase small while using the strongest verified patterns from all four projects.
