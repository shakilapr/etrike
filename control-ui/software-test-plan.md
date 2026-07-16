# Control-UI Software Test Plan

**Status:** Required implementation plan
**Last updated:** 2026-07-15

## 1. Purpose

Verify the complete Control UI software path without CANalyst-II or vehicle hardware, while reusing the repository's canonical protocol, deterministic simulator, native firmware tests, and proven analyzer test patterns. Software evidence and hardware evidence remain separate.

The test path is:

```text
protocol vectors / simulation trace / native JSONL frame
  -> virtual transport
  -> timestamp + strict DLC normalization
  -> bus/ID discovery + selected Python codec
  -> validation + latest state + counters
  -> SQLite + WebSocket/API
  -> Zustand + React UI + feedback preview
```

## 2. Test suites

| Suite | Tool | Scope | Required frequency |
|---|---|---|---|
| Protocol preflight | protocol CLI + pytest | Contract validation, generated drift, vectors, custom codecs, sequence semantics | Every CUI run |
| Backend unit | pytest | Pure transport, decode, state, clock, queue, recording, injection logic | Every change |
| Backend integration | pytest + FastAPI client | Dual virtual bus through API, WebSocket, and temporary SQLite | Every change |
| Frontend unit | Vitest + React Testing Library | Stores, components, keyboard, preview projection, reconnect behavior | Every change |
| Browser smoke | Playwright | Real frontend/backend process with deterministic fixture | Every change |
| Scenario replay | pytest + repository traces | Nominal and fault traces exported from `simulation/` | Every change |
| Native compatibility | CTest + pytest | Existing native tests plus C++ JSONL frames decoded by CUI | Before bench use |
| Fault/property | Hypothesis + seeded simulator cases | Malformed frames, ordering, state-machine and API invariants | Every change / scheduled matrix |
| Load/soak | pytest + Playwright | Sustained dual-bus load, slow clients, reconnect and repeated TX | Scheduled and before bench use |
| Hardware | pytest `hardware` marker | CANalyst-II, electrical bus, target ECUs, GPIO, actuators | Explicit Phase 4 only |

## 3. Reused repository assets

### `protocol/`

- Treat `protocol/contracts/*.yaml` as the only editable CAN authority.
- Run `protocol/tools/protocol.py generate --check` to reject stale generated artifacts.
- Parametrize CUI tests from `protocol/vectors/payload-v1.json` and `sequences-v1.json`.
- Use `protocol/generated/discovery.json` or generated Python metadata for bus-scoped lookup.
- Route semantic work through `protocol/codecs/python`; SES/SEB/PWT custom messages must not be decoded by a second CUI implementation.

### `simulation/`

- Reuse `SimulationRunner.capturedFrames` as the source for JSONL replay fixtures.
- Cover its drive, steering, braking, ESTOP, heartbeat timeout, bus routing, corruption, property, varied-condition, and soak suites.
- Add a deterministic exporter rather than coupling CUI Python tests to a live Node process.
- Store scenario name, simulator commit, protocol wire hash, seed, duration, and expected invariants with every trace.

### `native-test/`

- Run the existing CTest suite as upstream software evidence.
- Use `sim_engine_native` JSON Lines output for cross-language framing and decoding tests.
- Compare the native frame bytes and expected values against CUI's Python codec output.
- Do not claim complete firmware equivalence where native coverage is marked partial.

### `debug-tool/`

- Reuse its tests as patterns for virtual CAN routing, replay, stream batching, client filtering/backpressure, SQLite queries, latest-state coalescing, telemetry projection, keyboard handling, and Playwright flows.
- Prefer shared/generated protocol artifacts over copying decoder constants or signal layouts.
- Port a behavior only with a CUI-owned test that pins the required behavior.

## 4. Required test cases

### Receive and decode

- Channel 0 maps only to `high`; channel 1 maps only to `low`.
- The same CAN ID on two buses has independent state, counts, freshness, and sequence tracking.
- Unknown `(bus, ID)` remains visible as raw data and never receives a guessed name.
- Standard/extended format, RTR/error flags, strict DLC, payload bytes, and timestamp survive normalization.
- Generated and custom codecs return expected engineering values and typed failure statuses.
- Bad DLC, checksum, constant, enum, range, frame format, and wrong-bus frames are rejected and recorded.
- Duplicate, skipped, wrapped, frozen, and reordered counters produce the declared sequence result.

### State, time, and load

- Fake time controls age, stale/late state, batching, periodic TX, reconnect backoff, and timestamp wrap.
- Quiet bus, missing expected message, adapter exception, backend stream loss, and slow presentation remain distinct states.
- Latest-state overwrite retains correct counts and delta while raw recording receives every accepted frame.
- Queue overflow follows one documented policy and exposes exact received, processed, dropped, overwritten, streamed, and recorded totals.
- A slow WebSocket client cannot delay or exhaust other clients.

### Injection

- Engineering inputs encode to exact golden bytes on the declared bus and frame format.
- Invalid range, enum, unsupported codec, or wrong-bus request returns a typed error and never calls transport TX.
- SES/SEB checksum and rolling counter are correct across first send, increment, wrap, cancellation, and new session.
- Key release, window blur, WebSocket loss, backend shutdown, and test cancellation stop periodic TX or issue the specified neutral command.
- A transmitted command never updates the feedback preview unless a matching feedback frame is later received.

### API, database, and UI

- REST and WebSocket schemas reject malformed input and remain backward compatible within the CUI API version.
- WebSocket batching preserves bus-scoped identity, order metadata, errors, and drop counters across reconnect.
- SQLite stores exact raw bytes and decode status; migration, rollback, restart, and disk/write failure paths are tested.
- Store updates are bounded and coalesced; high-rate data does not trigger one React render per frame.
- Highlight, stale, error, and disconnected states expire only from fake-clock advancement.
- The preview reads steering from decoded `0x201` and speed from decoded `0x206`, including a test where commands deliberately disagree with feedback.
- Browser tests fail on uncaught page errors, unhandled promise rejection, failed network request, or unexpected console error.

## 5. Fault matrix

Every fault test declares the expected UI state, API event, persistence row, counters, and whether TX must stop.

| Fault family | Cases |
|---|---|
| Frame | Unknown ID, wrong bus, wrong format, RTR, error frame, DLC short/long, corrupt checksum, invalid constant/enum/range |
| Timing | Duplicate timestamp, timestamp reversal, hardware counter wrap, late frame, frozen counter, burst, prolonged silence |
| Delivery | Drop, duplicate, delay, reorder, queue full, overwrite, slow consumer |
| Transport | Open failure, receive exception, send exception, unplug evidence, reconnect, shutdown during receive |
| Stream | Client disconnect, backend restart, malformed subscription, slow client, send failure, reconnect gap |
| Storage | Migration failure, locked database, failed transaction, partial batch, restart recovery |
| Operator | Repeated keys, key release, focus loss, route change, control unmount, invalid slider/API input |

## 6. Reproducible commands

Commands are run from the repository root unless a working directory is shown.

```powershell
# Canonical protocol and Python codecs
python protocol/tools/protocol.py validate
python protocol/tools/protocol.py generate --check
python -m pytest protocol/tests/python

# Existing deterministic vehicle simulation
npm --prefix simulation test

# Existing native production-logic suite
cmake -S native-test -B native-test/build
cmake --build native-test/build
ctest --test-dir native-test/build -C Debug --output-on-failure

# CUI backend (after Phase 0/1 scaffolding)
python -m pytest control-ui/backend/tests -m "not hardware" --cov=control-ui/backend --cov-branch --cov-report=term-missing

# CUI frontend (after Phase 2 scaffolding)
npm --prefix control-ui/frontend run check
npm --prefix control-ui/frontend test

# CUI browser tests (after Phase 2 scaffolding)
npm --prefix control-ui/e2e test

# Upstream full software evidence; result is reported separately from CUI
powershell -File tools/phase1-software-gate.ps1 -SkipVehicle
```

Hardware tests must require both an explicit marker and explicit hardware configuration. They are never part of the default software command.

## 7. Gate and evidence

The fast pull-request gate runs protocol preflight, backend unit/integration, frontend typecheck/unit, a browser smoke flow, and a short deterministic simulation replay. The scheduled/full gate adds native CTest, the complete scenario/fault matrix, coverage, cross-language parity, and accelerated soak.

Required artifacts on failure:

- command, versions, commit, protocol wire hash, and selected test profile;
- random seed and minimized property-test example;
- JSONL input/output CAN trace with source and bus;
- backend logs and counter snapshot;
- temporary SQLite database where relevant;
- Playwright trace, screenshot, page errors, console errors, and failed requests.

The gate reports `PASS`, `FAIL`, or `BLOCKED`. Missing dependencies or missing required evidence are `BLOCKED`, never a skipped success. Software `PASS` authorizes hardware bench verification; it is not hardware conformance.
