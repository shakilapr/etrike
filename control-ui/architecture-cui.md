# Control-UI (CUI) — Architecture

**Status:** Streamlined Implementation
**Last updated:** 2026-07-15

## 1. Primary Objectives
- **Monitor:** Real-time visibility into all CAN traffic on the vehicle.
- **Control & Mimic:** Ability to inject specific frames to control actuators directly.
- **Simplicity:** Minimal runtime processes. Use direct-to-hardware testing without complex simulated profiles.

## 2. Supported Work Modes
- **Direct Hardware Control:** The tool connects directly to the High and Low buses via CANalyst-II. It relies on a physical hardware bypass pin to suppress timeout ESTOPs, eliminating the need for a complex "Synthetic Peer" simulation engine in software.

## 3. Technology Stack

### Frontend
- **React + Vite + TypeScript:** For a maintainable, fast interface with reactive state.
- **Zustand:** Ultra-fast state management for live CAN traffic.
- **UI Components:** Re-using the premium CSS layout and SVG/Canvas preview from the original `index.html` prototype.
- **Vitest + React Testing Library:** Component, store, keyboard, and feedback-projection tests with deterministic clocks.
- **Playwright:** Browser-level tests against the real FastAPI application and software CAN fixtures.

### Backend
- **Python + FastAPI:** Provides the asynchronous WebSocket foundation.
- **python-can:** Standard transport connecting directly to the CANalyst-II.
- **SQLite:** A lightweight, single-file database (`cui.db`) for persisting raw frame logs and anomalies, allowing post-test debugging.
- **Canonical protocol package:** Uses generated discovery/metadata under `protocol/generated` and the selected runtime codecs under `protocol/codecs/python`, all derived from or selected by `protocol/contracts/*.yaml`. CUI owns no duplicate signal layouts.
- **pytest:** Unit and integration tests using fake clocks, temporary SQLite databases, FastAPI test clients, and virtual CAN transports.

## 4. Feature Requirements

### Live CAN Workspace
- A high-performance table showing the latest decoded messages for both buses.
- **Highlighting:** Values flash briefly upon update. Erroneous messages (e.g., checksum mismatch, DLC length error, out-of-bounds) highlight in red and are logged to SQLite.

### Control Sidebar & Keyboard Teleoperation
- **Sidebar:** Sliders and toggles for sending specific manual commands (e.g., `0x169` steering angle, `0x7B9` brake pressure).
- **WASD Teleoperation:** A global keyboard listener that captures input to drive the vehicle (targeting `0x300 HOST_DRIVE_CMD` or direct actuator commands).
- **Dynamic Checksums:** The backend automatically computes and appends rolling counters and XOR checksums dynamically for injected actuator frames.

### Tricycle Preview
- A visual 2D top-down representation of the tricycle.
- **Feedback-Driven:** The graphic's state (steering angle, wheel rotation speed) is wired strictly to incoming CAN telemetry (`0x201 SES_STATUS`, `0x206 MTR_MOTOR_FBK`). It does not respond directly to UI input, guaranteeing a true reflection of physical hardware response.

## 5. Software-Only Verification Architecture

Software verification is a required development gate, not a production work mode. Test code may replace CANalyst-II with deterministic transports and software ECU traces, but a production process must never silently fall back from physical CAN to a virtual bus.

### Test boundaries

- **Injectable transport:** The receive/decode/state/record/stream pipeline depends on a small transport interface. Production uses CANalyst-II; tests use an in-memory adapter or `python-can` virtual buses through the same interface.
- **Injectable clock:** Freshness, age, periodic TX, batching, reconnect, and timeout tests advance virtual time rather than sleeping.
- **Single protocol authority:** Tests import `protocol/generated` metadata and `protocol/codecs/python`; they do not maintain a second CUI signal dictionary.
- **Observable accounting:** Every accepted, rejected, overwritten, streamed, recorded, and dropped frame has a counter so load tests can reconcile input with output.
- **Deterministic evidence:** Failed scenario, fuzz, and soak tests preserve the seed, input frame trace, API events, and first failing invariant.

### Reuse of repository test codebases

| Codebase | CUI use |
|---|---|
| `protocol/` | Run contract validation, generation drift checks, payload golden vectors, sequence vectors, and Python custom-codec tests before CUI tests. Use the generated discovery catalog for `(bus, CAN ID)` lookup. |
| `simulation/` | Export deterministic High/Low CAN traces from its existing drive, steering, brake, ESTOP, heartbeat-timeout, corruption, property, and soak scenarios. Replay those traces through the complete CUI backend and browser. Simulator evidence verifies CUI behavior; it is not claimed as firmware-equivalence evidence. |
| `native-test/` | Run the existing CTest suite and consume JSONL frames from `sim_engine_native` to check that CUI Python decoding agrees with frames produced by the generated C++ protocol and testable production firmware logic. |
| `debug-tool/` | Reuse proven test patterns for virtual dual-bus transport, replay, WebSocket batching/backpressure, SQLite persistence, latest-state stores, telemetry projection, keyboard release, and Playwright interaction tests. Shared protocol artifacts may be imported; product code is not copied blindly. |
| `tools/phase1-software-gate.ps1` | Supply upstream protocol, simulation, native firmware, and build evidence. Its result remains separate from the CUI result so a passing CUI cannot hide an upstream failure or known incomplete coverage. |

### Required automated layers

| Layer | Required checks |
|---|---|
| Protocol conformance | Generated artifacts are current; every supported message has a passing vector; bus, ID, frame format, DLC, endian, range, enum, checksum, and counter behavior match the canonical contract. |
| Backend unit | Timestamp wrap, strict DLC slicing, channel-to-bus mapping, unknown IDs, duplicate IDs on separate buses, latest-state counts/age, queue overflow, disconnect, and error-frame handling. |
| Virtual pipeline | Frame ingress through decode, validation, state update, SQLite recording, WebSocket batching, and API query using two virtual buses. |
| Injection | Engineering values encode to exact golden bytes; custom SES/SEB checksums and rolling counters progress correctly; invalid values never reach the transport; stopping a run cancels all periodic TX. |
| API and persistence | FastAPI request validation, WebSocket connect/filter/reconnect/backpressure, temporary-database migrations, raw-frame fidelity, decode-error persistence, and restart recovery. |
| Frontend | Zustand coalescing, bus-scoped identity, change/error highlighting, stale state, keyboard press/release/blur, and preview values derived only from received `0x201`/`0x206` feedback. |
| Browser E2E | Start the real backend with deterministic fixtures, exercise monitor and controls, verify transmitted bytes and feedback-driven preview, and retain Playwright trace/screenshot on failure. |
| Fault and load | Wrong DLC/format/bus, corrupt checksum, invalid enum/range, unknown ID, dropped/delayed/duplicated/reordered frames, timestamp wrap, quiet bus, transport exception, slow WebSocket client, queue saturation, and SQLite failure. |
| Cross-language | Python and TypeScript decode the protocol vectors identically; CUI decodes C++/native-generated frames to the same engineering values. |
| Soak | Accelerated dual-bus traffic and repeated connect/disconnect/injection cycles prove bounded memory, no deadlock, no unhandled exception, and exact drop/accounting totals. |

### Software release gate

A software-only CUI build is releasable for bench verification only when:

1. All required protocol, backend, frontend, browser, simulation-replay, and native compatibility suites pass with no required skips.
2. CUI-owned code meets at least 90% line and 80% branch coverage; bus mapping, codec dispatch, validation, injection, failure transitions, and periodic-TX cancellation have explicit branch tests.
3. Nominal scenarios produce zero decode errors, unknown messages, unhandled browser errors, lost database rows, and unaccounted queue drops.
4. Fault scenarios produce the expected typed error without crashing, deadlocking, transmitting invalid bytes, or presenting commanded values as feedback.
5. Every failure report contains a reproducible command plus seed or frame trace.

Passing this gate proves the software pipeline under modeled inputs. It does not prove CANalyst-II USB behavior, electrical CAN integrity, ECU timing on target hardware, GPIO bypass behavior, or actuator response; those remain Phase 4 bench checks.

The detailed implementation matrix and commands live in `software-test-plan.md`.

## 6. Excluded Features (Out of Scope)
Based on project needs, the following "overkill" features from previous VTC iterations are permanently removed:
1. **Synthetic Peer Engine:** No fake heartbeats are broadcast; hardware bypass pins handle timeout suppression.
2. **Formal Lease & Profile Management:** No explicit "Test Profiles" or "Resource Leases". The UI permits direct, unrestricted transmission.
3. **In-product Evidence Executor:** No operator-facing "Complete/Degraded" session grader or profile executor. The development/CI software gate above remains required, while bench operation relies on live streaming, SQLite logging, and manual verification.
