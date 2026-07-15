## Overall verdict

The architecture is **technically coherent, but it is no longer a “minimal CAN controller.”** It has evolved into a **bench verification platform** with protocol generation, deterministic stimulus scheduling, state reconstruction, source arbitration, capture integrity, formal test execution, evidence collection, and a rich engineering UI.

That distinction matters because most of the apparent overengineering comes from trying to satisfy two incompatible product definitions:

> **A.** Minimal CAN monitor/injector with synthetic peers
> **B.** Trustworthy ECU verification instrument that can produce defensible Pass/Fail evidence

The scope repeatedly demands minimum code and a stateless, non-gatekeeping backend, but it also requires periodic synthetic ECUs, rolling counters, keyboard control, actuator commands, liveness tracking, limits, verification, and logging. Those functions inherently require state and policy. 

The architecture resolves that technically by making only the transport layer stateless while putting state in the scheduler, router, TX gate, recorder, and test system. That is a good design, but it does **not** satisfy the literal wording of the current scope. 

My blunt assessment:

* Roughly **40% of the complexity is unavoidable** because of the actual functional requirements.
* Roughly **30% is good engineering that should be retained but implemented more simply**.
* Roughly **30% belongs in a later formal-verification product**, not the first controller release.

---

# Highest-priority issues

## 1. “Stateless backend” contradicts almost every active requirement

The scope says:

> The backend must be a transparent, stateless pipe and must not maintain simulated state or gatekeep messages.

But it also requires:

* periodic synthetic peers;
* rolling counters;
* dynamic checksums;
* heartbeat schedules;
* virtual encoders;
* keyboard/gamepad command streams;
* command limits;
* automatic enable fields;
* sequential verification;
* actuator isolation.

Those cannot be implemented by a stateless pipe. Someone must own counters, deadlines, current synthetic values, command expiry, periodic tasks, and duplicate-producer prevention.

The architecture’s solution is correct: keep the **transport adapter** thin and state-free, while stateful backend services own scheduling and testing. 

### Recommendation

Replace the scope requirement with:

> **Thin transport:** The CAN transport adapter only opens devices and moves raw frames. Protocol interpretation, synthetic peers, scheduling, state projection, transmission policy, and test behavior are separate backend responsibilities.

That preserves the intended anti-spaghetti rule without prohibiting required behavior.

---

## 2. Automatic virtual fallback conflicts with explicit profile control

The scope requires automatic fallback to the virtual bus whenever the CANalyst-II is unplugged. 

The architecture explicitly prohibits silent fallback because a physical session suddenly becoming virtual could make stale or synthetic data look real. 

The architecture is right here.

Automatic fallback is convenient during development, but dangerous during an active hardware session. It can hide:

* USB failure;
* missing physical feedback;
* disconnected actuators;
* a failed bench test;
* a broken wiring setup.

### Better rule

Use explicit profiles:

* **Physical profile:** adapter loss leaves the system disconnected and disables TX.
* **Pure Software profile:** starts directly on virtual buses.
* Optional **Development Auto mode:** may choose virtual only during initial startup when no physical test session exists.

Never switch an active physical session into simulation automatically.

---

## 3. The stack document contradicts the protocol architecture

The stack says `cantools` parses DBC files at runtime. 

The scope and architecture say:

* YAML is authoritative;
* generated codecs and metadata are used;
* DBC is only an optional export;
* runtime behavior must not depend on DBC.  

These are mutually exclusive architecture choices.

### Recommendation

Remove `cantools` from the required runtime stack.

It may remain as:

* a development validation tool;
* an optional DBC exporter;
* a compatibility test against third-party tooling.

It should not decode production UI traffic.

---

## 4. The channel mapping needs physical verification before implementation

The scope and control architecture specify:

* Channel 0 → High
* Channel 1 → Low

However, another E-Trike project document in your library describes:

* Channel 0 → Low
* Channel 1 → High. 

That document could be stale, but this is a high-risk contradiction. A wrong mapping would produce plausible-looking but incorrect bus attribution and could send commands onto the wrong network.

### Recommendation

Do not bury the mapping as a hard-coded assumption yet.

Before implementation:

1. Verify the actual bench wiring.
2. Pick one canonical mapping.
3. Correct all project documents.
4. Keep the mapping configurable internally.
5. Display it in the session header and recording metadata.
6. Warn when observed bus-specific IDs strongly disagree with the configured mapping.

The architecture’s proposed `CHANNEL MAPPING SUSPECT` warning is useful; automatic remapping would not be.

---

## 5. The architecture has two overlapping concurrency-control models

Current concepts include:

* Bench TX Enabled/Disabled;
* test session;
* stimulus lease;
* resource ownership;
* `bus + CAN ID` source ownership;
* command deadline;
* adapter epoch;
* command correlation.

Each has a valid purpose, but together they create a large conceptual surface. The logic specification already defines eight separate operational identities. 

There is overlap:

* stimulus leases prevent multiple control producers;
* CAN-ID ownership prevents multiple frame producers;
* deadlines prevent stale control;
* adapter epochs prevent old jobs crossing reconnects.

These do not necessarily require four separate services or user-visible concepts.

### Simplify to one TX job model

Use:

```text
Physical TX Enabled

ActiveTxJob:
    owner
    bus
    CAN ID
    optional resource group
    adapter epoch
    expiration
    period
    current semantic values
```

Rules:

* only one active job may own a `bus + CAN ID`;
* an optional resource group prevents kinematics and direct steering from operating together;
* every interactive job has an expiry;
* epoch mismatch invalidates the job;
* disabling physical TX cancels all physical jobs.

That preserves almost all current behavior while reducing terminology and implementation.

---

## 6. The documentation itself is becoming a source of complexity

The architecture document describes:

* state machines;
* reconnect behavior;
* WebSocket algorithms;
* freshness algorithms;
* recording behavior;
* test execution;
* assertion behavior.

The logic document then describes much of the same behavior again in normative detail.  

This creates drift risk.

There are already terminology remnants such as both **“arm”** and **“Bench TX”**, despite Bench TX apparently replacing the older term.

### Better document boundaries

| Document                       | Should contain                                                 |
| ------------------------------ | -------------------------------------------------------------- |
| `scope.md`                     | Required capabilities, exclusions, acceptance criteria         |
| `architecture.md`              | Components, ownership boundaries, data flow, major ADRs        |
| `vt-console-logic.md`          | Normative state machines, timers, algorithms, failure behavior |
| `stack.md`                     | Dependencies, deployment choices, version constraints          |
| Separate traceability document | Scope-to-implementation matrix                                 |

Avoid defining the same reconnect, freshness, TX, and test rules in multiple documents.

---

# What is genuinely unavoidable

These are not accidental architecture complexity. They follow directly from the requirements.

| Capability                                 | Why it is unavoidable                                                                                                         |
| ------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| YAML-derived protocol model                | Mixed endian layouts, overlaps, checksums, counters, limits, enums, and UI metadata cannot safely be maintained independently |
| Backend periodic scheduler                 | Synthetic peers and actuator commands require stable timing independent of browser rendering                                  |
| Backend-owned rolling counters/checksums   | Static payload repetition would generate invalid actuator and heartbeat traffic                                               |
| Explicit physical TX enable                | Connecting a USB device must not immediately begin transmitting                                                               |
| Input expiry/watchdog                      | Browser focus, WebSocket, keyboard, or controller loss must not leave stale continuous commands active                        |
| Duplicate producer prevention              | A synthetic ECU must not transmit the same ID as a real ECU                                                                   |
| Listen-before-speak                        | The tool must determine whether a claimed missing peer is already present                                                     |
| Adapter generation/epoch                   | Scheduled jobs from before a disconnect must not resume against a newly opened adapter                                        |
| Bounded queues and drop counts             | Raw CAN streams and browser rendering cannot be allowed to grow without limit                                                 |
| Separate latest-state and raw-stream views | Dashboards need current values; timing diagnosis needs individual frames                                                      |
| Freshness state                            | A last-known value without age is misleading                                                                                  |
| Separate recording I/O                     | Disk access must not block CAN RX or transmission scheduling                                                                  |
| One hardware-owner process                 | Multiple backend workers must not independently open the same USB device                                                      |

The scheduler is particularly unavoidable. The browser should express intent, but it should not own CAN periodic timing. That decision is correct. 

---

# Good ideas that should be retained but simplified

## Generated YAML protocol system

### Pros

* one protocol authority;
* eliminates hand-written Python/TypeScript disagreement;
* supports Intel and Motorola layouts;
* keeps counters, checksums, units, enums, and bounds consistent;
* enables golden test vectors.

### Cons

* the generator becomes critical infrastructure;
* schema evolution becomes its own project;
* generated code can be difficult to debug;
* generating Python, TypeScript, C++, documentation, DBC, hashes, and validators is a large initial undertaking.

### Verdict

**Keep the concept, reduce the targets.**

For the controller:

1. Generate backend protocol metadata/codecs.
2. Generate or export one JSON presentation catalog.
3. Let the backend serve that catalog to React.

The frontend does not need its own CAN encoder or decoder because the architecture already says the backend is authoritative.

Serving UI protocol metadata from the backend may eliminate the need for frontend/backend semantic-hash negotiation entirely. Both would be consuming the same runtime artifact.

Generating firmware C/C++ should be a separate protocol-platform project unless firmware already depends on the same generator.

---

## Single Python process with dedicated workers

The architecture proposes:

* one CAN receive thread;
* one router task;
* one scheduler;
* one recording worker;
* the ASGI event loop;
* isolated WebSocket senders. 

### Pros

* one owner for the USB device;
* no distributed state;
* no Redis/Kafka/process synchronization;
* straightforward shutdown;
* easy local deployment.

### Cons

* more concurrency than a simple FastAPI application;
* a driver crash can affect the API;
* queue ownership must remain disciplined.

### Verdict

**Keep.**

This is close to the minimum practical concurrency model.

Do not split the USB adapter into another process unless hardware testing proves the unofficial driver can hang or destabilize the process.

---

## TX acknowledgments with precise states

The design distinguishes:

* accepted;
* queued;
* submitted;
* failed;
* expired;
* canceled;
* optionally observed.

### Pros

* avoids claiming that `send()` means ECU delivery;
* useful for diagnosis;
* makes failures explainable.

### Cons

* too many statuses for the primary UI;
* additional command-state handling.

### Verdict

Keep the detailed backend states, but collapse the normal UI to:

* Rejected
* Scheduled
* Submitted
* Failed/Stopped

Expose detailed dispositions in the event log.

---

## Local API security

Loopback binding, origin validation, and a random session capability are not excessive for an application capable of commanding actuators. A malicious webpage can attempt to contact localhost services.

### Pros

* relatively low implementation cost;
* protects physical TX from unrelated browser pages;
* prepares for desktop packaging.

### Cons

* token lifecycle and WebSocket authentication add some setup.

### Verdict

**Keep loopback, origin checking, and a random session token.**

Defer elaborate Tauri-specific capability delivery until Tauri exists.

---

# Choices that currently feel overbuilt

## 1. Browser/backend clock synchronization

The design estimates:

* browser/backend clock offset;
* RTT;
* uncertainty;
* frame age;
* transport delay;
* render delay;
* visual age. 

### Pros

* excellent latency diagnosis;
* reveals a connected-but-delayed UI;
* useful for formal performance testing.

### Cons

* clock synchronization is subtle;
* produces false precision if implemented casually;
* adds protocol fields, state, UI, and test cases;
* local loopback transport is unlikely to justify it initially.

### Recommendation

Defer the full clock model.

For v1, send:

```text
age_at_publish_ms
backend_publish_sequence
```

The browser records its local monotonic receipt time and calculates:

```text
visible_age =
    age_at_publish_ms
    + elapsed_browser_time_since_receipt
```

This gives an honest visible age without synchronizing clocks.

Add RTT/clock estimation later as a diagnostics mode.

---

## 2. Metrics on every queue

The architecture requires every queue to expose:

* depth;
* high-water mark;
* dropped count;
* oldest-item age.

### Pros

* makes overload visible;
* prevents silent evidence loss;
* excellent for performance engineering.

### Cons

* creates repetitive plumbing;
* expands schemas and dashboards;
* engineers may spend more time instrumenting queues than delivering functionality.

### Recommendation

Instrument only the critical boundaries initially:

1. CAN driver → router
2. router → recorder
3. server → raw-monitor client

Track coalesced counts separately for latest-state UI updates.

Do not create a generic observability framework for every internal handoff.

---

## 3. Versioned records inside one process

Typed records are good. Versioning every internal record is probably premature.

### Pros

* easy future process separation;
* explicit contracts;
* safer migrations.

### Cons

* serialization-style thinking inside one codebase;
* version handling before any compatibility requirement exists;
* more tests and boilerplate.

### Recommendation

Use ordinary typed Python dataclasses or frozen models internally.

Version only actual boundaries:

* WebSocket protocol;
* recording file format;
* optional future IPC.

---

## 4. Full lossless recording and evidence integrity

The original scope asks for persistent diagnostic logging without slowing the UI. 

The architecture expands that into:

* immutable raw observations;
* transport events;
* protocol hashes;
* source hashes;
* adapter epochs;
* software versions;
* test boundaries;
* recording completeness;
* automatic Inconclusive verdicts when capture loss occurs. 

### Pros

* reproducible formal test evidence;
* excellent post-test analysis;
* trustworthy Pass/Fail results.

### Cons

* a major subsystem;
* storage formats and migrations;
* disk-pressure behavior;
* export tooling;
* evidence retention rules;
* significant testing cost.

### Verdict

This is justified only when formal verification is a core product requirement.

For the first release:

* append raw frames and transport events;
* include one session metadata header;
* mark dropped frames;
* support CSV or compact binary export.

Do not build a full evidence model before basic monitoring and bench control work reliably.

---

## 5. Formal Pass/Fail/Inconclusive test runner

The logic defines preparation, listening, exact stimuli, continuous assertions, stable durations, cleanup, evidence, and three-way verdicts. 

### Pros

* repeatable regression testing;
* removes subjective interpretation;
* supports automated ECU verification;
* highly valuable long-term.

### Cons

* effectively introduces a test DSL;
* requires assertion semantics;
* requires timing tolerances;
* depends on trustworthy recording;
* requires conflict and infrastructure-failure classification;
* can rival the rest of the application in effort.

### Recommendation

Do not make all injection go through this model.

Start with a lightweight sequential verifier:

```text
Send stimulus
→ wait for defined message/signal
→ display observed response and timeout
→ save result
```

Add formal stable-duration assertions and Inconclusive evidence later.

Exploratory control and formal testing should remain separate workflows.

---

## 6. Full cross-language protocol compiler

Generating:

* Python;
* TypeScript;
* C/C++;
* golden tests;
* Markdown;
* CSV;
* DBC;
* semantic hashes;
* source hashes

is ambitious. 

### Pros

* excellent consistency;
* long-term protocol governance;
* reduces duplicated firmware definitions.

### Cons

* far beyond a UI/backend project;
* generator defects affect every platform;
* firmware integration may become the critical path;
* considerably increases validation workload.

### Recommendation

Controller phase:

* validated normalized YAML;
* Python codec/runtime;
* JSON UI catalog;
* golden vectors.

Treat firmware generation as a separate milestone.

---

## 7. Dynamic WebSocket subscriptions and elaborate startup protocol

Atomic snapshot plus ordered deltas is valuable. The rest may be premature.

### Keep

* one initial snapshot;
* one stream sequence;
* delta batches;
* gap detection;
* bounded raw batches.

### Defer

* dynamic subscription negotiation;
* several independent stream classes;
* clock negotiation;
* complex accepted-limit negotiation;
* sophisticated per-client policies.

For a local single-operator tool, begin with one WebSocket containing:

```text
snapshot
latest-state batch
critical event
raw batch when monitor is open
heartbeat
```

---

## 8. Rich topology as an early priority

A topology map looks impressive but ordinary CAN does not prove which physical device transmitted an ID. The architecture correctly acknowledges that sender identity is an expectation from protocol ownership, not electrical proof. 

### Pros

* easy system overview;
* useful for synthetic-versus-observed status;
* accessible to non-CAN specialists.

### Cons

* significant UI effort;
* may imply more certainty than CAN provides;
* animated diagrams can consume time without improving diagnosis.

### Recommendation

Build a node-status table first:

| Node | Expected bus | Defining message | Status | Age | Observed source |
| ---- | ------------ | ---------------- | ------ | --- | --------------- |

Add the graphical topology later.

---

# Stack assessment

| Choice                    | Pros                                                                | Problems                                                                                  | Verdict                                                |
| ------------------------- | ------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------ |
| React + TypeScript + Vite | Mature ecosystem, strong UI tooling, suitable for desktop packaging | More frontend structure than a minimal HTML UI                                            | Keep                                                   |
| Zustand                   | Selector-based updates are useful for live state                    | Unnecessary if used as one giant frame store; can cause excessive updates                 | Keep only for latest state and UI state                |
| TanStack Table            | Strong sorting/filtering/table composition                          | It is not, by itself, the raw-stream virtualization solution implied by the stack         | Keep; add TanStack Virtual or a dedicated virtual list |
| Tailwind                  | Fast layout and consistent styling                                  | Utility-heavy markup can become noisy                                                     | Reasonable                                             |
| shadcn/ui                 | Accessible primitives and editable components                       | Does not “guarantee” premium design; copied components become maintained application code | Use selectively                                        |
| FastAPI                   | Good REST/WebSocket integration with Python                         | Does not guarantee zero blocking                                                          | Keep                                                   |
| `python-can`              | Common interface for physical and virtual CAN                       | CANalyst-II backend is unofficial and requires hardware characterization                  | Keep and pin                                           |
| `cantools` runtime        | Convenient DBC ecosystem                                            | Direct conflict with YAML-generated architecture                                          | Remove from runtime                                    |
| Tauri                     | Potentially good desktop distribution                               | Python sidecar, USB drivers, lifecycle, signing, and packaging are not automatically easy | Defer                                                  |

Some claims in `stack.md` should also be corrected:

* React Strict Mode does not provide TypeScript type safety.
* FastAPI does not “ensure zero blocking”; the architecture must isolate blocking driver and disk work.
* shadcn does not guarantee visual quality.
* “Absolute lowest latency,” “minimum lines of code,” and “premium UI” are separate objectives that can conflict. 

---

# Recommended lean architecture

```text
CANalyst-II / Virtual CAN
          │
          ▼
   TransportAdapter
  thin, device-specific
          │
          ▼
   Bounded RX Queue
          │
          ▼
 Router + Protocol Runtime
 decode, validate, freshness
       ┌──┴───────────┐
       ▼              ▼
 LatestState       RawRingBuffer
       │              │
       └──────┬───────┘
              ▼
        One WebSocket
   snapshot + deltas + events

UI command
     │
     ▼
   TxManager
 physical-TX enable
 one owner per bus/ID
 expiry + adapter epoch
     │
     ▼
  Scheduler
 counter/checksum
     │
     ▼
 Protocol Encoder
     │
     ▼
TransportAdapter

Optional:
Router → bounded recorder queue → raw log writer
```

This can be implemented as approximately six backend modules:

1. `protocol`
2. `transport`
3. `router_state`
4. `tx_manager`
5. `api_stream`
6. `recording`

Do not turn each box into its own service, process, framework, or abstraction hierarchy.

---

# What I would change in the scope immediately

Replace:

> Absolute minimum lines of code.

With:

> Minimize duplicated protocol logic, independent state owners, runtime processes, and unnecessary abstractions.

Raw line count is a poor architecture objective. Generated code may increase line count while reducing defects.

Replace:

> Backend is a transparent stateless pipe and does not gatekeep messages.

With:

> The transport adapter is a transparent frame-moving layer. Stateful protocol, scheduling, transmission control, and test behavior remain separate backend modules.

Replace:

> Automatically fall back to virtual CAN when unplugged.

With:

> Pure Software is an explicit profile. Adapter loss during a physical session never silently changes the data source. An optional development startup mode may select virtual CAN when no hardware session exists.

Replace:

> Zero-latency feel.

With measurable targets such as:

* dashboard updates normally at 20–30 Hz;
* critical state transitions appear on the next render;
* no silent RX loss;
* command scheduling jitter is measured;
* stale values are never shown as live.

The architecture’s move toward measurable service levels is much stronger than “zero latency.” 

---

# Suggested delivery cut

## Release 1 — useful controller

* YAML validation;
* Python codec;
* JSON UI metadata;
* dual physical/virtual transport;
* latest-state dashboard;
* raw CAN monitor;
* freshness and heartbeat state;
* one-shot injection;
* basic diagnostic logging.

## Release 2 — bench emulation

* physical TX enable;
* backend scheduler;
* dynamic counters/checksums;
* synthetic peers;
* listen-before-speak;
* source ownership;
* HMI mode/power;
* reconnect invalidation.

## Release 3 — interactive control

* keyboard/gamepad;
* command expiry;
* kinematics mode;
* direct actuator mode;
* simple sequential response verifier.

## Release 4 — verification platform

* formal test definitions;
* Pass/Fail/Inconclusive;
* strict evidence recording;
* advanced latency measurements;
* full queue observability;
* configurable verification suites;
* Tauri packaging.

---

## Bottom line

The architecture is not badly designed. In many places it is unusually careful and technically sound.

The problem is that the documents are claiming **simplicity** while specifying **verification-grade trustworthiness**. Those are not free together.

Keep the complexity that protects correctness:

* backend scheduling;
* generated protocol handling;
* explicit physical TX enable;
* source-conflict prevention;
* command expiry;
* reconnect epochs;
* bounded queues;
* freshness;
* separate latest and raw data paths.

Simplify or defer:

* full browser clock synchronization;
* metrics on every internal queue;
* versioned in-process records;
* formal test verdict infrastructure;
* evidence-grade recording;
* cross-language firmware generation;
* dynamic multi-subscription WebSocket machinery;
* Tauri;
* elaborate topology graphics.

The best simplification target is **not minimum LOC**. It is:

> **One protocol authority, one hardware owner, one operational state owner, one TX scheduler, one live stream, and no duplicated state machines.**



# The core contradiction

The scope uses **“stateless backend”** as a defense against complexity:

> “The backend must act as a transparent, stateless pipe between the CANalyst-II and the UI. It must not attempt to maintain simulated vehicle state or gatekeep messages.” 

That is understandable as an architectural instinct. It is trying to prevent:

* a giant backend vehicle simulator;
* hidden behavior;
* duplicated ECU logic;
* business rules scattered through the transport;
* a maze of routers and state stores;
* the backend becoming a second vehicle controller.

Those are good things to prevent.

But the wording goes too far. The same scope requires the application to act as synthetic ECUs, generate periodic messages, maintain rolling counters, enforce protocol limits, support continuous keyboard/gamepad input, isolate actuator tests, track heartbeat liveness, and verify responses. 

Those capabilities do not merely **benefit from state**. They are mathematically and operationally defined by state.

The correct distinction is:

> **The CAN transport should be protocol-agnostic and behaviorally transparent. The complete backend cannot be stateless.**

The architecture already moves in this direction by separating transport, protocol interpretation, observation, scheduling, TX policy, and presentation. 

---

# 1. “Stateless” has several meanings, and they are being conflated

There are at least four different interpretations.

## A. Truly stateless

A component receives an input and produces an output without depending on previous events.

For example:

```python
payload = encode_message(
    message="HOST_DRIVE_CMD",
    speed=1200,
    yaw=50,
)
```

Given the same inputs, it always produces the same result.

This is a pure function.

Some protocol operations can and should be stateless:

* decoding bits;
* scaling raw values;
* converting enums;
* validating DLC;
* checking bounds;
* calculating a checksum when all protected bytes are supplied;
* looking up CAN metadata.

But the application as a whole cannot work this way.

---

## B. No persistent database state

Sometimes “stateless backend” means:

> The server does not need a database to remember information after a restart.

That is largely achievable.

The controller probably does not need to restore:

* active synthetic peers;
* active motion commands;
* TX enable state;
* rolling counters;
* ownership;
* active test sessions.

In fact, it is better if those are deliberately lost on restart.

The backend can be **nonpersistent but stateful**.

Example:

```text
Backend starts
→ TX disabled
→ no synthetic jobs
→ no control owner
→ counters begin only when jobs start
```

This is entirely reasonable.

---

## C. No simulated vehicle model

This is likely what the original requirement intended.

It means the backend should not internally invent a complete E-Trike world such as:

```text
vehicle.speed
vehicle.position
vehicle.acceleration
vehicle.steering_geometry
vehicle.battery_soc
vehicle.motor_temperature
vehicle.obstacle_model
vehicle.drive_state_machine
```

unless a specific test explicitly requires such a model.

That is also a good constraint.

But maintaining:

```text
HOST_HEARTBEAT.counter = 7
HOST_HEARTBEAT.next_send = 10.500 s
MTR_FBK.synthetic_speed = 0 mm/s
```

is not necessarily maintaining a simulated vehicle.

It is maintaining the minimum state needed to generate protocol-correct test traffic.

That distinction is crucial.

---

## D. Thin, transparent transport

This is the architecture’s actual solution.

The adapter layer:

* opens CANalyst-II;
* receives frames;
* preserves channel, DLC, bytes, timestamps, and flags;
* submits already-authorized outgoing frames;
* reports errors;
* closes cleanly.

It does not decide:

* whether AUTO mode should be requested;
* whether a synthetic SYS should exist;
* what steering angle to generate;
* whether a command is within the test’s allowed limits;
* whether an expected response passed.

The architecture explicitly assigns those responsibilities elsewhere. 

This is the right interpretation.

---

# 2. Even the transport cannot be literally stateless

There is another subtle issue: a USB/CAN transport is not literally stateless either.

To communicate with CANalyst-II, it must retain operational state such as:

```text
device handle
selected adapter
channel configuration
bitrate
open/closed status
receive worker
pending receive queue
error state
timestamp mapping
shutdown state
```

An open hardware connection is itself state.

So “stateless bridge” cannot be interpreted literally.

The useful requirement is instead:

> The transport has no **vehicle-domain behavior** and no **test-policy authority**.

That is enforceable.

A better transport boundary would be:

```python
class CanTransport:
    def open(self, config): ...
    def receive(self) -> RawFrameBatch: ...
    def send(self, frame: RawCanFrame): ...
    def status(self) -> TransportStatus: ...
    def close(self): ...
```

The transport knows:

* device;
* channel;
* frame;
* timestamp;
* error.

It does not know:

* Host;
* RT;
* steering;
* AUTO;
* HMI;
* synthetic peer;
* speed limit;
* test verdict.

That is what “transparent” should mean.

---

# 3. Requirement-by-requirement: exactly why state is unavoidable

## 3.1 Periodic synthetic peers

The scope requires multiple frames at different periods:

* MTR feedback every 50 ms;
* steering and brake statuses every 100 ms;
* RT and Host heartbeats every 500 ms;
* RT heartbeat on two buses. 

To generate one periodic message, the software must know at least:

```text
enabled
bus
CAN ID
period
next deadline
current values
current rolling counter
source identity
```

Suppose the application emits:

```text
0.000 s → counter 0
0.100 s → counter 1
0.200 s → counter 2
0.300 s → counter 3
```

The frame at 300 ms depends on previous transmissions.

A stateless function cannot determine that the next counter is `3` unless something supplies the previous state.

The state could technically be stored elsewhere, but it must exist somewhere.

### Minimum required job state

```python
PeriodicJob(
    bus="low",
    can_id=0x201,
    period_ns=100_000_000,
    next_deadline_ns=...,
    counter=5,
    values={
        "angle": 0,
        "angle_status": "aligned",
    },
)
```

The architecture correctly assigns periodic traffic and counters to the scheduler. 

### What happens with no state?

You have only bad options:

1. Send the same static frame repeatedly.
2. Recalculate time from wall-clock values and risk discontinuities.
3. Have the browser construct every frame.
4. Put periodic protocol behavior inside the USB driver.

Static repetition is explicitly inadequate because counters would stop advancing. The architecture identifies static periodic payloads as behavior that should not be carried forward. 

---

## 3.2 Rolling counters

A rolling counter is inherently stateful.

Given:

```text
counter range = 0–15
```

the sequence is:

```text
0, 1, 2, 3, ... 15, 0, 1 ...
```

The value of the next frame is:

```text
next = (previous + 1) mod 16
```

That equation contains `previous`.

Therefore some component must remember previous state.

The logic specification explicitly says that periodic jobs increment the correct per-job/per-bus counter before each encode. 

The **per-job/per-bus** part matters.

For example, an RT heartbeat on High and an RT heartbeat on Low may require independent transmission histories:

```text
High RT counter: 4
Low RT counter: 9
```

A single global counter would be wrong.

### Why deriving the counter from time is risky

You might attempt:

```python
counter = int(monotonic_time / period) % 16
```

That looks stateless but creates problems:

* restarting produces an arbitrary counter;
* schedule delays can skip values unexpectedly;
* changing periods changes the sequence;
* two buses may become incorrectly synchronized;
* tests cannot deliberately create duplicates or gaps;
* actual submitted frames may not match calculated slots.

A counter should generally advance according to actual scheduled transmission behavior, not merely wall-clock time.

---

## 3.3 Dynamic checksums

A checksum function by itself can be stateless:

```python
checksum = xor(payload[0:7]) ^ 0xFF
```

But the complete outgoing frame is not stateless because its payload contains stateful fields such as:

* rolling counter;
* current target speed;
* current steering request;
* enable bits;
* mode;
* job-specific values.

The order is generally:

```text
1. obtain current command values
2. insert current rolling counter
3. force required protocol fields
4. pack the payload
5. calculate checksum over final protected bytes
6. transmit
```

The logic document explicitly specifies inserting the current rolling counter before calculating the checksum. 

So:

> **Checksum calculation is stateless; checksum generation within a live periodic stream depends on state.**

This distinction lets the implementation remain clean.

The codec can stay functional:

```python
payload = encode(definition, values, counter)
```

The scheduler owns `counter`.

That is good separation.

---

## 3.4 Heartbeat schedules

A heartbeat is more than “send this CAN ID.”

It has temporal semantics:

```text
send every 500 ms
do not burst missed heartbeats
track lateness
advance alive counter
stop on session loss
```

A scheduler must retain:

```text
last scheduled deadline
next deadline
period
number of missed periods
job active/inactive
```

The architecture states that missed deadlines should be recorded and stale instances skipped rather than sent as a catch-up burst. 

That requires history.

Consider the backend stalls for 1.2 seconds.

A naive loop might do this after recovery:

```text
send
send
send
send
send
```

to “catch up.”

That could place five stale heartbeat frames onto the CAN bus almost simultaneously.

The correct stateful scheduler instead says:

```text
three deadlines were missed
record missed_count += 3
send only the next valid current instance
schedule the next future deadline
```

Without retained scheduling state, the software cannot distinguish:

* an on-time frame;
* a late frame;
* a missed period;
* a catch-up burst.

---

## 3.5 Virtual encoder behavior

The scope requires synthetic motor feedback to represent virtual wheel movement. 

There are several possible modes.

### Constant value

```text
speed = 0
```

This requires very little state:

```text
enabled
speed
period
counter
```

### Manual value

```text
operator changes speed from 0 to 1000 mm/s
```

Now the backend must retain the latest requested value between transmissions.

### Ramp

```text
increase speed by 100 mm/s each second
```

Now it needs:

```text
start value
target value
rate
start time
current calculated value
```

### Model-derived value

If speed derives from acceleration or simulated motion:

```text
v(t + dt) = v(t) + a × dt
```

then previous simulated value is required.

The logic document explicitly supports constant, ramp, recorded trace, or model-derived virtual speed. 

### Important simplification

The backend does **not** need a full virtual vehicle just because virtual encoders exist.

A minimal implementation can begin with:

```text
constant value
manual value
simple ramp
```

Those are signal generators, not a complete vehicle simulation.

The architecture should explicitly preserve that limit:

> Synthetic peer state is message-oriented, not a hidden full-vehicle dynamics model.

---

## 3.6 Keyboard/gamepad control

Continuous browser input is one of the strongest reasons backend state is needed.

The scope requires keyboard/gamepad control. 

A browser can report:

```text
W pressed
steering axis = -0.3
brake = 0
```

But CAN commands often need to continue at a fixed rate independent of browser event timing.

The backend therefore needs to retain:

```text
latest accepted intent
client sequence
last renewal time
expiry deadline
current shaped command
previous shaped command
active input owner
```

The logic specification says the browser sends target intent, while the backend rejects stale or out-of-order updates, renews the lease, applies shaping, and generates CAN commands at the protocol-required period. 

Each step is stateful.

### Why browser-owned timing is fragile

Browsers are not stable periodic schedulers.

They may:

* throttle background tabs;
* pause timers;
* lose focus;
* suspend during sleep;
* experience garbage collection;
* reload;
* close;
* lose WebSocket connectivity.

Suppose the user presses `W`:

```text
t=0 ms:
browser sends speed target 1000

t=50 ms:
browser freezes

t=2000 ms:
backend still has no new message
```

What should happen?

A stateless backend has no concept of expiry. It cannot know whether the old speed request remains valid.

A stateful backend can retain:

```text
target = 1000
valid_until = t + 150 ms
```

At expiry:

```text
stop periodic command
or execute declared neutralization sequence
```

The architecture explicitly uses short validity deadlines so loss of focus, controller, WebSocket, or renewal stops the stimulus. 

This is not simulation. It is command lifecycle management.

---

## 3.7 Command shaping

The logic currently proposes:

* deadband;
* acceleration limits;
* deceleration limits;
* steering-rate limits;
* direction-change rules. 

These depend on previous output.

For acceleration limiting:

```text
new_speed ≤ previous_speed + acceleration_limit × dt
```

For steering-rate limiting:

```text
|new_angle - previous_angle| ≤ max_rate × dt
```

Both require:

```text
previous command
previous time
```

Therefore shaping is stateful.

### Is shaping unavoidable?

Not necessarily all of it.

This is where the architecture may be more complex than the minimum requirements.

The unavoidable pieces are:

* retain latest target;
* expire stale intent;
* produce CAN at the required period.

Optional pieces are:

* acceleration shaping;
* steering ramping;
* reversal handling;
* smoothing.

Those should exist only if:

1. the ECU expects them from the real Host; or
2. the test explicitly intends to emulate Host behavior.

Otherwise they risk duplicating logic that belongs inside RT.

The backend should not quietly become a second control system.

---

## 3.8 Command limits

The scope requires input values to stay within YAML-defined limits. 

Checking:

```python
minimum <= requested_value <= maximum
```

is technically stateless.

But determining whether a message may be transmitted may depend on state:

```text
current profile
physical or virtual destination
Bench TX enabled
selected target
active test type
current owner
negative-test override
```

The architecture’s command policy includes these checks. 

So two separate concerns exist.

### Stateless validation

```python
validate_signal_value(
    message="HOST_DRIVE_CMD",
    signal="speed",
    value=1200,
)
```

### Stateful permission

```python
may_transmit(
    profile=current_profile,
    tx_enabled=current_tx_state,
    owner=current_owner,
    bus="high",
    can_id=0x300,
)
```

The first belongs in the protocol layer.

The second belongs in operational control.

Combining them into one giant “safety validator” would be poor design.

---

## 3.9 Automatic enable fields

Mandatory protocol fields can be implemented mostly statelessly.

For example:

```text
SES_RollCntEnable = 1
SES_ChecksumEnable = 1
```

The encoder can force these whenever generating a normal positive-test frame.

No historical state is necessarily required.

However, the encoder still needs context:

```text
positive test
or
explicit negative test intentionally disabling the field
```

The logic permits selected rules to be violated only when the test explicitly declares the violation. 

That test mode is operational state.

### Clean separation

```python
encoded = encode(
    values=user_values,
    policy=EncodePolicy.POSITIVE_TEST,
)
```

or:

```python
encoded = encode(
    values=user_values,
    policy=EncodePolicy.NEGATIVE_TEST,
    allowed_violations={"checksum_enable"},
)
```

The encoder does not independently decide the current test mode. The test or TX layer supplies it.

---

## 3.10 Sequential message verification

Verification is inherently stateful because a test is a process over time.

A verification step contains:

```text
preconditions
stimulus
start time
expected response
timeout
observed evidence
result
```

The architecture defines a guided verifier with message values, prerequisites, expected feedback, timeout, and Pass/Fail/Inconclusive evidence. 

Suppose the test is:

```text
Send HMI_MODE_REQ = AUTO

Expect:
SYS mode = AUTO
within 500 ms
stable for 200 ms
```

The verifier must remember:

```text
stimulus submission time
whether matching feedback has appeared
when the matching condition first became true
whether it remained true
whether contradictory evidence appeared
whether capture gaps occurred
```

A stateless request handler cannot determine the result from a single frame.

The result depends on the sequence:

```text
t=0       command submitted
t=110 ms  SYS reports MANUAL
t=230 ms  SYS reports AUTO
t=350 ms  SYS reports AUTO
t=450 ms  SYS reports AUTO
```

The test passes because the expected condition appeared before timeout and remained stable.

That conclusion requires historical state.

---

## 3.11 Actuator isolation

The scope requires controlling a selected actuator without interfering with the rest of the network. 

That means the tool needs to answer:

```text
Who currently owns this CAN ID?
Is RT already transmitting it?
Is another UI function transmitting it?
Is a synthetic peer transmitting it?
Is this direct-control session allowed to replace that source?
```

This is an arbitration problem.

Arbitration requires state.

A minimal ownership table might be:

```python
owners = {
    ("low", 0x169): "direct-steering-test",
    ("low", 0x7B9): "direct-brake-test",
}
```

Without it, two internal sources could simultaneously generate the same ID:

```text
synthetic RT → steering command
manual injector → steering command
gamepad → steering command
test runner → steering command
```

CAN arbitration does not solve this.

CAN arbitration only resolves which frame wins access to the wire at a given moment. It does not tell the application which internal producer is semantically authorized.

The logic explicitly acquires ownership before direct-actuator tests and checks that a physical producer is not already sending the same ID. 

---

## 3.12 Duplicate synthetic and physical producers

Bench mode says synthetic peers should replace **missing** ECUs.

Therefore the system must remember:

```text
which IDs were observed
when they were last observed
which source currently owns each ID
whether synthetic transmission has started
```

The synthetic logic requires a listen-before-speak phase, refuses already-present IDs, maintains independent counters, and stops synthetic output when conflicting physical traffic appears. 

Without state, this sequence is impossible:

```text
0–1000 ms:
listen for 0x201

no physical 0x201 seen:
start synthetic 0x201

5 seconds later:
physical 0x201 appears

stop synthetic 0x201
report conflict
```

The decision at five seconds depends on the existing synthetic ownership state.

---

# 4. Observation alone also requires state

Even if the application were read-only, the UI requirements already force backend or frontend state.

The scope requires:

* latest status dashboards;
* ECU liveness;
* connected-item topology;
* heartbeat indicators. 

To display:

```text
RT: Live
SYS: Late
MTR: Offline
```

the system must remember:

```text
last valid heartbeat time
expected period
last alive-counter value
current freshness state
```

The logic explicitly defines:

* Unseen;
* Live;
* Late;
* Missing;
* Invalid;
* Frozen;
* Recovering. 

Those are temporal states.

A frame arriving now cannot by itself tell you whether:

* the previous counter advanced;
* three periods were missed;
* the ECU was previously missing;
* this is the first recovery frame.

Even a monitor is not entirely stateless once it claims to show **liveness** rather than just raw traffic.

---

# 5. The backend does not need “vehicle state” in the dangerous sense

This is the key architectural boundary.

The backend should maintain **operational state** and **observed state**, not necessarily a hidden authoritative vehicle model.

## Bad: duplicated vehicle model

```python
vehicle = {
    "mode": "AUTO",
    "power": "ON",
    "speed": 1.4,
    "steering": 32,
    "brake_pressure": 120,
    "safe_to_drive": True,
}
```

If the backend mutates this state according to its own assumptions and then treats it as truth, it risks becoming a parallel implementation of RT/SYS.

That is dangerous because:

* behavior can diverge from firmware;
* tests may validate the simulator rather than the ECU;
* hidden assumptions accumulate;
* debugging becomes ambiguous.

---

## Better: observed projection

```python
observations = {
    ("low", 0x206): {
        "latest_frame": ...,
        "decoded": {
            "speed": 1400,
        },
        "last_valid_time": ...,
        "freshness": "live",
        "source": "physical",
    }
}
```

The backend does not say:

> The vehicle speed is definitely 1.4 m/s.

It says:

> The newest valid `MTR_MOTOR_FBK` observation reports 1.4 m/s, arrived 42 ms ago, from the physical Low-bus path.

That is evidence, not simulation.

The architecture already separates raw observations, validation projections, latest operational state, and recording. 

That is a strong design decision.

---

# 6. Where should each kind of state live?

A clean ownership model would be:

| State                     | Owner                      | Reason                                 |
| ------------------------- | -------------------------- | -------------------------------------- |
| USB handle, channel setup | Transport                  | Device lifecycle                       |
| Raw RX queue              | Transport/router boundary  | Absorb driver timing                   |
| Latest observed frame     | Observation store          | Dashboard and diagnostics              |
| Freshness/liveness        | Observation service        | Depends on message timing              |
| Protocol definitions      | Immutable protocol runtime | Shared authoritative metadata          |
| Current counter           | TX scheduler job           | Changes per transmitted instance       |
| Next send deadline        | TX scheduler               | Timing ownership                       |
| Current synthetic values  | TX job/synthetic source    | Needed across periods                  |
| Latest keyboard target    | Interactive command job    | Needed between browser updates         |
| Command expiry            | Interactive command job    | Stop stale intent                      |
| `bus + ID` ownership      | TX manager                 | Prevent duplicate producers            |
| Current profile           | Runtime/session state      | Determines destination and permissions |
| Physical TX enabled       | Runtime/session state      | Explicit transmission boundary         |
| Test step/result          | Test runner                | Temporal assertion process             |
| Display formatting        | Frontend                   | Presentation only                      |

The architecture’s runtime model follows a similar rule: one scheduler owns deadlines, counters, checksums, leases, and TX; the frontend reads snapshots rather than creating a competing operational state machine. 

---

# 7. Why not put the state in the browser?

This is the most tempting simplification:

```text
React owns synthetic ECU state
React runs setInterval()
React calculates counters
React sends complete frames
FastAPI forwards them to CAN
```

Then the backend appears stateless.

But the complexity has not disappeared. It moved into the browser.

And the failure behavior becomes worse.

## Problems

### Browser timer throttling

Background tabs may reduce timer frequency.

A 50 ms synthetic MTR message may arrive late or irregularly.

---

### Page reload

Reloading loses:

* counters;
* active jobs;
* command targets;
* test progress.

The USB connection may still exist while the control state disappears.

---

### WebSocket failure

The browser may believe it is still controlling while the backend has stopped receiving updates.

Or the backend may repeatedly transmit the last complete payload without knowing whether it is stale.

---

### Multiple tabs

Two tabs may both send the same CAN ID.

Without server-side ownership, neither can authoritatively prevent the other.

---

### Focus loss

The browser can send a final neutral command on blur, but there is no guarantee the message reaches the backend.

The backend needs an independent expiry deadline.

---

### Counter and timing integrity

The actual CAN transmission schedule should determine counter progression and jitter—not the browser’s render/event timing.

---

### Security and authority

Any client capable of reaching the local API could potentially bypass the UI’s checks unless transmission policy is also enforced server-side.

---

## Conclusion

Putting state in React makes the backend superficially stateless while making the system less deterministic and harder to reason about.

The browser should express intent:

```json
{
  "target_speed": 1000,
  "target_yaw": -50,
  "valid_for_ms": 150
}
```

The backend should own actual CAN scheduling.

That matches the architecture’s principle that the browser expresses test intent but does not own CAN timing. 

---

# 8. Why not put the state inside the CAN transport?

Another possible simplification:

```text
CAN adapter supports:
start_periodic(frame, period)
stop_periodic(id)
```

This can work for static payloads.

But it becomes problematic when each frame requires:

* new counter;
* new checksum;
* current operator value;
* lease expiry;
* profile checks;
* source ownership;
* test metadata.

Then the transport begins to know protocol behavior.

It becomes:

```text
CANalyst transport
+ protocol encoder
+ scheduler
+ command authority
+ test lifecycle
```

That violates separation more severely than the current architecture.

The transport should receive an already-encoded and already-authorized frame.

The architecture correctly states that the adapter receives authorized requests while the backend scheduler owns periodic protocol behavior. 

---

# 9. “Do not gatekeep messages” also needs refinement

The scope says the backend should not gatekeep messages. 

That can mean two different things.

## Good interpretation

The backend should not arbitrarily hide or discard observations.

For RX:

* unknown IDs remain visible;
* invalid frames remain visible;
* raw bytes remain recordable;
* decode failure should not erase evidence.

That is good.

The logic document follows this principle: unknown and invalid frames remain observable rather than being fabricated or silently removed. 

---

## Problematic interpretation

The backend must blindly transmit anything requested.

That conflicts with scope requirements for:

* command limits;
* required enable fields;
* mode-aware injection;
* actuator isolation;
* safe synthetic-peer operation. 

The application cannot simultaneously promise:

> Commands are restricted to YAML limits.

and:

> The backend does not gatekeep outgoing messages.

Those are opposites.

---

## Better rule

Separate RX transparency from TX authority:

> **RX is evidence-preserving and non-gatekeeping. TX is explicit, validated, owned, and auditable.**

That is a much cleaner requirement.

For ordinary positive testing:

```text
validate bounds
enforce required fields
prevent duplicate ownership
check destination
check current session
```

For negative testing:

```text
allow only explicitly declared violations
record the intentional violation
continue enforcing unrelated rules
```

That avoids both extremes:

* blindly transmitting;
* preventing legitimate protocol-negative tests.

---

# 10. Is the current architecture overcompensating?

Some of it may be.

The architecture currently distinguishes:

* profile;
* Bench TX state;
* test session;
* stimulus lease;
* source ownership;
* adapter epoch;
* command correlation;
* deadlines. 

Each solves a real problem, but the implementation should avoid turning each noun into a large independent subsystem.

A simpler model could preserve nearly all behavior.

---

# 11. Minimal state model

I would reduce the system to four main state owners.

## A. Transport state

```python
TransportState:
    adapter_id
    epoch
    status
    channel_config
```

Responsibilities:

* USB lifecycle;
* raw RX;
* raw TX submission;
* transport errors.

No ECU names. No vehicle modes. No protocol behavior.

---

## B. Observation state

```python
ObservedMessage:
    latest_frame
    latest_valid_decoded
    last_seen
    last_valid
    last_counter
    freshness
    validation
```

Key:

```python
(bus, can_id)
```

Responsibilities:

* latest state;
* freshness;
* validation;
* liveness evidence;
* UI snapshots.

This is derived from traffic.

It is not authoritative vehicle simulation.

---

## C. TX manager and jobs

```python
TxRuntime:
    profile
    physical_tx_enabled
    adapter_epoch
    jobs: dict[(bus, can_id), TxJob]
```

Each job:

```python
TxJob:
    owner
    source_type
    bus
    can_id

    period
    next_deadline
    expires_at

    semantic_values
    rolling_counter

    optional_resource_group
```

Responsibilities:

* periodic scheduling;
* ownership;
* counters;
* current semantic values;
* expiry;
* checksum generation through codec;
* submission.

This one structure can replace much of the conceptual fragmentation between:

* scheduler job;
* source ownership;
* stimulus lease;
* command deadline.

---

## D. Optional test state

```python
TestRun:
    phase
    current_step
    started_at
    expected_condition
    timeout
    evidence
    result
```

Only required when formal sequential verification is active.

Do not force exploratory manual injection through a full formal test state machine.

---

# 12. The most important distinction: source state versus vehicle state

The backend needs **source state**.

Example:

```text
Synthetic Host source:
  HOST_HEARTBEAT
  counter = 7
  next deadline = 400 ms
  health = running
```

It does not necessarily need:

```text
Simulated Host thinks vehicle is in AUTO
Simulated Host follows route X
Simulated Host predicts acceleration
```

The former is necessary.

The latter may be overengineering unless required by a specific test.

Similarly:

```text
Virtual MTR source:
  speed output = 0
  update period = 50 ms
  counter = 2
```

is not a full motor model.

It is a controlled signal source.

I would avoid calling these objects “simulated ECUs” internally because that encourages feature growth.

Call them:

> **Scheduled protocol sources**

or:

> **Synthetic message sources**

That keeps the abstraction narrow.

---

# 13. Recommended rewritten architectural requirement

The current requirement should not say “stateless backend.”

I would replace it with:

> **Thin transport and explicit state ownership:**
> The CAN transport layer shall remain protocol-agnostic and behaviorally transparent. It shall open physical or virtual CAN interfaces, preserve raw frame evidence, submit authorized frames, report transport status, and perform no vehicle simulation or test-policy decisions.
>
> Stateful behavior required for synthetic peers, periodic transmission, counters, checksums, command expiry, liveness, source arbitration, and verification shall reside in small, explicit backend services with a single owner for each mutable state. The backend shall not maintain an independent authoritative vehicle model or duplicate RT/SYS control logic.

That captures the original intent without contradicting the product.

---

# 14. An even shorter scope version

For `scope.md`, I would use:

> **Minimal stateful backend:** The transport adapter is a thin protocol-agnostic CAN pipe. Backend state is limited to what is required for observation freshness, scheduled test traffic, counters/checksums, active command expiry, source ownership, and verification. The application must not contain a hidden full-vehicle simulator or duplicate ECU control logic.

This is probably the best balance.

---

# Final judgment

The architecture is correct to reject a literally stateless backend.

The requirements force state in several independent ways:

* counters require previous values;
* periodic schedules require deadlines;
* synthetic peers require current outputs;
* continuous controls require retained intent;
* disconnect handling requires expiry;
* isolation requires ownership;
* liveness requires last-seen history;
* verification requires temporal evidence;
* reconnect safety requires generation boundaries.

But this does **not** justify an unrestricted vehicle simulation framework.

The right architecture is:

```text
Thin CAN transport
        │
        ▼
Protocol decode/encode
        │
   ┌────┴─────┐
   ▼          ▼
Observed    Scheduled
state       message sources
   │          │
   ▼          ▼
  UI       TX manager
```

The guiding rule should be:

> **Keep the transport behavior-free, keep protocol functions mostly pure, keep runtime state explicit and bounded, and never create more simulated vehicle state than a specific test requires.**
