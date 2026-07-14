# Phase 1 Software-Only Validation Details

This document captures the detailed Phase 1 validation scope discussed for RT,
SYS, and the related CAN software framework. Phase 1 is software-only: no ESP32
controllers, no CAN analyzer, no physical CAN transceivers, no steering unit, no
brake-by-wire unit, no motor unit, and no real vehicle hardware.

Phase 1 must prove, as much as possible in software, that the framework logic,
CAN contracts, safety state machines, fault handling, mode behavior, and virtual
integration paths work before moving to controller-only testing.

This document is intentionally detailed. It is a planning and traceability file,
not proof that the tests already exist or pass.

## Phase Definition

| Item | Definition |
|------|------------|
| Phase name | Phase 1: Software-only validation |
| Hardware allowed | None |
| Hardware forbidden | RT board, SYS board, CAN analyzer, CAN transceiver, SES/EPS-C, SEB, MTR, PWT, vehicle wiring, vehicle power |
| Main goal | Validate framework logic and CAN behavior before flashing or wiring controllers |
| Required outcome | A repeatable software gate that runs all tests and reports pass/fail evidence |
| Not covered | Electrical behavior, real CAN arbitration, real FreeRTOS scheduling on ESP32, physical actuator behavior, power sequencing on actual hardware |

## Current Known Status

The current repository already has useful Phase 1 assets, but Phase 1 is not yet
complete or automated as a reliable release gate.

Known RT checks already run successfully in recent work:

| Test | Current result observed |
|------|-------------------------|
| `pio test -e native` in `rt-esp32` | success, all tests passed |

Known SYS checks observed:

| Test | Current result observed |
|------|-------------------------|
| `pio test -e native` in `sys-esp32` | success, all tests passed |

Known current Phase 1 blockers or gaps observed:

| Area | Current issue |
|------|---------------|
| `native-test` CMake build | Fails in RT-related tests due MSVC `std::max`/Windows macro issue in `rt-esp32/src/pid_controller.h`; also has a `g_sim_time_us` unresolved external in `sim_engine_native` |
| Simulation suite | `npm test` in `simulation` fails and timed out; failures include CAN encoding expectations for `0x204`, `0x205`, `0x300`, `0x600`, `0x169`, `0x7B9`, and an 18s diagnostic soak case |
| RT/SYS vehicle builds | Need clean confirmed pass with sufficient timeout and stable build environment |
| PlatformIO cleanup | `.pio/build/vehicle` cleanup sometimes reports locked `sdkconfig.h`; native builds still succeeded but the lock should be investigated |
| Generated CAN files | `protocol/generated/*` timestamp-only diffs can appear after prebuild scripts; this creates noisy worktree changes |
| Phase 1 automation | No single command currently runs the entire Phase 1 gate and produces a report |
| Scenario matrix | Existing simulation scenarios are example-based; no full generated hundreds-case matrix yet |

## Test-Type Coverage Plan

Phase 1 should not rely on only one kind of test. Each test type catches a
different class of failure, so the release gate should combine compile checks,
static analysis, unit tests, mocked tests, component tests, integration tests,
protocol tests, fault injection, boundary tests, regression tests, and coverage.

| Test type | What it should check for this project | Existing coverage | Still to implement |
|-----------|----------------------------------------|-------------------|--------------------|
| Build / compile tests | RT/SYS compile for native, vehicle, bench, debug/release-like profiles | RT/SYS `native` builds pass; vehicle builds attempted | One scripted build matrix for RT, SYS, MTR, PWT; explicit vehicle build success; compile-time bypass flag audit |
| Static analysis | Bugs without running code: null pointer, bounds, uninitialized state, suspicious assignment, dead code, unsafe casts | Not yet a formal gate | Add `clang-tidy` or `cppcheck` for C/C++; add TypeScript `tsc --noEmit`; add lint for simulation/debug-tool; fail on safety-relevant findings |
| Unit tests | One function/class/module at a time: physics, PID, mode manager, safety monitor, checksum, parser | RT physics/PID/heartbeat/steering; SYS mode/safety/brake tests | Fill missing RT/SYS modules: gateway policy, brake arbitration, heartbeat health flags, run-mode/bypass init, invalid enum handling |
| Mocked unit tests | Logic with hardware/API replaced: fake time, fake CAN, fake GPIO, fake NVS, fake serial, fake ESP timer | Some fake time via SYS safety monitor; native-test HAL stubs exist | Add common fake clock, fake CAN bus, fake GPIO override pin, fake NVS/crash storage, fake TWAI/MCP2515 error injection |
| Component tests | Related modules together: RT dispatch + physics + steering; SYS mode + safety + brake | Some native-test component-like tests exist | Add explicit RT component tests and SYS component tests with virtual CAN inputs/outputs and invariant checker |
| Integration tests | Multiple firmware layers together in software: Host + RT + SYS + unit mimics | Simulation has some integration/scenario tests | Add full virtual RT/SYS/Host/SES/SEB/MTR/PWT integration matrix; fix existing failing simulation tests first |
| On-target tests | Tests running on actual ESP32: FreeRTOS, drivers, memory behavior | Not Phase 1; belongs to Phase 2 controller-only | Prepare Phase 2 ESP-IDF Unity/on-target tests for queues, timers, TWAI, stack/heap, watchdogs |
| RTOS/concurrency tests | Task timing, queues, notifications, race conditions, watchdogs | `native-test` has task/watchdog tests but current build is not green | Fix native-test build; add virtual scheduler tests for task period, queue overflow, ESTOP fast path, mode/ESTOP races |
| Protocol tests | CAN IDs, DLC, endian, scaling, checksums, counters, wrong-bus behavior | `test_signals`, protocol roundtrip, checksum tests, simulation CAN tests exist | Fix failing simulation CAN encoding expectations; add every RT/SYS frame to protocol matrix; wrong-bus and malformed-frame tests |
| Fault-injection tests | Failure handling: disconnect, timeout, bad checksum, frozen counter, NVS fail, queue fail | Some simulation and HIL docs describe faults | Add software-only fault injector for Host/RT/SYS/SES/SEB/MTR/PWT; add NVS/write-fail and queue-full tests |
| Boundary/edge-case tests | Limits and invalid values: min/max speed, yaw, pressure, stroke, counters, timeouts | Some RT physics and simulation varied-condition tests | Add generated boundary matrix for every input field and timing threshold |
| Regression tests | Prevent old bugs from returning; one test for every fixed bug | RT QA bug tests exist | Add regression ID list for every known QA/changelog bug; ensure each fixed bug maps to one automated test |
| Coverage tests | Measures code exercised by tests | Not yet a formal gate | Add host/native coverage with `gcov`, `lcov`, or `gcovr`; track line/branch/function coverage for RT/SYS testable modules |

Suggested Phase 1 implementation order by test type:

| Order | Test type | Reason |
|-------|-----------|--------|
| 1 | Build / compile tests | Nothing else matters if target builds are not reproducible |
| 2 | Existing unit/regression tests | Establish current green baseline |
| 3 | Static analysis | Finds simple safety bugs before expanding runtime tests |
| 4 | Protocol tests | CAN contract mismatch invalidates all integration tests |
| 5 | Mocked unit tests | Enables deterministic hardware-independent behavior checks |
| 6 | Component tests | Validates groups of RT/SYS logic before full simulation |
| 7 | Integration tests | Validates virtual RT/SYS/Host/unit interactions |
| 8 | Fault-injection tests | Proves safe behavior when things disconnect, corrupt, freeze, or timeout |
| 9 | Boundary/edge-case generated tests | Covers corners too numerous for hand-written tests |
| 10 | RTOS/concurrency tests | Catches event ordering, queue, timing, and race bugs |
| 11 | Coverage tests | Shows what remains untested and prevents false confidence |

Corner areas that need explicit implementation:

| Corner area | Examples to implement |
|-------------|-----------------------|
| Compile profiles | RT/SYS native, vehicle, bench; MTR/PWT compile; flags audited for bypass/test defines |
| Static analysis | `cppcheck`/`clang-tidy` on RT/SYS/shared; `tsc --noEmit` on simulation/debug-tool; generated report |
| CAN frame fields | Every ID, DLC, endian, signedness, scaling, offset, checksum, rolling counter, mode mux |
| CAN bus placement | Low-only, high-only, powertrain-only, forwarded, blocked, wrong-bus, duplicate, unknown ID |
| Mode authority | Manual, Auto, Estop, invalid, stale, repeated, out-of-order, reconnect during mode transition |
| Motion cases | Forward, reverse, stopped, creep, max, stop-go-stop, reverse-to-forward, forward-to-reverse |
| Steering cases | Straight, gentle left/right, hard left/right, reverse left/right, high-speed clamp, low-speed full lock |
| Braking cases | No brake, normal brake, obstacle assist, ESTOP brake, release, brake while turning, brake while reversing |
| Timeouts | Startup grace, Host stale, Host heartbeat, RT heartbeat, SYS heartbeat, SES feedback, SEB feedback |
| Reconnects | Host, RT, SYS, SES, SEB, MTR, PWT reconnect before/at/after timeout and during ESTOP/braking/steering |
| Bypasses | Production off, prototype pin off/on, software mode on, EPS bypass, SEB bypass, MTR absence bypass |
| Queue/race behavior | ESTOP fast path, queue full, duplicate ESTOP, mode change at same tick as ESTOP, stale command at boundary |
| Persistence/failures | NVS read fail, NVS write fail, crash flag invalid, config missing, default recovery safe |
| Diagnostics | Faults visible in heartbeat health flags, state reports, diagnostic frames, logs, Phase 1 report |
| Coverage | Uncovered files/functions listed; safety-critical uncovered code becomes a blocker |

## Additional Test Surfaces To Add

The previous sections cover the obvious functional and fault scenarios. The
following surfaces catch deeper issues that often escape normal unit and scenario
testing.

| Surface | What to test | Why it matters |
|---------|--------------|----------------|
| Determinism | Same input trace produces identical output frames, states, and diagnostics every run | Safety validation must be reproducible |
| Generated-code consistency | YAML, generated C++ constants, generated TypeScript constants, and protocol structs agree | Prevents simulator/debug-tool/firmware from validating different CAN contracts |
| Schema drift | CAN dictionary, `can_protocol.h`, generated files, docs, and tests do not disagree on ID/DLC/scale | Avoids hidden integration mismatch |
| Property-based tests | Random valid inputs always satisfy invariants such as bounds, mode authority, checksum validity | Finds combinations not manually listed |
| Fuzz tests | Random invalid frames, bad lengths, bad enums, malformed payloads, weird timing | Ensures parsers fail safe instead of trusting garbage |
| Metamorphic tests | Related inputs produce related outputs, e.g. left/right steering signs mirror, speed sign changes reverse behavior | Catches math/sign bugs like reverse steering inversion |
| Mutation tests | Deliberately break safety logic and confirm tests fail | Proves tests are sensitive, not just present |
| Resource-limit tests | Queue full, frame burst, max bus load, large logs, memory allocation failure where applicable | Embedded failures often happen at limits |
| Reset/restart simulation | RT reset, SYS reset, Host reset, unit reset during Manual/Auto/Estop | Proves safe defaults and resynchronization |
| Persistence simulation | Corrupt NVS, missing config, stale crash flag, invalid saved mode | Prevents unsafe boot from old or corrupt state |
| Version compatibility | Firmware version, CAN catalog version, debug-tool/simulation generated version align | Prevents testing one protocol while flashing another |
| Authority/security | Wrong source sends mode/drive/brake frame, valid frame on wrong bus, replayed frame, stale counter replay | Prevents authority bypass through CAN injection or replay |
| Diagnostics completeness | Every safety-relevant fault has a visible diagnostic or health flag | Field debugging and release evidence require observability |
| Log correctness | Logs include mode, fault, bypass state, build version, and timing without hiding critical events | Prevents silent safety state changes |
| Numeric robustness | NaN, infinity, overflow, underflow, rounding, signed/unsigned conversion, endian sign extension | Vehicle math and CAN packing must fail safe |
| Time robustness | Clock jumps, wraparound, zero delta time, long uptime, timeout boundary jitter | Time bugs can break heartbeat and staleness logic |
| Idempotence | Repeating same ESTOP, mode, reconnect, or fault command does not produce extra unsafe side effects | CAN frames can duplicate or replay |
| Monotonicity | Brake assist increases as obstacle distance decreases; steering clamp decreases as speed increases | Catches inverted curves and bad interpolation |
| State-machine exhaustiveness | Every state/event pair has an expected next state or explicit ignore behavior | Prevents undefined behavior on rare sequences |
| Test isolation | Tests do not depend on previous tests, stale globals, old `.exe` files, generated timestamps, or dirty environment | Prevents false pass/fail results |

### Determinism And Replay Tests

Every generated scenario should be replayable. If a scenario fails once, the
test report must contain enough information to run the same case again.

Required deterministic replay tests:

| Test | Required result |
|------|-----------------|
| Same seed, same config, same commit | Identical frame trace and state timeline |
| Same hand-written trace replayed twice | Identical outputs and diagnostics |
| Failure replay from saved artifact | Reproduces the same invariant failure |
| Different seed recorded | Different scenario allowed, but seed and event list saved |
| Randomized test without seed | Not allowed in release gate |

Required replay artifact fields:

| Field | Meaning |
|-------|---------|
| `gitHash` | Commit under test |
| `dirtyStatus` | Whether local changes were present |
| `scenarioId` | Stable scenario name or generated ID |
| `seed` | Random seed, if generated |
| `events` | Ordered input/fault/mode/connect/disconnect events |
| `config` | Timing, limits, bypass flags, build profile |
| `expectedInvariants` | Invariant list applied to the run |
| `failureTimeMs` | Virtual time of first failure |
| `tracePath` | Saved virtual CAN/state trace |

### Generated-Code And Protocol Drift Tests

Phase 1 must fail if firmware, simulator, debug-tool, and docs disagree about the
CAN contract.

Required drift checks:

| Check | Required result |
|-------|-----------------|
| YAML to generated C++ | Regenerating from YAML produces no semantic diff |
| YAML to generated TypeScript | Regenerating from YAML produces no semantic diff |
| Generated constants to `can_protocol.h` | IDs, DLCs, field positions, scale, offset, signedness agree |
| Simulation constants to shared protocol | Simulation tests import or validate against shared-generated data |
| Debug-tool constants to shared protocol | Debug tool decode/encode tables match shared CAN catalog |
| Documentation tables to generated catalog | Any documented ID/DLC table matches generated source of truth |
| Timestamp-only generated diff | Does not count as semantic protocol change; should not pollute docs-only commits |

Protocol drift failure examples:

| Drift | Risk |
|-------|------|
| `0x7FD` DLC differs between test and firmware | Heartbeat validation is false |
| Simulation expects `0x169` byte layout different from firmware | Steering tests validate wrong frame |
| Debug tool encodes `0x7B9` security bits differently | Bench injection can be rejected by SEB |
| Docs list old rate or DLC | Human test procedure can accept wrong behavior |

### Property-Based And Fuzz Tests

Property-based tests generate many valid inputs and assert invariant properties.
Fuzz tests generate invalid inputs and assert safe rejection.

Property tests to add:

| Property | Input range | Expected invariant |
|----------|-------------|--------------------|
| Steering clamp monotonicity | Speed from 0 to max, yaw fixed high | Allowed angle never increases as speed increases above low-speed threshold |
| Left/right steering symmetry | Equal speed, yaw `+x` and `-x` | Target angles are opposite signs with similar magnitude |
| Forward/reverse steering relation | Equal yaw, speed `+v` and `-v` | Reverse angle sign follows reverse model expectation |
| Brake assist monotonicity | Obstacle distance from far to near | Brake pressure does not decrease as obstacle gets closer |
| Speed command bounds | Any int32 speed | Output speed remains in configured safe range |
| Pressure command bounds | Any int32 kPa | SEB pressure raw remains in valid range |
| Counter wrap | Rolling counters near max | Wrap is correct and accepted by receiver contract |
| Mode validity | Any uint8 mode | Only 0, 1, 2 are accepted; invalid never enables Auto |
| DLC validity | Any DLC 0 through 15 | Receiver accepts only exact required DLC |

Fuzz tests to add:

| Fuzz target | Invalid inputs |
|-------------|----------------|
| CAN frame parser | Random ID, DLC, payload, bus, timestamp |
| Host drive parser | Bad speed, yaw, gear, DLC, endian, repeated/stale frames |
| Mode command parser | Invalid enum, wrong bus, stale command, repeated command |
| SES status parser | Bad angle, bad status, frozen counter, missing alignment |
| SEB status parser | Bad pressure/stroke mux, L2/L3 flags, bad checksum, frozen status |
| Diagnostic parser | Unknown flags, max payload, invalid counters |
| Generated scenario events | Random connect/disconnect/fault/mode timing |

Fuzz pass criteria:

| Criterion | Required result |
|-----------|-----------------|
| No crash/assert in release-like mode | Parser fails safe |
| No unsafe output | Invalid input cannot produce Auto authority or actuator command |
| Diagnostics where appropriate | Safety-relevant invalid input is visible |
| Reproducible failure | Seed and input frame saved |

### Numeric And Time Corner Tests

Numeric tests:

| Area | Cases |
|------|-------|
| Speed math | `INT32_MIN`, `INT32_MAX`, max reverse, max forward, zero, sign flip |
| Yaw math | Very small yaw, max yaw, sign flip, values that would saturate steering |
| Floating point | `NaN`, `+inf`, `-inf`, denormal-like tiny values where host-side code can represent them |
| CAN packing | Signed to unsigned conversion, endian sign extension, overflow before clamp |
| Pressure/stroke | Negative kPa, huge kPa, boundary raw values, pressure/stroke mode mux |
| Counter math | 0, 1, 14, 15, 255, wrap points, repeated values |

Time tests:

| Area | Cases |
|------|-------|
| Millisecond wrap | `uint32_t` wraparound for timeout comparisons |
| Microsecond wrap | `int64_t`/timer boundary where applicable |
| Zero delta | Two ticks with same timestamp |
| Large delta | Long pause between ticks |
| Jitter | Periodic frame jitter within and outside tolerance |
| Timeout boundary | Before, exactly at, and after each timeout threshold |
| Long uptime | Virtual 1 hour, 8 hour, 24 hour timing sanity where fast simulation allows |

### Resource And Queue Limit Tests

Software-only tests can simulate pressure on queues and buffers even without
hardware.

| Resource | Cases |
|----------|-------|
| CAN RX queue | Empty, one frame, burst, full, overflow, high-priority ESTOP during full queue |
| CAN TX queue | Normal load, saturated load, retry/fail, ESTOP fast path bypasses queue |
| Gateway queue | Allowed frames only, blocked frames, duplicate prevention, overflow behavior |
| Task watchdog | Alive counters update, task stuck, task slow, wraparound |
| Log buffer | Many faults in short time, no crash or blocking unsafe path |
| Memory allocation | If any dynamic allocation exists, fail allocation and ensure safe behavior |
| Ring/circular buffers | Empty, full, wrap, overwrite policy, stale reads |

### Reset, Boot, And Persistence Tests

Even in Phase 1, resets can be simulated by reinitializing software nodes while
the virtual bus and other nodes continue.

| Scenario | Required result |
|----------|-----------------|
| RT reset in Manual | RT returns safe; SYS handles heartbeat loss/recovery |
| RT reset in Auto | Outputs go safe; SYS detects loss; Auto not restored silently after RT returns |
| RT reset in Estop | ESTOP remains safe after RT returns |
| SYS reset in Manual | SYS returns Manual; RT does not trust stale mode |
| SYS reset in Auto | RT handles SYS heartbeat loss and safe takeover if configured |
| SYS reset in Estop | ESTOP remains safe and recovery requires explicit action |
| Host reset | RT handles heartbeat/command loss and reconnect correctly |
| Unit mimic reset | SES/SEB/MTR/PWT feedback disappears then returns; sync required before authority |
| Corrupt persisted mode | Boot falls back to Manual/safe |
| Corrupt persisted fault/crash flag | Diagnostic safe default; no Auto authority from stored data |

### Authority And Security-Like Tests

This is not cybersecurity certification, but Phase 1 should test authority rules
because any CAN node or tool can inject frames during bench testing.

| Scenario | Required result |
|----------|-----------------|
| Host sends `0x110` mode command on high bus | Ignored unless explicitly allowed by gateway policy |
| Random low-bus node sends Host drive `0x300` | Ignored on wrong bus |
| Random node sends RT drive `0x204` | SYS/MTR accepts only from expected authority if modeled |
| Replayed old valid `0x169` | Rolling counter/freshness rejects or stale logic handles |
| Replayed old valid `0x7B9` | Rolling counter/freshness rejects or stale logic handles |
| Duplicate `0x001` ESTOP | Idempotent; no echo storm |
| Unauthorized clear of ESTOP | Ignored; only documented recovery path works |
| Conflicting mode commands | Safest state wins; invalid/out-of-order cannot enable Auto |

### Diagnostics, Logging, And Observability Tests

Phase 1 should test not only behavior but also whether failures are observable.

| Fault/event | Required observability |
|-------------|------------------------|
| Bypass enabled | Log/report indicates exact bypass flags enabled |
| Host heartbeat lost | RT state, heartbeat health, diagnostic, or report shows loss |
| RT heartbeat lost | SYS status/diagnostic/report shows loss |
| SYS heartbeat lost | RT status/diagnostic/report shows loss or takeover |
| Bad checksum | Counter or diagnostic records rejection if designed |
| Wrong DLC | Rejection visible in test report at minimum |
| ESTOP | Mode/status/heartbeat/log all reflect ESTOP |
| Reconnect | Report shows disconnect duration and recovery path |
| Queue overflow | Diagnostic or test report captures dropped/overflow count |
| Generated scenario failure | Trace includes enough context to reproduce |

### Test Quality Checks

The Phase 1 gate should also test the tests themselves.

| Check | Required result |
|-------|-----------------|
| Stale executable detection | RT/SYS host tests are rebuilt before execution |
| No timestamp-only generated diffs | Gate fails or warns if generated files changed only by timestamp |
| Test independence | Running tests in different order gives same results |
| Failure sensitivity | Known intentional mutation causes relevant tests to fail |
| Coverage threshold | Safety-critical modules meet agreed line/branch/function coverage |
| Skipped test accounting | Skips are listed and classified as acceptable or blocker |
| Slow test classification | Long soak tests separated from fast gate but still required before Phase 2 |
| Report completeness | Missing logs, missing seed, or missing commit hash fails the gate |

## Gap Analysis Against Generic ESP32 Test Checklist

The generic ESP32/PlatformIO checklist includes many useful test categories. For
RT/SYS, some are required in Phase 1, some are required later in Phase 2 or
hardware phases, and some are not applicable unless the firmware architecture
changes. This section records what is still missing so the validation plan does
not silently skip important categories.

### Required For Phase 1 But Missing Or Incomplete

These should be implemented as software-only checks before Phase 1 can be called
complete.

| Category | Needed RT/SYS test | Current status | Proposed file or command |
|----------|--------------------|----------------|--------------------------|
| Clean build test | Build from scratch without relying on `.pio` cache or stale host `.exe` files | Missing as gate | `tools/phase1/clean_build_matrix.ps1` |
| Multi-environment build | RT/SYS `native`, `vehicle`, `bench` where valid | Partial manual runs only | `tools/phase1/build_matrix.ps1` |
| Debug/release-like build | Compile with debug symbols and optimized vehicle profile | Missing | Add debug envs or scripted `build_flags` variants |
| Library dependency test | PlatformIO dependencies resolve reproducibly | Missing as explicit check | `pio pkg list`, `pio run -e <env>` in clean CI cache |
| Header include test | Headers compile independently enough to catch missing/circular includes | Missing | `native-test/test/test_header_self_containment.cpp` or `tools/static/check_headers.ps1` |
| C++ standard compatibility | RT/SYS/shared compile under configured C++17 without compiler-specific surprises | Partial; native-test currently exposes MSVC issue | Build with PlatformIO GCC and CMake/MSVC or MinGW where supported |
| PlatformIO environment test | Every `platformio.ini` env builds or is explicitly excluded | Missing as gate | `tools/phase1/build_all_pio_envs.ps1` |
| `pio check` static analysis | PlatformIO static analysis for RT/SYS | Missing | `cd rt-esp32 && pio check`; `cd sys-esp32 && pio check` |
| Static analysis allowlist | Known findings documented, temporary, and reviewed | Missing | `tools/static/static_allowlist.txt` |
| Formatting test | Code formatting/style drift detected | Missing | Add `clang-format --dry-run` or project formatter policy |
| Duplicate/complexity checks | Detect repeated safety logic and overly complex functions | Missing | `cppcheck`, `lizard`, or `clang-tidy` complexity checks |
| Include dependency analysis | Detect poor coupling/circular includes in RT/SYS/shared | Missing | `include-what-you-use` if practical, or scripted include graph report |
| Config default test | Runtime/build defaults are safe: Manual, production run mode, bypasses off | Partial | `rt-esp32/test/test_rt_reset_defaults.cpp`, `sys-esp32/test/test_sys_reset_defaults.cpp` |
| Config validation test | Invalid run mode, invalid persisted mode/config, invalid bypass combination rejected or safe | Missing | `rt-esp32/test/test_rt_bypass_modes.cpp`, `sys-esp32/test/test_sys_bypass_modes.cpp` |
| Mock GPIO | Developer override pin, buttons, ESTOP, brake lever | Partial in SYS mode tests | `native-test/hal/gpio_stubs.cpp` expansion and SYS/RT bypass tests |
| Mock time | All timeout/staleness paths use deterministic fake time | Partial | Shared fake clock for RT/SYS/native/simulation |
| Mock NVS/storage | Crash flags/config load failures, missing/corrupt values | Missing | `native-test/test/test_storage_failure.cpp` |
| Mock CAN drivers | TWAI/MCP2515 TX/RX failure, bus-off, queue full, wrong bus | Partial virtual CAN exists | `native-test/test/test_virtual_can_faults.cpp` |
| Component tests | RT dispatch + physics + steering; SYS mode + safety + brake | Partial | `native-test/test/test_rt_sys_*_integration.cpp` |
| System tests | Full software boot workflow, normal operation, recovery, long virtual run | Partial simulation exists but failing | `simulation/tests/phase1/phase1.system-flow.test.ts` |
| Regression index | Every fixed QA/changelog bug maps to a test | Partial RT QA bugs only | `docs/validation/phase1-traceability-matrix.md` |
| Boundary tests | Every CAN field, mode, gear, pressure, speed, yaw, timeout boundary | Partial | Generated scenario matrix and property tests |
| Retry/backoff tests | Any retry/recovery logic for CAN driver recovery or communication paths | Missing/unclear | Add where retry logic exists; otherwise document not applicable |
| Queue logic tests | CAN RX/TX/gateway queues empty/full/overflow and ESTOP priority | Partial native-test | `native-test/test/test_queue_pressure.cpp` |
| FreeRTOS/concurrency simulation | Task alive counters, queue ordering, event races, watchdog logic | Partial native-test but build failing | Fix `native-test`; add scheduler/race tests |
| Memory host checks | ASan/UBSan where possible for host/native code | Missing | `tools/phase1/run_sanitizers.ps1` for host-testable C++ |
| Firmware size test | `.bin`, flash, RAM/static usage within limits | Missing | `pio run -t size`, saved in Phase 1 report |
| Partition fit test | Vehicle firmware fits selected ESP32 partition layout | Missing | PlatformIO size/partition check in build report |
| Protocol version test | CAN catalog version matches firmware/simulation/debug-tool | Missing | `tools/can/check_protocol_version.ps1` |
| API/command tests | Debug-tool injection/API commands cannot create invalid unsafe frames | Outside RT/SYS only but affects software validation | Backend/API tests mapped into Phase 1 optional suite |
| Logging/diagnostic tests | Safety faults visible in heartbeat/status/diag/log report | Partial | `simulation/tests/phase1/phase1.diagnostics.test.ts` |
| Performance timing tests | Virtual frame rates, boot time, response latency, function hot paths | Partial | `simulation/tests/phase1/phase1.timing-rate.test.ts` |
| Stress tests | High virtual CAN message rate, rapid mode changes, repeated reconnect/reset | Partial | `simulation/tests/phase1/phase1.stress.test.ts` |
| Soak tests | Long virtual run with seeded random events | Partial `soak-test` exists but not gate-ready | `simulation/tests/phase1/phase1.soak.seeded.test.ts` |
| Coverage reports | Line/function/branch coverage for host-testable safety modules | Missing | `tools/coverage/run_cpp_coverage.ps1`, `tools/coverage/run_ts_coverage.ps1` |
| CI report generation | CI uploads logs, reports, traces, coverage, binaries | Missing | `.github/workflows/phase1-*.yml` |
| Artifact generation | Firmware binaries and reports saved but marked software-only | Missing | CI artifact upload plus local `artifacts/phase1/<timestamp>/` |

### Required Later, Not Phase 1

These are important but cannot be fully validated in a no-hardware phase. They
should be planned for Phase 2 controller-only and later hardware phases.

| Category | Why not Phase 1 | Later phase |
|----------|-----------------|-------------|
| On-target ESP32 boot test | Needs real ESP32 board | Phase 2 controller-only |
| Unity on-device tests | Needs ESP32 flash/run/test loop | Phase 2 |
| Real FreeRTOS task timing | Host simulation is not exact ESP32 scheduling | Phase 2 |
| Real TWAI driver behavior | Needs ESP32 TWAI peripheral and CAN transceiver | Phase 2 |
| Real MCP2515 SPI behavior | Needs SPI bus and MCP2515 module | Phase 2 |
| Real GPIO input behavior | Needs actual pins/buttons/ESTOP wiring | Phase 2/3 |
| Real watchdog reset behavior | Needs ESP32 watchdog/reset reason | Phase 2 |
| Heap and stack high-water marks on chip | Host memory behavior is not ESP32 memory behavior | Phase 2 |
| NVS real flash behavior | Host mocks do not prove ESP32 NVS behavior | Phase 2 |
| Brownout reset handling | Needs power behavior/hardware reset reason | Phase 2/4 |
| CAN arbitration and ACK behavior | Virtual CAN cannot prove physical CAN electrical behavior | Phase 2 |
| Bus-off recovery on actual bus | Needs real CAN controller/transceiver and fault injection | Phase 2 |
| Physical actuator fail-safe | Needs SES/SEB/MTR hardware or unit mimics on CAN | Phase 3 |
| Full hardware integration | Needs complete vehicle hardware stack | Phase 4 |

### Not Currently Applicable To RT/SYS Unless Scope Changes

These are common ESP32 tests but are not currently core RT/SYS requirements based
on the current architecture. If Wi-Fi, BLE, OTA, filesystem logging, or cloud
connectivity are added to RT/SYS, move them into the required test set.

| Category | Current decision | Revisit if |
|----------|------------------|------------|
| Wi-Fi connect/reconnect | Not applicable to RT/SYS control firmware | RT/SYS gains Wi-Fi connectivity |
| MQTT tests | Not applicable to RT/SYS | RT/SYS publishes/subscribes to MQTT |
| HTTP client/server tests | Not applicable to RT/SYS | RT/SYS hosts API or calls backend |
| BLE tests | Not applicable to RT/SYS | BLE control/config is added |
| TLS/certificate tests | Not applicable to RT/SYS | Network security added |
| OTA update tests | Not currently documented as RT/SYS release path | OTA update support is added |
| SPIFFS/LittleFS file tests | Not applicable unless file logging/config is added | Filesystem use added |
| SD card tests | Not applicable | SD logging added |
| Deep sleep tests | Not applicable for always-on safety controllers | Low-power mode added |
| Captive portal tests | Not applicable | Wi-Fi provisioning added |
| Web UI route/form tests on ESP32 | Not applicable to RT/SYS; debug-tool has separate UI tests | ESP32-hosted UI added |
| Cloud unavailable tests | Not applicable | RT/SYS cloud path added |

### Practical Minimum Missing Set Before Phase 2

From the full checklist, the minimum missing RT/SYS Phase 1 set that should be
implemented before controller-only testing is:

| Priority | Missing item | Why it is minimum |
|----------|--------------|-------------------|
| P0 | One Phase 1 gate runner | Prevents partial/manual validation from being mistaken as complete |
| P0 | Clean RT/SYS native and vehicle build matrix | Must know firmware and host tests compile reproducibly |
| P0 | Fix existing `native-test` failures | Existing safety/integration tests cannot be ignored |
| P0 | Fix existing simulation failures | Scenario validation depends on trustworthy simulation |
| P0 | Protocol drift check | CAN contract mismatch invalidates many tests |
| P0 | Shared invariant checker | Ensures scenarios prove safety, not only no-crash |
| P0 | Mode and bypass matrix | Manual/Auto/Estop and bypasses change safety authority |
| P0 | Fault/disconnect/reconnect matrix | Loss and recovery behavior is central to RT/SYS safety |
| P1 | Static analysis gate | Finds simple embedded bugs early |
| P1 | Firmware size/partition report | Prevents build from exceeding hardware limits |
| P1 | Coverage report for safety-critical host-testable modules | Shows what remains untested |
| P1 | Traceability matrix | Ensures every safety requirement has automated evidence |
| P2 | Mutation smoke tests | Proves key tests actually catch broken safety logic |
| P2 | Long seeded soak tests | Catches rare event ordering and slow leaks in virtual time |

## Existing Software Test Assets

Existing RT-local tests:

| File | Purpose |
|------|---------|
| `rt-esp32/test/.stale/test_qa_bugs.cpp` | Regression tests for RT QA bugs, including reverse steering and SEB alignment bit |
| `rt-esp32/test/.stale/test_physics_0_0_4.cpp` | Physics model clamps, following error threshold, obstacle brake curve |
| `rt-esp32/test/.stale/test_pid_controller.cpp` | PID controller behavior |
| `rt-esp32/test/.stale/test_heartbeat.cpp` | RT heartbeat counters and frame encoding |
| `rt-esp32/test/.stale/test_steering_control.cpp` | Steering ESTOP behavior |
| `rt-esp32/test/test_all_signals_native.cpp` | CAN signal coverage |

Existing SYS-local tests:

| File | Purpose |
|------|---------|
| `sys-esp32/test/test_mode_manager.cpp` | Mode toggles, ESTOP exit, long-press, debounce |
| `sys-esp32/test/test_safety_monitor.cpp` | RT heartbeat timeout, frozen counter, startup grace, ESTOP and brake lever flags |
| `sys-esp32/test/test_brake_priority.cpp` | Brake priority, `auto_brake`, kPa to SEB raw conversion, rolling counter |
| `sys-esp32/src/test_sys_qa.cpp` | SYS QA test source |
| `sys-esp32/src/native_entry.cpp` | SYS PlatformIO native checks |

Existing `native-test` assets:

| File | Purpose |
|------|---------|
| `native-test/test/test_protocol_roundtrip.cpp` | CAN protocol roundtrip |
| `native-test/test/test_checksum_full.cpp` | Checksum coverage |
| `native-test/test/test_rt_can_rx_router.cpp` | RT CAN RX routing |
| `native-test/test/test_rt_can_dispatch.cpp` | RT CAN dispatch |
| `native-test/test/test_rt_safety_monitor.cpp` | RT safety monitor |
| `native-test/test/test_sys_can_dispatch.cpp` | SYS CAN dispatch |
| `native-test/test/test_gateway_forwarding.cpp` | Gateway forwarding |
| `native-test/test/test_heartbeat_recovery.cpp` | Heartbeat recovery |
| `native-test/test/test_dual_heartbeat.cpp` | Dual heartbeat independence |
| `native-test/test/test_watchdog_wraparound.cpp` | Watchdog wraparound |
| `native-test/test/test_task_watchdog.cpp` | Task watchdog behavior |
| `native-test/test/test_signal_chains.cpp` | Signal chains |
| `native-test/test/test_safety_features.cpp` | Safety features |
| `native-test/test/test_component_io.cpp` | Component I/O |
| `native-test/test/test_components.cpp` | Component behavior |
| `native-test/test/test_dlc_consistency.cpp` | DLC consistency |
| `native-test/test/test_dlc_generator.py` | Generator DLC consistency |

Existing simulation scenario assets:

| File | Purpose |
|------|---------|
| `simulation/tests/scenarios/all-scenarios.test.ts` | Runs named scenarios such as drive-forward, ESTOP flow, heartbeat timeout, steering sync |
| `simulation/tests/scenarios/varied-conditions.test.ts` | Manual/Auto/ESTOP, speed ranges, steering, braking, faults, obstacles, edge cases |
| `simulation/tests/scenarios/heartbeat-timeout.test.ts` | Host heartbeat loss assisted stop case |
| `simulation/tests/scenarios/mode-transition.test.ts` | Mode transition race checks |
| `simulation/tests/scenarios/soak-test.test.ts` | Soak testing |
| `simulation/tests/integration/bus-routing.test.ts` | Bus routing |
| `simulation/tests/integration/rt-to-low-bus.test.ts` | RT to low bus integration |
| `simulation/tests/unit/can-encoding.test.ts` | CAN encoding expectations |
| `simulation/tests/unit/can-validator.test.ts` | CAN validation |
| `simulation/tests/unit/rt-kinematics.test.ts` | RT kinematics |
| `simulation/tests/unit/rt-steering.test.ts` | RT steering |
| `simulation/tests/unit/sys-safety.test.ts` | SYS safety |
| `simulation/tests/unit/sys-brake.test.ts` | SYS brake |
| `simulation/tests/unit/seb.test.ts` | SEB behavior |
| `simulation/tests/unit/epsc.test.ts` | EPS-C behavior |
| `simulation/tests/unit/mtr-motor.test.ts` | MTR behavior |
| `simulation/tests/unit/mode-transition-can.test.ts` | Mode transition CAN behavior |

## Required Phase 1 Gate Command

Phase 1 should eventually run from one command, for example:

```powershell
.\tools\phase1-software-gate.ps1
```

The gate should:

| Step | Required action |
|------|-----------------|
| 1 | Record git commit hash and dirty status |
| 2 | Build RT native with `pio run -e native` |
| 3 | Build SYS native with `pio run -e native` |
| 4 | Build RT vehicle firmware with `pio run -e vehicle` |
| 5 | Build SYS vehicle firmware with `pio run -e vehicle` |
| 6 | Run RT host tests with `pio test -e native` |
| 7 | Run SYS host tests with `pio test -e native` |
| 8 | Build and run `native-test` CMake suite |
| 9 | Run `simulation` TypeScript tests |
| 10 | Run generated hundreds-case scenario matrix |
| 11 | Write logs and machine-readable summary |
| 12 | Fail the gate if any required check fails or if required coverage is missing |

## How Validation Is Performed

Validation is not just running commands and looking for green output. For Phase 1,
validation is performed as a controlled workflow with explicit inputs, execution,
evidence, decision rules, and failure triage.

| Step | What happens | Required output |
|------|--------------|-----------------|
| 1. Freeze test input | Record git hash, branch, dirty status, tool versions, build profiles, bypass config, and scenario seeds | `phase1-report.json` metadata section |
| 2. Build software | Compile RT/SYS native and vehicle targets, then rebuild host/native tests from source | Build logs and binary paths |
| 3. Run deterministic unit tests | Run RT, SYS, shared protocol, and native unit tests with fake time and fake hardware | Per-suite pass/fail output |
| 4. Run protocol checks | Validate CAN ID, DLC, endian, scale, checksum, rolling counter, bus placement, generated-code drift | Protocol validation report |
| 5. Run component/integration tests | Run virtual RT/SYS/Host/unit interactions over software CAN | Virtual CAN traces and invariant results |
| 6. Run generated scenario matrix | Execute hundreds of deterministic maneuver, mode, fault, reconnect, bypass, and boundary cases | Scenario count, pass/fail count, failed scenario IDs |
| 7. Run static/coverage checks | Analyze code without hardware and measure test coverage for host-testable modules | Static analysis and coverage reports |
| 8. Apply pass/fail rules | Gate evaluates failures, skips, missing evidence, coverage gaps, and dirty/generated diffs | Final PASS/FAIL decision |
| 9. Save evidence | Store logs, traces, reports, seeds, and failed input frames under one artifact folder | Reproducible artifact bundle |
| 10. Triage failures | First failure is classified as code bug, test bug, environment issue, or missing coverage | Failure classification and next action |

The Phase 1 gate should produce a single final result:

| Result | Meaning |
|--------|---------|
| PASS | All required tests passed, required evidence exists, no blocker skips, no unsafe invariant failures |
| FAIL | At least one required test failed, evidence is missing, required suite did not run, or a safety invariant failed |
| BLOCKED | Tool/environment issue prevented validation, for example compiler missing or PlatformIO lock prevented build |
| INCOMPLETE | Tests ran but required coverage, scenario counts, bypass matrix, or traceability are missing |

Only `PASS` allows moving from Phase 1 to Phase 2. `FAIL`, `BLOCKED`, and
`INCOMPLETE` all block controller-only testing until resolved or explicitly
accepted as a documented risk by the responsible engineer.

### Validation Inputs

Every validation run must declare its inputs up front.

| Input | Example | Why it is required |
|-------|---------|--------------------|
| Git hash | `abc1234` | Makes the result reproducible |
| Dirty status | clean/dirty plus file list | Prevents untracked local changes from being hidden |
| Build profiles | RT native, RT vehicle, SYS native, SYS vehicle | Shows exactly what was compiled |
| Run mode | production, prototype, software simulation | Bypass expectations depend on this |
| Bypass flags | EPS, SEB, MTR, bench solo | Bypass state changes safety assumptions |
| Scenario seeds | numeric seeds | Randomized failures must be replayable |
| Tool versions | PlatformIO, compiler, Node, CMake | Tool differences can affect output |
| CAN catalog version | generated catalog hash or timestamp | Ensures all tools test the same protocol |

### Validation Outputs

Every validation run must save these outputs.

| Output | Required content |
|--------|------------------|
| `phase1-report.json` | Machine-readable result, inputs, suite results, scenario counts, failures, seeds |
| `phase1-report.md` | Human-readable summary and blocker list |
| Build logs | Full stdout/stderr for each build command |
| Test logs | Full stdout/stderr for each test command |
| Static analysis report | Tool findings and allowlist usage |
| Coverage report | Line/branch/function coverage where available |
| Virtual CAN traces | Failed scenario traces and selected passing traces |
| Invariant failures | Exact invariant name, time, frame, expected, actual |
| Skipped tests | Every skip with reason and blocker/non-blocker classification |

### Validation Decision Rules

These rules decide whether Phase 1 passes.

| Rule | Decision |
|------|----------|
| Required build fails | FAIL |
| Required test executable is stale or not rebuilt | FAIL |
| Required test suite did not run | INCOMPLETE |
| Any safety invariant fails | FAIL |
| Any scenario matrix minimum count is not met | INCOMPLETE |
| Any blocker requirement has no mapped test | INCOMPLETE |
| Required evidence file missing | INCOMPLETE |
| Static analysis finds blocker issue | FAIL |
| Coverage below required threshold for safety-critical module | FAIL or INCOMPLETE, depending on configured policy |
| Timestamp-only generated diff appears | WARN unless semantic protocol drift exists |
| Semantic generated-code/protocol drift appears | FAIL |
| Dirty worktree exists | WARN for development runs; FAIL for release-candidate runs unless documented |

### Failure Triage

Every failure should be classified before fixing.

| Classification | Meaning | Example | Next action |
|----------------|---------|---------|-------------|
| Code bug | Product logic is wrong | Estop does not override Auto command | Fix code, add/keep regression test |
| Test bug | Test expectation is stale or wrong | Test expects old heartbeat DLC | Fix test after confirming contract |
| Contract drift | Firmware, simulator, generated code, or docs disagree | `0x169` layout differs between TS and C++ | Update source of truth and regenerate |
| Environment issue | Tooling prevented valid run | PlatformIO file lock | Fix environment, rerun gate |
| Missing coverage | No automated test exists for required behavior | Reconnect during ESTOP untested | Add test before Phase 2 |
| Known accepted risk | Gap is documented and intentionally accepted | Noncritical coverage below target | Record owner, reason, expiration |

### Validation Of The Tests Themselves

The validation system must also check that the tests are meaningful.

| Check | Required behavior |
|-------|-------------------|
| Negative control | Selected tests fail when safety logic is intentionally mutated in a temp copy |
| Invariant self-test | Invariant checker is tested with safe and intentionally unsafe traces |
| Replay self-test | A saved failing seed/trace reproduces the same failure |
| Test isolation | Test order does not affect results |
| Stale binary detection | Runner rebuilds or rejects old executables before running them |
| Skip accounting | Skipped tests are visible and cannot silently count as pass |

### Human Review And Sign-Off

Automated tests provide evidence, but the phase transition should still require a
human review of the report.

| Review item | Required check |
|-------------|----------------|
| Final result | `PASS` only for Phase 2 entry |
| Dirty status | Clean for release-candidate validation, or explicitly documented |
| Bypass state | Vehicle/production validation has no unintended bypass enabled |
| Failed/skipped tests | None that affect safety-critical behavior |
| Known gaps | No blocker gaps remain open |
| Traceability | Safety requirements have mapped automated tests |
| Evidence | Logs, traces, reports, and seeds are saved and reproducible |

## CI Build And Test Strategy

Phase 1 validation should run both locally and in CI. Local runs help developers
iterate. CI prevents incomplete or unsafe changes from being merged and preserves
evidence for release-candidate decisions.

CI should be split into tiers so fast checks run on every PR while expensive
scenario/soak checks run on schedule or before Phase 2.

### CI Tiers

| CI tier | Trigger | Purpose | Expected duration | Required before merge |
|---------|---------|---------|-------------------|-----------------------|
| Fast PR gate | Every pull request and push to active branch | Catch compile, unit, protocol, static, and small simulation failures quickly | Short | Yes |
| Full PR gate | Pull request labeled for firmware/safety or manually requested | Run broader Phase 1 checks before reviewing safety-sensitive changes | Medium | Yes for RT/SYS/shared/CAN changes |
| Nightly gate | Scheduled daily/nightly | Run full generated scenario matrix, coverage, and longer simulation sets | Long | No, but failures must be triaged |
| Release-candidate gate | Before Phase 2/controller testing or vehicle-related release | Produce full auditable Phase 1 evidence bundle | Long | Yes |
| Toolchain drift gate | Scheduled weekly or dependency update | Detect build/tool/version breakage | Medium | No, but blocks dependency upgrades if failing |

### Fast PR Gate

The fast PR gate should run on every PR that touches source, tests, CAN catalog,
or validation docs.

Required jobs:

| Job | Commands/checks | Failure policy |
|-----|-----------------|----------------|
| Git hygiene | Check generated files, line endings if configured, no timestamp-only generated drift | Fail on semantic drift; warn or fail on noisy generated timestamp policy |
| RT native build | `cd rt-esp32 && pio run -e native` | Fail |
| SYS native build | `cd sys-esp32 && pio run -e native` | Fail |
| RT host tests | Rebuild and run RT local host tests | Fail |
| SYS host tests | Rebuild and run SYS local host tests | Fail |
| Protocol tests | Shared CAN protocol/roundtrip/checksum/DLC checks | Fail |
| Simulation fast tests | Unit tests and selected fast scenarios | Fail |
| Static TypeScript | `tsc --noEmit` or package `npm run check` where available | Fail for touched packages |
| Static C/C++ light | `cppcheck` or configured lightweight checks | Fail on blocker findings |

Fast PR gate should not rely on old `.exe` files. It must rebuild host tests from
source every time.

### Full PR Gate

The full PR gate is required for changes touching:

| Path or area | Why full PR gate is required |
|--------------|------------------------------|
| `rt-esp32/**` | RT controls gateway, physics, steering, safety outputs |
| `sys-esp32/**` | SYS controls mode, safety, body/brake/motor paths |
| `protocol/**` | CAN contract affects every ECU and simulator |
| `simulation/**` | Phase 1 scenario evidence depends on it |
| `native-test/**` | Native safety/integration test coverage depends on it |
| `docs/validation/**` | Changes release gate expectations |
| Build scripts/tooling | Can change what is actually validated |

Additional full PR jobs:

| Job | Commands/checks | Failure policy |
|-----|-----------------|----------------|
| RT vehicle build | `cd rt-esp32 && pio run -e vehicle` | Fail |
| SYS vehicle build | `cd sys-esp32 && pio run -e vehicle` | Fail |
| Native-test CMake | Configure/build/run `ctest` | Fail |
| Simulation integration/scenarios | `npm test` or targeted integration/scenario commands | Fail |
| Small generated matrix | Minimum representative generated scenario set, e.g. 50-100 cases | Fail |
| Bypass matrix fast subset | Production-off, software-on, EPS/SEB/MTR key cases | Fail |
| Protocol drift | YAML/generated C++/TS/protocol semantic comparison | Fail |

### Nightly Gate

Nightly runs should be broader and more expensive than PR checks.

Nightly jobs:

| Job | Scope | Failure policy |
|-----|-------|----------------|
| Full generated matrix | All documented minimum generated cases, currently 420+ plus bypass cases | Fail nightly, create issue/task for triage |
| Seeded soak tests | Long virtual random but deterministic sequences | Fail and preserve seed/trace |
| Coverage | C++ host/native and TypeScript simulation coverage | Fail if below release threshold; warn if below development threshold |
| Static analysis full | Full `clang-tidy`/`cppcheck`/TS checks | Fail on blocker; warn on non-blocker |
| Mutation smoke | Small selected mutation set for safety invariants | Fail if mutation survives unexpectedly |
| Dependency/toolchain check | Current pinned PlatformIO/Node/CMake/compiler versions | Fail if build impossible |

Nightly failures should not be ignored. They should produce a triage item with:

| Field | Required content |
|-------|------------------|
| Failing job | CI job name and URL |
| Commit | Hash under test |
| Failure class | Code bug, test bug, contract drift, environment, missing coverage |
| Artifact path | Logs, traces, coverage, seeds |
| Owner | Person responsible for triage |
| Decision | Fix immediately, accept temporarily, or block release |

### Release-Candidate Gate

The release-candidate gate is the CI version of the full Phase 1 validation. It
must run before controller-only Phase 2 testing starts.

Release-candidate requirements:

| Requirement | Policy |
|-------------|--------|
| Worktree state | Clean, reproducible commit; no undocumented local diff |
| RT/SYS native builds | Pass |
| RT/SYS vehicle builds | Pass |
| RT/SYS local host tests | Pass after rebuild |
| Native-test suite | Pass |
| Simulation suite | Pass |
| Generated scenario matrix | Full minimum count passes |
| Bypass matrix | Full bypass matrix passes |
| Static analysis | No blocker findings |
| Coverage | Meets configured safety-critical thresholds or documented blocker |
| Protocol drift | No semantic drift |
| Evidence bundle | Uploaded and retained |
| Human review | Required before Phase 2 entry |

Release-candidate CI must upload:

| Artifact | Retention |
|----------|-----------|
| `phase1-report.json` | Long-term/release artifact |
| `phase1-report.md` | Long-term/release artifact |
| Build logs | Long-term/release artifact |
| Test logs | Long-term/release artifact |
| Virtual CAN traces for failures and selected pass cases | Long-term/release artifact |
| Coverage reports | Long-term/release artifact |
| Static analysis reports | Long-term/release artifact |
| Firmware binaries if built | Long-term/release artifact, marked software-only not vehicle-approved until later phases |

### CI Workflow Files

Proposed workflow files if using GitHub Actions:

| File | Purpose |
|------|---------|
| `.github/workflows/phase1-fast.yml` | Fast PR gate |
| `.github/workflows/phase1-full.yml` | Full PR/manual gate |
| `.github/workflows/phase1-nightly.yml` | Nightly matrix, soak, coverage, full static analysis |
| `.github/workflows/phase1-release-candidate.yml` | Auditable Phase 1 gate before Phase 2 |
| `.github/workflows/can-contract.yml` | Optional separate CAN generation/protocol drift gate |

If another CI system is used, keep the same logical jobs and artifact outputs.

### CI Caching And Reproducibility

CI may cache dependencies, but it must not cache test results or stale binaries in
a way that hides failures.

| Cache | Allowed | Rule |
|-------|---------|------|
| PlatformIO packages | Yes | Cache by OS and lock/tool version |
| Node modules | Yes | Cache by lockfile hash |
| CMake dependencies | Yes | Cache by CMake/toolchain version |
| Built test executables | No for gate result | Tests must be rebuilt in the job that runs them |
| Generated CAN files | No as source of truth | Regenerate/check semantic diff from committed YAML/source |
| Test reports | Uploaded only | Never reused as pass evidence for another commit |

CI should record exact versions:

| Tool | Required in report |
|------|--------------------|
| OS image | Yes |
| PlatformIO | Yes |
| ESP-IDF/platform package | Yes |
| C/C++ compiler | Yes |
| CMake | Yes |
| Node/npm | Yes |
| Python | Yes |
| Static analysis tools | Yes |

### CI Pass/Fail Policy

| Situation | PR gate | Nightly | Release candidate |
|-----------|---------|---------|-------------------|
| Build fails | Fail | Fail | Fail |
| Unit/native test fails | Fail | Fail | Fail |
| Protocol semantic drift | Fail | Fail | Fail |
| Timestamp-only generated diff | Warn or fail based on repo policy | Warn | Fail if not documented/cleaned |
| Static analysis blocker | Fail | Fail | Fail |
| Static analysis non-blocker | Warn | Warn or fail by threshold | Must be triaged |
| Coverage below target | Warn for early rollout | Fail or warn by configured threshold | Fail unless accepted risk |
| Scenario matrix incomplete | Fail for required subset | Fail | Fail |
| Soak failure | Not usually run | Fail | Fail |
| Missing artifact | Fail | Fail | Fail |
| Dirty local diff | Not applicable in CI | Not applicable | Not applicable; release candidate must be a clean commit |

### CI And Local Gate Relationship

Local and CI validation should use the same underlying scripts. CI should not
duplicate test logic in YAML that differs from local behavior.

Recommended pattern:

| Layer | Responsibility |
|-------|----------------|
| `tools/phase1-software-gate.ps1` | Defines actual validation logic |
| CI workflow YAML | Installs tools, restores caches, calls gate script, uploads artifacts |
| `tools/phase1/phase1_config.json` | Defines suites, thresholds, timeouts, scenario counts |
| Docs | Explain what the gate means and how to triage failures |

This keeps local developer runs and CI runs consistent.

## Required Evidence Per Phase 1 Run

| Artifact | Required content |
|----------|------------------|
| Commit metadata | Git hash, branch, dirty status, timestamp |
| Build logs | RT/SYS native and vehicle build output |
| Host test logs | RT and SYS local test output |
| Native-test logs | CMake build and `ctest` output |
| Simulation logs | Vitest output and failed scenario details |
| Scenario report | Number of generated cases, pass/fail count, seed values for randomized tests |
| CAN trace artifacts | Virtual CAN frame traces for scenario failures and selected pass cases |
| Coverage inventory | Which requirements and scenario groups were covered |
| Known skips | Explicit list of skipped tests and why they are acceptable or blocking |

## Test Oracle And Invariant Checker

Hundreds of tests are only useful if every test asserts safety properties. Phase
1 needs a shared invariant checker used by every software scenario.

Global invariants:

| Invariant | Rule |
|-----------|------|
| Auto authority | Autonomous actuator output is allowed only in `Auto` with fresh valid prerequisites |
| ESTOP priority | `Estop` overrides drive, steering, brake, mode, reconnect, stale command, and obstacle events |
| Manual silence | `Manual` does not emit unsafe autonomous drive, steering, or brake commands |
| No stale actuation | Stale command data cannot keep speed, steering, brake, or gear authority alive |
| Bounded output | Speed, steering angle, pressure, stroke, gear, and mode stay within valid ranges |
| Valid CAN security | Outbound safety frames have valid DLC, checksum, rolling counter, enable bits, and alignment bits |
| No unsafe reconnect | Reconnected nodes do not immediately restore Auto output without fresh valid state |
| Manual-first recovery | Recovery from `Estop` or safety fault returns to `Manual`, not directly to `Auto` |
| Rate compliance | Periodic frames stay within allowed virtual timing tolerance |
| Diagnostic visibility | Safety-affecting faults are visible in health flags, status frames, or diagnostics |
| Wrong-bus isolation | A valid frame on the wrong bus cannot grant authority or bypass safety logic |
| Invalid enum safety | Invalid mode, gear, state, or control enum cannot enable Auto or unsafe output |
| No duplicate side effects | Duplicate frames do not cause repeated unsafe state transitions |
| Freshness before authority | A command must be recent and valid before affecting outputs |

Suggested invariant checker outputs:

| Field | Meaning |
|-------|---------|
| `scenarioId` | Unique generated scenario identifier |
| `seed` | Random seed if applicable |
| `timeMs` | Virtual time when invariant failed |
| `bus` | Bus where failing frame/event occurred |
| `canId` | CAN ID involved, if any |
| `mode` | Current vehicle mode |
| `failure` | Human-readable invariant failure |
| `tracePath` | Saved trace path for reproduction |

## Modes To Test

Vehicle modes from `protocol/generated/cpp/protocol.h`:

| Mode | Value | Meaning |
|------|-------|---------|
| Manual | 0 | Human/manual control is authoritative; autonomous actuator commands must be silent or safe-zeroed |
| Auto | 1 | Host/RT autonomous command path may control steering, brake, and motor within limits |
| Estop | 2 | Emergency state; outputs go to safest available command |

Mode-manager tests required:

| Scenario | Required result |
|----------|-----------------|
| Boot default | SYS starts in Manual, never Auto |
| Manual to Auto | Mode button release changes Manual to Auto after debounce |
| Auto to Manual | Mode button release changes Auto to Manual after debounce |
| Manual to Estop | ESTOP input forces Estop immediately |
| Auto to Estop | ESTOP input forces Estop immediately |
| Estop to Manual | START or documented long-press exits only to Manual |
| Estop to Auto | Direct Estop to Auto is blocked |
| Rapid bounce | Debounce prevents repeated unintended toggles |
| CAN set Manual | Accepted if contract allows |
| CAN set Auto | Accepted only under allowed rules |
| CAN set Estop | Rejected unless specifically designed as software ESTOP path |
| Invalid mode value | Ignored or forced safe; never treated as Auto |

RT behavior per mode:

| Mode | Required RT behavior |
|------|----------------------|
| Manual | Suppress host autonomous drive, steering, RT SEB auto request; report Manual |
| Auto | Accept fresh valid Host commands, apply physics/clamps, emit bounded actuator requests |
| Estop | Zero drive, enter steering ESTOP behavior, send safe brake/takeover if configured, report Estop/safe fault |
| Invalid | Treat as Manual or fault-safe; never enable Auto outputs |
| Stale | Hold last safe state or degrade to safe state according to timeout rules |

SYS behavior per mode:

| Mode | Required SYS behavior |
|------|-----------------------|
| Manual | Manual indicators active; manual gear/throttle path allowed if configured; autonomous commands suppressed |
| Auto | Auto indicator active; CAN-controlled path allowed only with healthy prerequisites |
| Estop | Brake lamp active; throttle/motor cut; gear safe/neutral; brake safe command |
| Invalid | Ignored or forced safe; never drives Auto outputs |

RT/SYS interaction tests:

| Scenario | Required result |
|----------|-----------------|
| SYS sends Manual | RT state follows Manual; no autonomous actuator requests |
| SYS sends Auto | RT follows Auto only with valid safety preconditions |
| SYS sends Estop | RT enters ESTOP behavior and reports safe state |
| RT reports Manual while SYS Manual | SYS remains consistent; no false fault |
| RT reports Auto while SYS Auto | SYS allows Auto only while RT heartbeat and state are healthy |
| RT reports fault while SYS Auto | SYS suppresses unsafe Auto path or exits according to design |
| RT heartbeat lost in Auto | SYS safe response within timeout |
| SYS heartbeat lost in Auto | RT safe takeover behavior within timeout |
| Out-of-order mode commands | Safest applicable state wins |

CAN-output checks by mode:

| Frame | Manual | Auto | Estop |
|-------|--------|------|-------|
| `0x110` SYS_MODE_CMD | mode=0 when commanded | mode=1 when commanded | mode=2 only through ESTOP path |
| `0x210` RT_STATE_RPT | reports Manual and safe state | reports Auto and active/safe state | reports Estop/fault/safe state |
| `0x204` RT_DRIVE_CMD | absent or speed=0 | bounded fresh speed/gear | speed=0 and safe gear |
| `0x205` RT_BRAKE_CMD | release or manual-safe value | bounded requested/obstacle brake | safe max or documented ESTOP brake |
| `0x169` VCU_SES_REQ | absent or safe hold/listen | bounded angle with valid checksum/counter | ramp/hold/silent ESTOP behavior |
| `0x7B9` VCU_SEB_REQ | release/zero unless takeover required | bounded pressure/stroke with alignment bit | safe takeover brake command |
| `0x7FD` RT_HEARTBEAT | Manual health flags | `mode_auto` bit set | `estop_active` bit set |
| `0x7FE` SYS_HEARTBEAT | Manual health flags | `mode_auto` bit set | `estop_active` bit set |

## User Scenario Catalog

Nominal scenarios:

| Scenario | Required checks |
|----------|-----------------|
| Start in Manual, no command | No autonomous output; heartbeats/status continue |
| Switch Manual to Auto while stopped | RT accepts mode only after SYS command; no lurch |
| Move forward straight | Positive speed command produces bounded `0x204`; steering near zero |
| Move forward then stop | Speed goes to zero; no stale speed remains |
| Move forward and take gentle left turn | Steering sign and magnitude correct |
| Move forward and take gentle right turn | Steering mirrors left-turn case |
| Hard turn at low speed | Steering bounded by low-speed hard limit |
| Hard turn at high speed | Dynamic angle clamp limits steering |
| Reverse straight | Reverse speed and gear encoded correctly; no forward lurch |
| Reverse left turn | Reverse steering sign correct |
| Reverse right turn | Reverse steering sign mirrors reverse left |
| Stop, reverse, then forward | Gear and speed transitions do not create unsafe mixed commands |
| Brake while moving forward | Brake command bounded; speed/brake do not conflict unsafely |
| Brake while turning | Steering remains bounded while brake applies |
| Obstacle far away | No brake assist beyond normal request |
| Obstacle approaching | Brake assist increases with distance curve |
| Obstacle emergency distance | ESTOP or max safe brake path activates |
| Auto to Manual while moving | Autonomous outputs safe-zero within timeout |
| Manual to Auto while moving | Auto authority only if preconditions valid |
| ESTOP while stopped | ESTOP state entered without bad frames or corruption |
| ESTOP while moving straight | Speed zero; brake safe command; heartbeats reflect ESTOP |
| ESTOP while turning | Steering ESTOP behavior follows ramp/hold/silent rules |
| ESTOP while reversing | Reverse command cancelled; brake/neutral safe |
| Recover from ESTOP | Recovery exits only to Manual; Auto requires separate command |

Command boundary scenarios:

| Input | Values |
|-------|--------|
| Speed | 0, creep, nominal, max allowed, above max, reverse, below reverse max |
| Yaw rate | 0, small left/right, hard left/right, above limit, sign changes |
| Brake pressure | 0, small, nominal, max allowed, above max, negative/invalid raw |
| Obstacle distance | none, clear, assist threshold, emergency threshold, invalid negative, stale |
| Gear | N, D, R, invalid enum, gear change while speed nonzero |
| Mode | Manual, Auto, Estop, invalid enum, stale command, repeated command |
| Time | t=0 event, just before timeout, exactly at timeout, just after timeout, long soak |

## Connection, Disconnection, And Reconnection Scenarios

Controller and Host scenarios:

| Scenario | Required behavior |
|----------|-------------------|
| Host never connected | RT stays safe; no autonomous output without valid Host command/heartbeat |
| Host connects after boot | RT accepts only after valid heartbeat and command sequence |
| Host disconnects in Manual | No unsafe change; diagnostic records missing Host if applicable |
| Host disconnects in Auto | RT assisted stop or safe fallback within timeout |
| Host reconnects after timeout | No jump back to Auto output without fresh valid command and mode authority |
| RT missing at SYS boot | SYS safe during startup grace, then faults if RT stays absent |
| RT connects after SYS boot | SYS accepts RT heartbeat and clears missing-RT state by design |
| RT disconnects in Auto | SYS safe response and expected ESTOP/brake behavior |
| RT reconnects after fault | Explicit operator/mode action required; no automatic Auto |
| SYS missing at RT boot | RT remains safe and does not trust stale SYS mode data |
| SYS disconnects in Auto | RT brake takeover activates within timeout if configured |
| SYS reconnects after fault | RT/SYS resync mode and heartbeat before enabling Auto output |

External-unit scenarios:

| Unit | Disconnect behavior | Reconnect behavior |
|------|---------------------|--------------------|
| SES/EPS-C | RT detects missing `0x201`; steering safe state | Boot sync/listen resumes before steering active |
| SEB | SYS/RT detects missing `0x721`; brake degraded or takeover safe | Alignment/sync required before normal brake control |
| MTR | SYS detects missing motor feedback if modeled; throttle safe | Gear/throttle resumes only after healthy feedback |
| PWT | Gateway/powertrain status missing reported; no unsafe dependent output | Heartbeat/status restored before dependent control |
| High CAN bus | Host commands disappear; RT high-bus behavior safe | Host path resumes only with fresh valid frames |
| Low CAN bus | RT/SYS communication fails safe; no stale cross-node mode trusted | Heartbeats and mode handshakes re-establish before Auto output |

Connection fault variants:

| Variant | Required check |
|---------|----------------|
| Clean disconnect | Expected frames stop; timeout response occurs once |
| Intermittent disconnect | Drop/recover cycles do not cause unsafe output or state oscillation |
| Frozen node | Same counter or payload is unhealthy, not connected |
| Reconnect with old counter | Counter rollback/reuse rejected or safe-handled |
| Reconnect with invalid mode | Invalid mode cannot enable Auto |
| Reconnect during ESTOP | ESTOP remains latched until explicit recovery |
| Reconnect during braking | Brake command does not release unexpectedly |
| Reconnect during steering ramp | Steering remains bounded and deterministic |

## CAN Fault And Data-Corruption Scenarios

| Fault | Frames to apply to | Required behavior |
|-------|--------------------|-------------------|
| Bad DLC | `0x110`, `0x204`, `0x205`, `0x300`, `0x301`, `0x169`, `0x7B9`, heartbeats | Reject/ignore; no unsafe output |
| Bad checksum | `0x169`, `0x7B9`, third-party status frames if checksummed | Reject; stale/timeout logic acts |
| Frozen rolling counter | Security-critical command/status frames | Treat as stale or faulted |
| Counter jump forward | Security-critical command/status frames | Accept only if contract allows; otherwise fault |
| Counter rollback | Security-critical command/status frames | Reject or safe-handle |
| Unknown CAN ID | Both buses | Ignore unless explicitly routed by gateway policy |
| Valid ID on wrong bus | Low-only and high-only frames | Drop/ignore; no authority leak |
| Duplicate frame burst | Mode, ESTOP, drive, brake | Idempotent behavior; no repeated unsafe side effects |
| Frame reordering | Mode, heartbeat, command frames | Newer safe state wins; stale unsafe state cannot override |
| Delayed frame | Host drive/brake/mode | Timeout prevents delayed unsafe command reuse |
| Bit flip in mode byte | `0x110`, status reports | Invalid values cannot enable Auto |
| Bit flip in gear byte | Drive/motor frames | Invalid gear becomes neutral/safe or rejected |

## Generated Scenario Matrix

Phase 1 should not depend only on hand-written scenarios. It should generate
hundreds of deterministic cases from equivalence classes.

Matrix dimensions:

| Dimension | Values |
|-----------|--------|
| Mode | Manual, Auto, Estop |
| Motion | stopped, forward creep, forward nominal, forward max, reverse creep |
| Steering | straight, gentle left, gentle right, hard left, hard right, sign change |
| Braking | none, normal brake, obstacle assist, ESTOP brake, brake release |
| Gear | N, D, R, invalid |
| Host state | absent, healthy, stale command, frozen heartbeat, reconnecting |
| RT state | healthy, missing heartbeat, frozen heartbeat, reconnecting |
| SYS state | healthy, missing heartbeat, frozen heartbeat, reconnecting |
| SES state | absent, booting, aligned, stale feedback, reconnecting |
| SEB state | absent, booting, aligned, stale feedback, reconnecting |
| CAN quality | clean, dropped frame, delayed frame, corrupt frame, duplicate frame |
| Event timing | before startup grace, during startup grace, before timeout, at timeout, after timeout |

Minimum generated scenario sets:

| Set | Minimum cases | Purpose |
|-----|---------------|---------|
| Nominal maneuver matrix | 100 | Forward/reverse/turn/brake/stop combinations across modes |
| Mode transition matrix | 60 | Transitions while stopped, moving, turning, braking, and faulted |
| Connection matrix | 80 | Host, RT, SYS, SES, SEB, MTR, PWT absent/disconnect/reconnect cases |
| CAN corruption matrix | 80 | Bad DLC, checksum, counters, bus placement, duplicate/delayed frames |
| Boundary-value matrix | 80 | Speed/yaw/brake/gear/obstacle/time limits and invalid values |
| Soak and sequence matrix | 20 | Long randomized but seeded sequences with invariant checks |

Minimum target: 420 generated Phase 1 software-only scenarios, plus unit,
native, build, protocol, and simulation tests.

## Timing And Race Tests

Race and boundary timing tests required:

| Race | Required result |
|------|-----------------|
| Mode change exactly when ESTOP happens | ESTOP wins |
| Stale Host command exactly at timeout boundary | Safe timeout behavior, deterministic result |
| Host command arrives just after Manual switch | Command ignored until valid Auto authority |
| Host command arrives just before Auto switch | Command not used unless still fresh after Auto authority |
| SYS heartbeat lost while RT sends brake takeover | Brake takeover/safe response remains deterministic |
| RT heartbeat lost while SYS is in Auto | SYS safe response within timeout |
| Host reconnects during ESTOP | ESTOP remains latched |
| SEB reconnects during braking | Brake command does not release unexpectedly |
| SES reconnects during steering ramp | Steering state remains bounded |
| Duplicate ESTOP from both buses | No echo loop, no duplicate side effects |
| High and low bus faults overlap | System remains safe and diagnostic-visible |

Timeout boundary tests:

| Timeout | Values to test |
|---------|----------------|
| Startup grace | before, at, after grace |
| Host command stale | before, at, after stale timeout |
| Host heartbeat loss | before, at, after timeout |
| RT heartbeat loss | before, at, after timeout |
| SYS heartbeat loss | before, at, after timeout |
| Steering following error | below duration threshold, at threshold, above threshold |
| Brake feedback timeout | below threshold, at threshold, above threshold |

## Soak And Randomized Testing

Seeded randomized tests should run after deterministic tests pass.

Required soak classes:

| Soak class | Example duration | Events |
|------------|------------------|--------|
| Clean nominal soak | 5 virtual minutes | Normal mode changes, forward/reverse, turns, braking |
| Fault soak | 5 virtual minutes | Random dropped/delayed/corrupt frames |
| Reconnect soak | 5 virtual minutes | Host/RT/SYS/SES/SEB/MTR/PWT disconnect and reconnect |
| ESTOP soak | 5 virtual minutes | Random ESTOP triggers and recovery attempts |
| Mixed stress soak | 15 virtual minutes | Random motion, mode, faults, reconnects, bad frames |

Randomized tests must be seeded and reproducible. Failure output must include the
seed and the event list needed to replay the case.

## CAN Mimic Requirements

Phase 1 requires software mimics for every external participant.

| Mimic | Required healthy behavior | Required fault behavior |
|-------|---------------------------|-------------------------|
| Host/Jetson | Sends heartbeat, drive, brake, obstacle frames on high bus | Missing heartbeat, frozen heartbeat, stale command, invalid command, reconnect |
| RT | Processes SYS mode, Host commands, SES/SEB status; emits RT status and commands | Missing heartbeat, frozen heartbeat, stale output, reset/reconnect |
| SYS | Emits mode, safety, heartbeat, brake/motor-related commands | Missing heartbeat, frozen heartbeat, invalid mode, reset/reconnect |
| SES/EPS-C | Sends steering feedback/status and follows steering commands in virtual plant | Missing feedback, frozen angle, following error, reconnect with old counter |
| SEB | Sends brake feedback/status and follows stroke/pressure commands in virtual plant | Missing feedback, pressure drop, frozen status, L2/L3 faults, reconnect |
| MTR | Sends motor feedback and applies virtual speed/gear response | Missing feedback, gear mismatch, throttle stuck, reconnect |
| PWT | Sends gateway/powertrain status if included | Missing heartbeat, wrong bus forwarding, reconnect |

Every mimic should support:

| Fault action | Meaning |
|--------------|---------|
| `disconnect` | Stop all frames from that mimic |
| `reconnect` | Resume frames after absence |
| `freezeCounter` | Keep alive/rolling counter unchanged |
| `freezePayload` | Keep payload unchanged while time advances |
| `badDlc` | Emit valid ID with wrong DLC |
| `badChecksum` | Emit checksum-protected frame with invalid checksum |
| `delayFrame` | Deliver frame late |
| `dropFrame` | Remove selected frame |
| `duplicateFrame` | Deliver same frame more than once |
| `wrongBus` | Emit valid frame on invalid bus |
| `bitFlip` | Corrupt selected payload bit |

## Bypass And Bench-Mode Validation

Bypass behavior must be tested in Phase 1 because bypasses intentionally change
safety assumptions. The software gate must prove both sides:

1. Bypass modes work when intentionally enabled for software/bench testing.
2. Bypass modes are off and cannot silently affect vehicle/production behavior.

Known bypass and test flags:

| Flag or mode | Location | Meaning | Phase 1 concern |
|--------------|----------|---------|-----------------|
| `SYSTEM_RUN_MODE == 0` | RT/SYS `app_main()` | Production mode, safety checks enforced | Must not enable runtime bypass flags |
| `SYSTEM_RUN_MODE == 1` | RT/SYS `app_main()` | Prototype mode, bypasses enabled only if developer override pin is active | Must test both pin inactive and active behavior |
| `SYSTEM_RUN_MODE == 2` | RT/SYS `app_main()` | Pure software simulation mode | Must enable software-only bypasses deliberately and visibly |
| `g_bench_solo_mode` | RT/SYS globals | Runtime bench solo mode | Must gate bypass behavior; should be false in production |
| `g_bypass_eps_sync` | RT/SYS globals | Skip EPS-C listen/sync path | Must only work with bench/software mode, never production |
| `g_bypass_seb_sync` | RT/SYS globals | Skip SEB sync path where implemented | Must only work with bench/software mode, never production |
| `g_bypass_mtr_absent` | RT/SYS globals | Treat missing MTR as allowed | Must not disable EGAS/motor absence checks in production |
| `TESTING` | SYS/native build flags | Host/native test stubs and deterministic time | Must not be present in vehicle builds |

| `ETRIKE_RT_PID_MODE` | RT build flag | Active speed PID injection | Must be 0 (off) unless encoder prerequisites are met and speed feedback source is set to 2 |
| `ETRIKE_RT_ENCODERS` | RT build flag | Encoder PCNT support | Must be paired correctly with active PID tests |

Runtime bypass initialization tests:

| Scenario | Required result |
|----------|-----------------|
| Production run mode | `g_bench_solo_mode=false`, `g_bypass_eps_sync=false`, `g_bypass_seb_sync=false`, `g_bypass_mtr_absent=false` |
| Prototype mode, override pin inactive | Same as production; safety checks enforced |
| Prototype mode, override pin active | All intended bypass flags become true and warning is visible |
| Pure software simulation mode | All intended software bypass flags become true and warning is visible |
| Invalid run mode | Treated as production/safety-enforced |
| Runtime bypass toggled after ESTOP | ESTOP remains latched; bypass cannot clear emergency state |
| Runtime bypass toggled while Auto active | No unsafe immediate actuator jump; fresh valid state still required |

EPS-C/steering bypass tests:

| Scenario | Required result |
|----------|-----------------|
| Production, no `0x201` SES feedback | Steering remains in listen-sync then faults; it must not become active |
| Bench/software, no `0x201` with `g_bypass_eps_sync=true` and `g_bench_solo_mode=true` | Steering may enter active with assumed center exactly as designed |
| `g_bypass_eps_sync=true` but `g_bench_solo_mode=false` | Bypass must not activate |
| `g_bench_solo_mode=true` but `g_bypass_eps_sync=false` | Bypass must not activate |
| Bypass active then valid SES appears | State remains deterministic; no angle jump outside bounds |
| Bypass active then ESTOP | ESTOP steering behavior still follows ramp/hold/silent rules |

SEB/brake bypass tests:

| Scenario | Required result |
|----------|-----------------|
| Production, no SEB feedback | Brake sync/degraded/takeover behavior follows safety design; no fake healthy state |
| Bench/software with SEB bypass enabled | Software tests can run without physical SEB but outbound `0x7B9` remains bounded and valid |
| SEB bypass active in Manual | No unsafe brake release or surprise pressure command |
| SEB bypass active in Auto | Brake commands still bounded and checksum/counter/alignment bits valid |
| SEB bypass active in ESTOP | Safe brake command still wins |
| Bypass active then SEB reconnects | Alignment/sync behavior deterministic before normal control resumes |

MTR/motor absence bypass tests:

| Scenario | Required result |
|----------|-----------------|
| Production, MTR absent | Missing MTR feedback cannot be silently ignored in Auto |
| Bench/software with `g_bypass_mtr_absent=true` | Tests can run without MTR feedback while outputs remain bounded |
| MTR absent bypass active in Auto | EGAS/motor checks that are intentionally bypassed are reported as bypassed in diagnostics or test report |
| MTR absent bypass inactive in Auto | Speed mismatch or missing feedback triggers expected safe behavior |
| MTR reconnect after bypassed operation | No automatic unsafe throttle/gear output without fresh healthy feedback |

Compile/build flag tests:

| Build | Required check |
|-------|----------------|
| RT vehicle | Does not define test-only or unintended bypass flags |
| SYS vehicle | Does not define `TESTING` or unintended bench-only motor ownership flags |
| RT bench/native | Test/bypass flags are explicit in build output/report |
| SYS bench/native | `TESTING` presence is explicit in build output/report |
| Active PID build | Fails or is blocked unless encoder prerequisites are intentionally enabled and tested |
| Encoder-disabled build | PID/encoder paths compile out cleanly and do not read fake encoder data |

Bypass safety invariants:

| Invariant | Must always hold |
|-----------|------------------|
| Bypass visibility | Any enabled bypass appears in logs, diagnostics, or Phase 1 report |
| No silent production bypass | Vehicle/production builds must not enable bypasses silently |
| Pair-gated bypass | `g_bypass_eps_sync` must not act unless `g_bench_solo_mode` is also true |
| ESTOP still wins | No bypass can suppress or clear ESTOP behavior |
| Manual still safe | No bypass can create autonomous actuator output in Manual |
| Output bounds still apply | Bypass cannot skip speed, steering, brake, gear, checksum, or rolling-counter bounds |
| Reconnect still safe | Reconnecting a bypassed unit cannot restore Auto output without fresh valid state |

Bypass matrix minimum cases:

| Set | Minimum cases | Purpose |
|-----|---------------|---------|
| Run-mode matrix | 12 | Production/prototype/software modes with override active/inactive/invalid |
| EPS bypass matrix | 20 | Steering sync bypass combinations and ESTOP interaction |
| SEB bypass matrix | 20 | Brake sync bypass, command validity, reconnect, ESTOP |
| MTR absence matrix | 20 | Missing motor feedback with bypass on/off and reconnect |
| Build flag matrix | 12 | Vehicle, bench, native, TESTING, encoder/PID flags |

Minimum additional bypass target: 84 Phase 1 cases.

## Traceability Requirements

Every safety requirement should map to at least one Phase 1 test.

Example requirement IDs:

| Requirement | Meaning | Minimum Phase 1 test evidence |
|-------------|---------|-------------------------------|
| `REQ-MODE-001` | Boot starts Manual | SYS mode test and RT/SYS simulation |
| `REQ-MODE-002` | Direct Estop to Auto blocked | Mode-manager and scenario test |
| `REQ-ESTOP-001` | ESTOP overrides all commands | Deterministic and generated scenario tests |
| `REQ-STALENESS-001` | Stale Host drive zeros output | RT host/native and simulation stale tests |
| `REQ-HB-001` | Frozen heartbeat is unhealthy | RT/SYS safety tests |
| `REQ-CAN-001` | Bad DLC rejected | CAN corruption matrix |
| `REQ-CAN-002` | Checksums validated | Protocol and corruption tests |
| `REQ-GW-001` | Wrong-bus frame cannot grant authority | Gateway and wrong-bus simulation tests |
| `REQ-STEER-001` | Steering bounded by speed | Physics and generated maneuver tests |
| `REQ-BRAKE-001` | Brake command bounded | Brake priority and scenario tests |
| `REQ-RECON-001` | Reconnect cannot restore Auto unsafely | Connection matrix |
| `REQ-DIAG-001` | Safety faults are visible | Diagnostics/status tests |
| `REQ-BYPASS-001` | Production builds do not silently enable bypasses | Build flag and run-mode tests |
| `REQ-BYPASS-002` | Bench/software bypasses work only when explicitly enabled | Bypass matrix |
| `REQ-BYPASS-003` | ESTOP priority is preserved even with bypasses enabled | Bypass ESTOP scenarios |
| `REQ-BYPASS-004` | Bypassed unit reconnect cannot resume Auto unsafely | Bypass reconnect scenarios |

## Phase 1 Implementation Roadmap

Recommended order:

| Order | Work item | Reason |
|-------|-----------|--------|
| 1 | Fix current broken `native-test` build | Existing baseline must be green before adding more tests |
| 2 | Fix current failing simulation suite | Existing simulation must be trusted before expanding it |
| 3 | Confirm RT/SYS vehicle builds complete cleanly | Build reproducibility is a release-gate requirement |
| 4 | Add one Phase 1 runner script | Prevents manual partial validation |
| 5 | Add machine-readable report output | Makes pass/fail evidence auditable |
| 6 | Add shared invariant checker | Ensures all scenario tests check safety, not just no-crash |
| 7 | Add/complete CAN mimics | Enables disconnect/reconnect/fault simulation without hardware |
| 8 | Add bypass/run-mode matrix | Proves bench bypasses work and production builds stay strict |
| 9 | Implement generated scenario matrix | Produces hundreds of deterministic cases |
| 10 | Add seeded randomized soak tests | Catches rare event-order bugs |
| 11 | Add traceability matrix | Maps requirements to evidence |

## Proposed Test File Layout And Behavior

This section describes the concrete files that should be added or upgraded to
turn this strategy into executable Phase 1 tests. File names are proposed names;
they should be adjusted only if the repository already has a stronger convention.

### Gate Runner And Reports

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `tools/phase1-software-gate.ps1` | PowerShell runner | Single entry point for Phase 1 | Runs builds/tests in order, records logs, fails if any required check fails |
| `tools/phase1/phase1_config.json` | Config | Defines required suites, timeouts, build profiles, coverage thresholds | Runner reads it so the gate is configurable without editing script logic |
| `tools/phase1/collect_git_state.ps1` | Helper | Captures commit hash, branch, dirty status | Writes metadata into report before tests run |
| `tools/phase1/write_phase1_report.ps1` | Helper | Creates summary report | Writes JSON and markdown summaries with pass/fail, logs, seeds, skipped tests |
| `artifacts/phase1/<timestamp>/phase1-report.json` | Generated artifact | Machine-readable result | Contains exact command results, test counts, failed cases, seeds, trace paths |
| `artifacts/phase1/<timestamp>/phase1-report.md` | Generated artifact | Human-readable result | Summarizes what passed, failed, skipped, and what blocks Phase 2 |

Expected runner behavior:

| Step | Command or action | Failure behavior |
|------|-------------------|------------------|
| 1 | Record git state | Continue, but mark dirty state in report |
| 2 | Build RT native | Stop or mark gate failed |
| 3 | Build SYS native | Stop or mark gate failed |
| 4 | Build RT vehicle | Stop or mark gate failed |
| 5 | Build SYS vehicle | Stop or mark gate failed |
| 6 | Rebuild RT local host tests | Stop on compile failure |
| 7 | Run RT local host tests | Fail on nonzero exit |
| 8 | Rebuild SYS local host tests | Stop on compile failure |
| 9 | Run SYS local host tests | Fail on nonzero exit |
| 10 | Build and run `native-test` | Fail on build or test failure |
| 11 | Run simulation unit/integration/scenario tests | Fail on test failure |
| 12 | Run generated scenario matrix | Fail on invariant failure |
| 13 | Run protocol drift checks | Fail on semantic drift |
| 14 | Run static analysis and coverage if enabled | Fail based on configured severity/threshold |

The runner should save the full stdout/stderr for every command. It should not
depend on previously built `.exe` files unless the same run just rebuilt them.

### Static Analysis Files

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `tools/static/run_cppcheck.ps1` | PowerShell runner | Run `cppcheck` on RT/SYS/shared C++ | Emits XML/text report; safety-relevant warnings fail the gate |
| `tools/static/run_clang_tidy.ps1` | PowerShell runner | Run `clang-tidy` where compile commands exist | Starts warning-only, later gate on selected checks |
| `tools/static/run_ts_checks.ps1` | PowerShell runner | Run TypeScript checks | Runs `npm run check` or `tsc --noEmit` for simulation/debug-tool packages |
| `tools/static/static_allowlist.txt` | Config | Temporary known findings | Each allowed finding must include reason and expiration/removal plan |

Static analysis should check at minimum:

| Area | Checks |
|------|--------|
| C/C++ bounds | Array index bounds, buffer length, invalid shifts |
| C/C++ initialization | Uninitialized variables, missing default cases, dead stores |
| C/C++ conversions | Signed/unsigned narrowing, integer overflow risks, enum misuse |
| C/C++ pointers | Null dereference, dangling references, lifetime issues |
| TypeScript | Type errors, unreachable code, invalid assumptions in simulation and debug-tool |

### Protocol And Generated-Code Tests

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `native-test/test/test_can_contract_all_frames.cpp` | C++ native test | Validate all shared CAN frames from firmware perspective | Iterates known IDs/DLCs, packs/unpacks frames, checks field positions and bounds |
| `simulation/tests/unit/can-contract.generated.test.ts` | Vitest | Validate TypeScript view of CAN contract | Imports generated TS constants and checks IDs/DLCs/field metadata |
| `simulation/tests/unit/can-protocol-drift.test.ts` | Vitest | Detect simulator/protocol drift | Compares simulation encode/decode expectations against generated catalog |
| `tools/can/check_generated_no_drift.ps1` | Runner | Ensure generated files are current | Runs generator into temp or checks semantic diff; timestamp-only changes are ignored or reported separately |
| `tools/can/check_docs_can_tables.ps1` | Runner | Check docs against generated CAN IDs/DLCs | Parses documented CAN tables where practical and flags mismatches |

Protocol tests should include:

| Frame group | Required checks |
|-------------|-----------------|
| Heartbeats | ID, DLC, counter wrap, health flags, bus placement |
| Mode/status | `0x110`, `0x011`, `0x210`, valid/invalid enum handling |
| Drive/brake | `0x300`, `0x301`, `0x204`, `0x205`, signedness, endian, bounds |
| Steering | `0x169`, `0x201`, `0x202`, alignment, angle encoding, checksum, counter |
| Brake-by-wire | `0x7B9`, `0x721`, `0x731`, stroke/pressure mode mux, checksum, counter |
| Gateway | Allowed direction, blocked direction, wrong-bus rejection |

### RT Local Host Tests

Existing RT local tests should be rebuilt and then expanded. Proposed new files:

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `rt-esp32/test/test_rt_host_all.ps1` | Runner | Build and run all RT host tests | Compiles from `.stale/*.cpp` or renamed current sources, then runs each `.exe` |
| `rt-esp32/test/test_rt_build_profiles.ps1` | Runner | Build native/vehicle/bench variants | Verifies expected build flags and no unintended bypass flags in vehicle profile |
| `rt-esp32/test/test_rt_bypass_modes.cpp` | C++ host test | Runtime bypass flag behavior | Tests production/prototype/software run-mode helper logic once extracted into testable function |
| `rt-esp32/test/test_rt_mode_behavior.cpp` | C++ host test | RT Manual/Auto/Estop behavior | Feeds mode and Host commands into testable dispatch/control helpers, asserts outbound frames |
| `rt-esp32/test/test_rt_stale_commands.cpp` | C++ host test | Host command and heartbeat staleness | Uses fake time to verify output zeroing and assisted-stop timing |
| `rt-esp32/test/test_rt_gateway_policy.cpp` | C++ host test | Gateway allow/block rules | Injects frames on high/low virtual buses and asserts forwarding behavior |
| `rt-esp32/test/test_rt_can_faults.cpp` | C++ host test | Bad DLC/checksum/wrong-bus behavior | Feeds malformed frames and asserts no unsafe state or output |
| `rt-esp32/test/test_rt_reset_defaults.cpp` | C++ host test | Safe defaults after reset | Reinitializes RT state in each mode/fault case and checks Manual/safe default behavior |

How RT tests should work:

| Requirement | Implementation approach |
|-------------|-------------------------|
| No hardware | Use fake time and virtual CAN frames only |
| Deterministic | No sleeps; advance fake time explicitly |
| No stale executables | Runner rebuilds tests before execution |
| Testable app logic | Extract small pure functions for run-mode evaluation, dispatch decisions, and output construction where needed |
| Safety assertions | Use common invariant helper for mode authority, stale output, bounded outputs, checksum/counter validity |

### SYS Local Host Tests

Existing SYS local tests should be rebuilt and then expanded. Proposed new files:

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `sys-esp32/test/test_sys_host_all.ps1` | Runner | Build and run all SYS host tests | Compiles `test_mode_manager.cpp`, `test_safety_monitor.cpp`, `test_brake_priority.cpp`, and new tests |
| `sys-esp32/test/test_sys_build_profiles.ps1` | Runner | Build native/vehicle/bench variants | Confirms `TESTING` is absent/present only where intended |
| `sys-esp32/test/test_sys_bypass_modes.cpp` | C++ host test | Runtime bypass flag behavior | Tests production/prototype/software run-mode helper logic once extracted into testable function |
| `sys-esp32/test/test_sys_mode_matrix.cpp` | C++ host test | Full mode transition matrix | Tests Manual/Auto/Estop/invalid/stale/repeated/out-of-order transitions |
| `sys-esp32/test/test_sys_rt_loss_behavior.cpp` | C++ host test | SYS response to RT loss | Fake time + RT heartbeat frames; asserts timeout and safe output behavior |
| `sys-esp32/test/test_sys_brake_sync_faults.cpp` | C++ host test | SEB sync/degraded/fault paths | Simulates missing/stale/bad SEB status and checks brake output/fault state |
| `sys-esp32/test/test_sys_motor_absence.cpp` | C++ host test | MTR absence and EGAS behavior | Tests `g_bypass_mtr_absent` on/off, speed mismatch, missing feedback |
| `sys-esp32/test/test_sys_reset_defaults.cpp` | C++ host test | Safe defaults after reset | Reinitializes SYS during Manual/Auto/Estop and verifies Manual/safe recovery |

How SYS tests should work:

| Requirement | Implementation approach |
|-------------|-------------------------|
| Fake time | Reuse deterministic test time from `safety_monitor.cpp` or standardize a common fake clock |
| Fake inputs | Simulate buttons, ESTOP, brake lever, mode commands, RT heartbeat, SEB/MTR status |
| No direct GPIO dependency | Hardware GPIO should be wrapped or compiled out in host tests |
| Mode authority | Every test asserts Auto cannot be entered or resumed unexpectedly |
| Bypass visibility | Tests assert bypass state is reported in the Phase 1 result or diagnostics |

### Native-Test Suite Additions

The `native-test` suite should cover cross-firmware logic with a shared virtual
CAN bus and HAL stubs.

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `native-test/test/test_phase1_invariant_checker.cpp` | C++ native test | Verify invariant checker itself | Feeds safe and intentionally unsafe traces; unsafe traces must fail |
| `native-test/test/test_virtual_can_faults.cpp` | C++ native test | Shared virtual CAN fault injector | Tests drop, delay, duplicate, corrupt, wrong-bus, bad-DLC behavior |
| `native-test/test/test_rt_sys_mode_integration.cpp` | C++ native test | RT/SYS software integration | Virtual SYS mode frames drive RT state; asserts outbound frames by mode |
| `native-test/test/test_rt_sys_heartbeat_integration.cpp` | C++ native test | RT/SYS heartbeat loss/reconnect | Simulates missing/frozen/reconnected heartbeats on both sides |
| `native-test/test/test_rt_sys_estop_integration.cpp` | C++ native test | ESTOP fast path and forwarding | Injects ESTOP on each bus, asserts no duplicate echo and safe output |
| `native-test/test/test_component_disconnect_matrix.cpp` | C++ native test | Software disconnect/reconnect | Runs Host/SES/SEB/MTR/PWT absence and reconnect cases |
| `native-test/test/test_bypass_matrix.cpp` | C++ native test | Cross-firmware bypass matrix | Tests production/prototype/software/bypass combinations with invariants |

How native-test additions should work:

| Requirement | Implementation approach |
|-------------|-------------------------|
| Shared virtual bus | Use `native-test/can/virtual_can_bus.cpp` with deterministic timestamps |
| No real FreeRTOS dependency where unnecessary | Prefer pure C++ tests for policy logic; use FreeRTOS only for queue/task behavior |
| Trace output | On failure, write virtual frame trace to `artifacts/phase1/...` |
| Integration over mocks | Use real protocol structs and real dispatch helpers when possible |

### Simulation Test Additions

The TypeScript simulation should own the large generated scenario matrix because
it is faster to run hundreds of virtual cases there.

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `simulation/tests/phase1/phase1.generated-matrix.test.ts` | Vitest | Main generated scenario matrix | Generates deterministic cases from equivalence classes and applies invariant checker |
| `simulation/tests/phase1/phase1.nominal-maneuvers.test.ts` | Vitest | Forward/reverse/turn/brake user cases | Tests named driving scenarios with clear assertions and traces |
| `simulation/tests/phase1/phase1.mode-matrix.test.ts` | Vitest | Manual/Auto/Estop transitions | Tests transitions while stopped, moving, turning, braking, faulted |
| `simulation/tests/phase1/phase1.connection-matrix.test.ts` | Vitest | Connect/disconnect/reconnect | Tests Host, RT, SYS, SES, SEB, MTR, PWT presence changes |
| `simulation/tests/phase1/phase1.can-corruption.test.ts` | Vitest | Malformed CAN frames | Bad DLC, checksum, counters, wrong bus, duplicate, delayed, replayed frames |
| `simulation/tests/phase1/phase1.bypass-matrix.test.ts` | Vitest | Runtime/build bypass assumptions | Tests bypass combinations and ESTOP/manual invariants |
| `simulation/tests/phase1/phase1.reset-restart.test.ts` | Vitest | Reset/restart behavior | Reinitializes virtual nodes during each mode and fault case |
| `simulation/tests/phase1/phase1.soak.seeded.test.ts` | Vitest | Seeded long-run scenarios | Runs deterministic random event sequences, saves failing seed/trace |
| `simulation/src/phase1/invariants.ts` | Library | Shared invariant checker | Exports checks for mode authority, bounds, staleness, CAN validity, diagnostics |
| `simulation/src/phase1/scenario-generator.ts` | Library | Generates matrix cases | Produces case configs from dimensions and stores `scenarioId`/seed |
| `simulation/src/phase1/fault-injector.ts` | Library | Injects faults | Drop, delay, corrupt, duplicate, wrong-bus, disconnect, reconnect |
| `simulation/src/phase1/mimics/*.ts` | Library | Unit mimics | Host, RT, SYS, SES, SEB, MTR, PWT behavior models where not already present |
| `simulation/src/phase1/trace-writer.ts` | Library | Trace persistence | Writes virtual CAN/state traces for failures and selected pass cases |

How simulation tests should work:

| Requirement | Implementation approach |
|-------------|-------------------------|
| Generated but deterministic | Each case has stable `scenarioId`; randomized cases include seed |
| Fast feedback | Split fast matrix and long soak files so quick gate can run first |
| Strong assertions | Every case calls `assertPhase1Invariants(result)` plus scenario-specific checks |
| Failure reproduction | Failure message includes scenario ID, seed, event list, and trace path |
| No hidden skips | Skipped generated cases must be counted and reported as blockers unless justified |

### Scenario Generator Design

Proposed generator files:

| File | Purpose |
|------|---------|
| `simulation/src/phase1/types.ts` | Shared types for modes, motion classes, unit states, CAN quality, event timing |
| `simulation/src/phase1/matrix.ts` | Defines equivalence-class dimensions and minimum case counts |
| `simulation/src/phase1/scenario-generator.ts` | Builds deterministic scenario configs from matrix dimensions |
| `simulation/src/phase1/scenario-pruner.ts` | Removes impossible or duplicate combinations while recording why |
| `simulation/src/phase1/scenario-runner.ts` | Runs one scenario and returns frames, state timeline, diagnostics, invariants |

Generator behavior:

| Step | Behavior |
|------|----------|
| 1 | Load dimensions: mode, motion, steering, braking, gear, node states, CAN quality, timing |
| 2 | Generate combinations using pairwise or constrained combinatorial strategy |
| 3 | Assign stable `scenarioId` from dimension values |
| 4 | Reject impossible combinations only through explicit documented constraints |
| 5 | Run each case with virtual time and deterministic event order |
| 6 | Apply global invariants and scenario-specific expected checks |
| 7 | Save trace on failure and optional sample traces for passing cases |
| 8 | Report counts by scenario set and dimension coverage |

Example scenario ID format:

```text
phase1.mode-auto.motion-forwardNominal.steer-gentleLeft.brake-normal.host-healthy.sys-healthy.can-clean.timing-beforeTimeout
```

### Invariant Checker Design

Proposed invariant checker functions in `simulation/src/phase1/invariants.ts`:

| Function | Checks |
|----------|--------|
| `assertAutoAuthority(result)` | Auto actuator frames appear only with valid Auto authority and fresh prerequisites |
| `assertManualSilence(result)` | Manual mode suppresses autonomous actuator outputs except documented safe-zero/release frames |
| `assertEstopPriority(result)` | Once ESTOP occurs, drive/steer/brake outputs go safe and cannot be overridden |
| `assertNoStaleActuation(result)` | Stale commands cannot keep nonzero speed/steering/brake authority alive |
| `assertOutputBounds(result)` | Speed, steering, brake pressure, stroke, gear, mode stay within legal bounds |
| `assertCanValidity(result)` | Required outbound frames have correct ID, DLC, checksum, counter, endian, mux fields |
| `assertReconnectSafety(result)` | Reconnect does not resume Auto output without fresh healthy state |
| `assertRateCompliance(result)` | Periodic frames are within configured virtual timing tolerance |
| `assertDiagnosticVisibility(result)` | Safety-relevant faults appear in status/health/diagnostic/report |
| `assertBypassVisibility(result)` | Any bypass is explicitly visible and does not suppress ESTOP/manual safety |

Each invariant failure should include:

| Field | Meaning |
|-------|---------|
| `invariant` | Name of failed invariant |
| `timeMs` | Virtual time of failure |
| `mode` | Current mode |
| `event` | Current event or last relevant event |
| `frame` | CAN frame involved, if any |
| `expected` | Expected safe behavior |
| `actual` | Observed behavior |

### CAN Mimic File Design

Proposed mimic files:

| File | Mimic | Behavior |
|------|-------|----------|
| `simulation/src/phase1/mimics/host.ts` | Host/Jetson | Emits `0x7FC`, `0x300`, `0x301`, obstacle frames; supports stale/frozen/reconnect |
| `simulation/src/phase1/mimics/rt.ts` | RT model wrapper | Uses existing RT simulation behavior and exposes reset/bypass/fault hooks |
| `simulation/src/phase1/mimics/sys.ts` | SYS model wrapper | Uses existing SYS simulation behavior and exposes reset/bypass/fault hooks |
| `simulation/src/phase1/mimics/ses.ts` | SES/EPS-C | Emits steering feedback/status; follows or fails to follow commands |
| `simulation/src/phase1/mimics/seb.ts` | SEB | Emits brake feedback/status; follows stroke/pressure or injects faults |
| `simulation/src/phase1/mimics/mtr.ts` | MTR | Emits motor feedback; follows speed/gear or simulates mismatch/stuck throttle |
| `simulation/src/phase1/mimics/pwt.ts` | PWT | Emits powertrain/gateway status if included |

All mimics should implement a common interface:

```ts
interface Phase1Mimic {
  readonly name: string;
  reset(config: MimicConfig): void;
  tick(nowMs: number, bus: VirtualCanBus): void;
  disconnect(): void;
  reconnect(): void;
  freezeCounter(enabled: boolean): void;
  freezePayload(enabled: boolean): void;
  injectFault(fault: Phase1Fault): void;
}
```

### Coverage And Mutation Test Files

| File | Type | Purpose | How it should work |
|------|------|---------|--------------------|
| `tools/coverage/run_cpp_coverage.ps1` | Runner | C++ host/native coverage | Builds with coverage flags, runs tests, generates `gcovr`/HTML/XML report |
| `tools/coverage/run_ts_coverage.ps1` | Runner | TypeScript simulation coverage | Runs Vitest coverage and reports uncovered scenario/invariant code |
| `tools/coverage/coverage_thresholds.json` | Config | Defines coverage gates | Separate thresholds for safety-critical and noncritical code |
| `tools/mutation/run_safety_mutations.ps1` | Runner | Optional mutation smoke | Applies selected safe mutations in temp copy and confirms tests fail |

Coverage should be treated carefully. High coverage alone is not safety proof,
but low coverage on safety-critical files is a blocker.

Safety-critical files that should have explicit coverage tracking:

| Area | Files or modules |
|------|------------------|
| RT physics | `rt-esp32/src/physics_model.cpp`, `physics_model.h` |
| RT steering safety | `rt-esp32/src/steering_control.h` |
| RT dispatch/gateway | `rt-esp32/src/can_dispatch.h`, `can_rx_router.h`, gateway helpers |
| RT heartbeat/safety | `rt-esp32/src/heartbeat.h`, `safety_monitor.h` |
| SYS mode/safety | `sys-esp32/src/mode_manager.*`, `safety_monitor.*` |
| SYS brake | Brake arbitration/priority helpers, SEB request construction |
| Shared protocol | `protocol/generated/cpp/protocol.h`, generated CAN data |
| Simulation invariants | `simulation/src/phase1/invariants.ts` |

### Traceability File Design

| File | Purpose | How it should work |
|------|---------|--------------------|
| `docs/validation/phase1-traceability-matrix.md` | Human-readable requirement-to-test map | Lists every requirement, tests that cover it, status, and gaps |
| `tools/phase1/requirements.json` | Machine-readable requirements | Runner can validate every required ID has at least one test |
| `tools/phase1/test_inventory.json` | Machine-readable test inventory | Maps test files to requirement IDs and scenario groups |

Example requirement entry:

```json
{
  "id": "REQ-ESTOP-001",
  "title": "ESTOP overrides all commands",
  "criticality": "blocker",
  "tests": [
    "simulation/tests/phase1/phase1.mode-matrix.test.ts",
    "native-test/test/test_rt_sys_estop_integration.cpp",
    "rt-esp32/test/test_rt_mode_behavior.cpp"
  ]
}
```

The Phase 1 gate should fail if a blocker requirement has no mapped automated
test or if all mapped tests are skipped.

### Documentation Files To Maintain

| File | Purpose |
|------|---------|
| `docs/validation/rt-sys-pre-vehicle-validation.md` | High-level phase gates and exit criteria |
| `docs/validation/phase1-software-validation-details.md` | Detailed Phase 1 strategy and implementation blueprint |
| `docs/validation/phase1-traceability-matrix.md` | Requirement-to-test mapping once tests are implemented |
| `docs/validation/phase1-runbook.md` | How to run the Phase 1 gate, inspect logs, reproduce failures |
| `docs/validation/phase1-known-gaps.md` | Temporary missing coverage and blockers |

The runbook should include:

| Section | Content |
|---------|---------|
| Prerequisites | Compiler, PlatformIO, Node, Python, CMake, optional static/coverage tools |
| Commands | Fast gate, full gate, one-suite-only commands |
| Artifacts | Where logs, traces, reports, coverage files are written |
| Failure triage | How to find first failed test, reproduce a seed, inspect virtual CAN trace |
| Known environment issues | PlatformIO locked `.pio` cleanup, generated timestamp diffs, stale `.exe` behavior |

## Immediate Known Fixes Needed

Based on recent tool runs:

| Issue | Likely fix direction |
|-------|----------------------|
| MSVC compile error in `pid_controller.h` around `std::max` | Avoid Windows `max` macro conflict, e.g. parenthesized `(std::max)(...)` or define `NOMINMAX` in native build |
| `sim_engine_native` unresolved `g_sim_time_us` | Align symbol declaration/definition linkage and type in native-test sim engine |
| Simulation CAN encoding failures | Reconcile test expectations with current `protocol/generated/cpp/protocol.h` and generated CAN data |
| Simulation timeout | Split slow soak tests or increase timeout after failures are fixed |
| PlatformIO locked `.pio/build/vehicle` cleanup warnings | Identify process holding file or clean when no PlatformIO process is active |
| Generated CAN timestamp diffs | Avoid timestamp-only regeneration diffs or exclude from docs-only commits |

## Phase 1 Exit Criteria

Phase 1 is complete only when all of the following are true:

| Criterion | Required state |
|-----------|----------------|
| RT native build | Pass |
| SYS native build | Pass |
| RT vehicle build | Pass |
| SYS vehicle build | Pass |
| RT local host tests | Pass, rebuilt from current source |
| SYS local host tests | Pass, rebuilt from current source |
| `native-test` CMake suite | Pass |
| Simulation unit/integration/scenario tests | Pass |
| Generated scenario matrix | Minimum case counts met, all invariants pass |
| Bypass matrix | Run-mode, EPS, SEB, MTR, and build-flag bypass cases pass |
| Seeded soak tests | Pass, seeds recorded |
| Fault injection tests | Missing/frozen/malformed/stale/delayed/wrong-bus frames safe |
| Mode coverage | Manual, Auto, Estop, invalid, stale, and transition cases covered |
| Reconnect coverage | Host, RT, SYS, SES, SEB, MTR, PWT disconnect/reconnect covered |
| Evidence | Logs and summary saved with commit hash |
| Known gaps | Either closed or explicitly listed as blockers |

## Stop Conditions For Phase 1

Do not proceed to Phase 2 if any of these remain unresolved:

| Stop condition | Reason |
|----------------|--------|
| Any required software test fails | Controller testing would start from untrusted software |
| Simulation CAN contract disagrees with shared protocol | Hardware tests may validate the wrong contract |
| Manual mode emits unsafe autonomous output | Human/manual authority can be violated |
| Estop does not override a scenario | Emergency behavior is not proven |
| Stale command keeps actuation alive | Loss of Host or bus data can cause unsafe motion |
| Reconnect restores Auto without fresh valid state | Disconnect/reconnect can create surprise actuation |
| Invalid mode or gear enables output | Corrupt data can create authority or motion |
| Wrong-bus frame grants authority | Gateway isolation is broken |
| Missing/frozen heartbeat not detected | Distributed safety monitoring is broken |
| Required evidence is not saved | Result cannot be audited or repeated |
| Production build enables a bypass silently | Vehicle test would start from unsafe assumptions |
| Any bypass suppresses ESTOP | Emergency path cannot be trusted |
