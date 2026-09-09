# E-Trike Testing Architecture

This document describes the full verification stack for the E-Trike codebase: what
is tested, where, how it is run, and what each layer proves. It is the
authoritative map of the test suites. The companion policy is
[`docs/testing_and_validation/protocol-testing-plan.md`](docs/testing_and_validation/protocol-testing-plan.md),
which defines evidence levels and CI gates. This file describes the actual code
and commands.

---

## 1. Guiding principle: one oracle, many languages

The single source of truth for wire behavior is a **language-neutral vector
document**:

- `protocol/vectors/payload-v1.json` — packed-bits, signed widths, endian
  layout, constants, vendor checksums, DLC/format errors, and `unsupported_semantics` cases for every canonical message.
- `protocol/vectors/custom-codec-values-v1.json` — SES/SEB custom-codec value round-trips.
- `protocol/vectors/sequences-v1.json` — counter/freshness sequences (wrap, duplicate, gap, reorder, frozen, recovery, session epoch, same-ID-on-different-buses).

The **same bytes** in `payload-v1.json` are decoded and re-encoded by three
independent generated codecs — **Python** (`protocol.generated.python`),
**C++** (`protocol/generated/cpp/etrike_protocol.hpp`), and **TypeScript**
(`protocol/codecs/typescript`) — and asserted byte-for-byte identical. A
language may omit an unsupported *custom* codec, but then its capability manifest
must mark semantic decode unavailable. This cross-language contract is what makes
the protocol "frozen": a wire-meaning change is caught because it breaks at least
one codec or shifts the exported `SEMANTIC_HASH` / `NETWORK_HASH`.

---

## 2. Evidence levels

Each suite is labelled by what it can prove. A "Pass" means only the assertions
at that level passed; a simulator result is never hardware evidence.

| Level | Proves | Does not prove |
|---|---|---|
| Schema / generator | Valid normalized contract; deterministic generation | Runtime scheduling, hardware |
| Codec vectors | Exact cross-language payload behavior | Transport, deadlines, ECU policy |
| Native component | State machines, counters, deadlines, aggregation | Target peripherals, real-time timing |
| Replay / virtual CAN | Routing, loss/corruption, ECU interaction | Physical bus / adapter behavior |
| HIL / bench | Flashed firmware, measured timing, physical I/O | Paths not exercised by that procedure |

---

## 3. Test inventory by layer

| # | Layer | Location | Runner |
|---|---|---|---|
| L0 | Contract & schema validation | `protocol/tools/protocol.py` | `validate` CLI |
| L1 | Deterministic code generation | `protocol/tools/protocol.py` | `generate --check` |
| L2 | Cross-language codec conformance | `protocol/vectors/*.json` | Python + C++ + TS suites |
| L3 | Protocol Python unit tests | `protocol/tests/python/*.py` | `unittest` / `pytest` |
| L4 | Protocol C++ unit tests | `protocol/tests/cpp/*.cpp` | compiled, run via pytest L3 harness |
| L5 | Native C++ component & integration | `native-test/` | CMake + CTest; standalone binaries |
| L6 | Firmware native tests (PlatformIO) | `sys-esp32`, `rt-esp32`, `mtr-stm32` | `pio test` |
| L7 | SIL Python physics & diagnostics | `simulation/sil/tests/*.py` | pytest / runner |
| L8 | Simulation TypeScript suites | `simulation/tests/**` | `npm test` (vitest) |
| L9 | CI gates | `.github/workflows/ci.yml` | GitHub Actions |
| — | Master verification runner | `scripts/run_all_tests.py` | `test.ps1` / `test.cmd` |

---

## 4. Layer details

### 4.1 Contract & schema validation (L0)

`python -m protocol.tools.protocol validate` loads every file in
`protocol/contracts/` (`network.yaml` + one file per owner/vendor family:
`host`, `rt`, `sys`, `mtr`, `ses`, `seb`, `hmi`, `pwt`) plus
`baseline-manifest.json`, builds the canonical model, and asserts it against the
**frozen baseline** (`baseline-manifest.json`). It enforces, among others:

- exactly one canonical key `owner:key` and unique `(bus, CAN ID)` per instance;
- every payload selects exactly one `generated` / `profile` / `custom` strategy;
- profile/custom strategies require a versioned `implementation_id` / `profile_id`
  and a `vector_set_id`;
- no field exceeds the DLC, no overlapping fields, ambiguous IDs require `--bus`;
- `same_frame` routes must preserve identity; `regenerated` routes must declare
  explicit semantics.

`validate` also rejects competing implementations (a `generated` message must not
carry an `implementation_id`). The contract schema itself is exported as
`protocol/generated/contract-schema.json`.

### 4.2 Deterministic code generation (L1)

`python -m protocol.tools.protocol generate` renders all artifacts under
`protocol/generated/` (C++ header, Python module, TypeScript catalog, plus
`discovery.json`, `capabilities.json`, `errors.json`, `contract-schema.json`,
DBC/CSV/docs). Generation is **deterministic** — no wall-clock content — and
verification is read-only:

```
python -m protocol.tools.protocol generate --check     # fails if any artifact is stale
```

`test_generation.py` and `test_golden_vectors.py` both invoke `generate --check`
as an assertion and confirm `render_outputs(...)` is stable across calls.
After a reviewed contract change, run `generate` (no `--check`), inspect the
diff, then re-run read-only verification.

### 4.3 Cross-language codec conformance (L2)

`payload-v1.json` is the oracle. Each vector carries `message`, `bus`,
`frame_format`, `payload` (hex), optional typed `values`, and an expected
`status` (`ok` / `unsupported_semantics` / an error status). The same document
is consumed by:

- **Python** — `test_vectors.py` (`test_all_payload_vectors`) and
  `test_golden_vectors.py` (`test_payload_vector`, parametrized).
- **C++** — `protocol/tests/cpp/test_generated_vectors.cpp` encodes a hand-picked
  value per generated message and emits `VECTOR <key> <hex>`;
  `test_cpp_generated.py` compiles and runs it, then asserts every emitted
  payload is one of the canonical ok vectors for that message.
- **TypeScript** — `protocol/tests/typescript/payload-v1.test.ts` decodes and
  re-encodes every vector through `protocol/codecs/typescript`.

All three must agree on the bytes. `test_golden_vectors.py` additionally proves
the exported `SEMANTIC_HASH` / `NETWORK_HASH` in the Python, C++, and TypeScript
catalogs are identical, and that `generate --check` is clean.

### 4.4 Protocol Python unit tests (L3)

Run with `python -m unittest discover -s protocol/tests/python -v` (or `pytest
protocol/tests/python`). Files:

| File | Responsibility |
|---|---|
| `test_contracts.py` | Model validity vs frozen baseline; rejects duplicate keys, duplicate bus+ID, custom/profile codec rule violations, field-outside-DLC, overlapping fields, ambiguous-id-requires-bus, route-identity rules. Asserts `42` messages / `58` instances. |
| `test_vectors.py` | Every message has a success vector; round-trips **all** `payload-v1` vectors through the Python codec (decode + re-encode == bytes); decode failure leaves caller output unchanged; `ses:ses_version` raw-only; `sequences-v1.json` boundary coverage. |
| `test_generation.py` | Rendering is deterministic; generated artifacts are current (`write_or_check(check=True)`); `generate --check` CLI + ambiguous `inspect`; `discovery`/`capabilities`/`errors`/`contract-schema` manifests; C++ only emits `generated`-strategy codecs. |
| `test_phase0_audit.py` | No legacy dual `can_high/can_low` YAML; message counts & strategy mix; Control-Toolkit consumer import path (no YAML re-parse); generated encode rejects out-of-range; baseline validation; discovery/runtime hash match; diagnostics registry (`149` total, `43` IMPLEMENTED). |
| `test_diagnostics.py` | Diagnostics registry counts (`149`/`43`/`106`); hash deterministic + 64-char + ignores ordering/description; hash shifts on snapshot-semantics change; implemented-enum uniqueness + ESTOP must latch; many negative validation cases. |
| `test_diagnostics_manager.py` | Compiles `shared/test_diagnostics.cpp` (embedded-safe `DiagnosticManager`) with a C++17 compiler and runs it; asserts `PASS`. |
| `test_custom_codecs.py` | Shared `payload-v1` vectors through the `protocol.codecs.python` layer; SES command typed round-trip; SEB pressure command + overlapping status; raw/error/version/telemetry paths; identity/constant/range/atomic-output checks; generic codec cannot compete with a custom codec. |
| `test_golden_vectors.py` | Phase-0 golden vectors (pytest): every catalog message has a success vector; parametrized payload vectors; DLC-zero ESTOP on both buses; `rt:rt_heartbeat` independent high/low instances; critical toolkit IDs resolve; network routes match RT gateway set; custom-codec value round-trips; SES/SEB checksum integrity; semantic/network hash deterministic and equal across Python/C++/TS; `generate --check`; counter metadata on heartbeats; `sys:sys_diag_rpt.rx_overflow` field. |
| `test_cpp_generated.py` | Compiles `test_compat.cpp` (C++11) and `test_generated_vectors.cpp` (C++17) and runs them; the generated-vectors test asserts the C++ codec reproduces a canonical ok vector for every generated message and that `kMessages`/`kRoutes` counts and `kImplementedDiagCount` match. |

### 4.5 Protocol C++ unit tests (L4)

Located in `protocol/tests/cpp/`. Compiled with a C++17 (or C++11 for the
transport compat file) compiler; run directly or via the L3 pytest harness.

| File | Responsibility |
|---|---|
| `test_protocol.cpp` | Core bit/endian helpers; `xor8_ff_v1` profile; SES/SEB/PWT codecs (identity, checksum, range, atomicity); `CounterTracker` and `FreshnessTracker` event machine (first/increment/wrap/duplicate/frozen/gap/reorder/recovery/reset, session & bus epoch). |
| `test_compat.cpp` | C++17 **migration boundary** (`protocol/compat/can.hpp`): canonical IDs, `Mode`/`Gear` names, generated route lookups, `can::gen` / `can::custom` encode/decode adapters. |
| `test_compat_cpp11.cpp` | C++11 **transport** compatibility (`protocol/compat/transport.hpp`): `to_protocol_frame` / `from_protocol_frame` usable independently in driver code; DLC overflow leaves input untouched. |
| `test_generated_vectors.cpp` | Hand-maintained vectors (~32 generated messages) encoded through the generated C++ codec; emits `VECTOR` lines consumed by `test_cpp_generated.py` for the language-neutral cross-check (see 4.3). |

### 4.6 Native C++ component & integration (L5)

`native-test/` is a CMake project built and executed with CTest:

```
cmake -S native-test -B native-test/build -G "Unix Makefiles"
cmake --build native-test/build -j4
ctest --test-dir native-test/build --output-on-failure
```

It covers the full cross-node software interaction without hardware: master/qualification
matrices, all-signals data, architecture data flow, component I/O, gateway
forwarding, dual heartbeat, ESTOP latch, heartbeat recovery, MCP2515 config, SPI
failure, task/safety watchdog, RT CAN dispatch / RX router, RT safety checks /
safety monitor, SYS CAN dispatch / inhibit state, RT brake fallback, RT phase-2
motion, vehicle integration, DLC consistency, signal chains, and remediation
fixes.

Three suites are also executed as **standalone binaries** by the master runner
(`scripts/run_all_tests.py`), compiling with `-std=c++17 -Wall -Wextra -Werror
-pedantic`:

- `test_diagnostic_manager_exhaustive.cpp` — exhaustive diagnostic state-machine contract.
- `test_heap_allocation.cpp` — zero-allocation trap (no `new`/`malloc` in hot paths).
- `test_estop_22_scenarios.cpp` — 22-scenario ESTOP & safety-reset suite.

### 4.7 Firmware native tests (L6 — PlatformIO)

Run on host via PlatformIO `native` / `native_pipeline` environments:

| Project | Command | Scope |
|---|---|---|
| `sys-esp32` | `pio test -e native` | Mode manager, cross-node ESTOP |
| `rt-esp32` | `pio test -e native` | Resolver, stream-loss fail-safe |
| `rt-esp32` | `pio test -e native_pipeline` | End-to-end HOST_DRIVE_CMD → DAC pipeline |
| `mtr-stm32` | `pio test -e native` | DAC golden vectors |

### 4.8 SIL Python physics & diagnostics (L7)

`simulation/sil/tests/` exercises the closed-loop longitudinal plant + PID and
the diagnostic differential reference model:

- `test_differential_diag.py` — Python diagnostic reference differential.
- `test_closed_loop_drive.py` — longitudinal physics & PID loop.
- `test_sil_longitudinal.py`, `test_sil_lateral.py`, `test_sil_braking.py`,
  `test_sil_whole_vehicle.py` — SIL 1–4 (speed/torque/slope/reverse, steering/
  curvature/yaw/rollover, hydraulic braking/ESTOP, whole-vehicle
  Host→RT→SYS→Plant→Feedback).

### 4.9 Simulation TypeScript suites (L8)

`simulation/` is a vitest project (`npm test --prefix simulation`). It includes
unit (`can-encoding`, `can-protocol-drift`, `plant`, `rt-kinematics`,
`safety-checker`, `scheduler`, …), integration (`bus-routing`, `rt-to-low-bus`,
…), and scenario suites (fuzz, soak, heartbeat-timeout, mode-transition,
property, schema-drift, all-scenarios, …). The master runner's `--sim` mode runs
a quick subset; full suite runs the whole project.

### 4.10 CI gates (L9)

`.github/workflows/ci.yml` triggers on changes to `rt-esp32`, `sys-esp32`,
`mtr-stm32`, `shared`, `protocol`, `native-test`. Jobs:

- `static-analysis` — `cppcheck` over `rt-esp32/src`, `sys-esp32/src`, `shared`.
- `pio-rt` / `pio-sys` — firmware builds.
- `pio-native` — SYS/RT/MTR `native` + RT `native_pipeline` (see 4.7).
- `native-test` — CMake configure/build + `ctest` (see 4.6).
- `vehicle-build` — builds RT/SYS with the **vehicle** profile and fails if any
  bypass flag (`CONFIG_BENCH_SOLO`, `TESTING`, `CONFIG_BYPASS`) leaks into the
  build or `sdkconfig.h`.
- `bypass-audit` — audits `platformio.ini` vehicle sections for bypass flags
  (bench sections are expected to retain `CONFIG_BENCH_SOLO`).

> Note: CI does **not** currently invoke `pytest protocol/tests/python` or the
> protocol C++ suites directly. Those are run locally via
> `python -m unittest discover -s protocol/tests/python` and the `generate --check`
> gate; wire them into CI if protocol changes must be gated automatically.

### 4.11 Master verification runner

`scripts/run_all_tests.py` is the **Pre-Hardware Verification Gate** orchestrator.
It prints a per-suite PASS/FAIL summary and exits non-zero on any failure.

```
python scripts/run_all_tests.py                 # full gate
python scripts/run_all_tests.py --quick         # quick simulation subset
python scripts/run_all_tests.py --native        # native C++ suites only
python scripts/run_all_tests.py --python        # Python SIL suites only
python scripts/run_all_tests.py --sim           # TypeScript vitest only
python scripts/run_all_tests.py --pio           # PlatformIO native pipelines
```

Wrappers: `scripts/test.ps1` and `scripts/test.cmd` forward the same flags.
The runner executes: native C++ exhaustive / heap / 22-ESTOP binaries, the
Python diagnostic differential + closed-loop physics, the four SIL Python
suites, the vitest simulation suite, and the PlatformIO native pipelines
(forcing the LLVM-MinGW C++17 toolchain ahead of any legacy MinGW on PATH).

---

## 5. Quick reference — how to run

```text
# Protocol contract + generation (L0/L1)
python -m protocol.tools.protocol validate
python -m protocol.tools.protocol generate --check
python -m protocol.tools.protocol inspect 0x210 --bus low

# Protocol unit tests (L3/L4) — also compiles & runs the C++ suites
python -m unittest discover -s protocol/tests/python -v
pytest protocol/tests/python

# Native cross-node integration (L5)
cmake -S native-test -B native-test/build -G "Unix Makefiles"
cmake --build native-test/build -j4
ctest --test-dir native-test/build --output-on-failure

# Firmware native (L6)
cd sys-esp32 && pio test -e native
cd rt-esp32 && pio test -e native -e native_pipeline
cd mtr-stm32 && pio test -e native

# SIL + simulation (L7/L8)
pytest simulation/sil/tests
npm test --prefix simulation

# Full pre-hardware gate
python scripts/run_all_tests.py
```

---

## 6. Toolchain requirements

- **Python 3.11+** with `pydantic`, `PyYAML`, `pytest`. The protocol package is
  imported as `protocol.*` (run from the repo root).
- **C++17 compiler** for all protocol C++ suites, `native-test`, and the
  `DiagnosticManager` test. Any `g++`/`clang++` ≥ GCC 7 (CI uses `ubuntu-latest`)
  works.
- **Node.js + npm** for the TypeScript simulation suites (`simulation/`).
- **PlatformIO** for firmware native tests (L6).
- **CMake ≥ 3.x** for `native-test` (L5).

### Windows toolchain gotcha (recurring failure mode)

On this Windows environment the first `g++` on `PATH` is **MinGW.org GCC 6.3.0**
(`D:\Programs\MinGW\bin\g++.exe`), whose libstdc++ predates C++17 and **lacks
`<string_view>`**. Every protocol C++ test therefore fails to *compile* with:

```
fatal error: string_view: No such file or directory
 #include <string_view>
```

Fix: put the **LLVM-MinGW** C++17 toolchain ahead of MinGW 6.3 on `PATH`:

```powershell
$env:PATH = "C:\Users\logsh\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.MSVCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-msvcrt-x86_64\bin;" + $env:PATH
```

With that on `PATH`, all protocol C++ suites (`test_protocol.cpp`,
`test_compat.cpp`, `test_compat_cpp11.cpp`, `test_generated_vectors.cpp`), the
`DiagnosticManager` test, and `native-test` build and pass. The master runner
(`scripts/run_all_tests.py`) already strips legacy MinGW/TDM-GCC paths and
prepends this LLVM-MinGW bin when invoking PlatformIO.

---

## 7. Key invariants guarded by the suite

1. **Frozen wire contract** — `validate` against `baseline-manifest.json` fails
   on any instance drift; `generate --check` fails if any artifact is stale.
2. **Cross-language byte equality** — Python/C++/TypeScript must encode the same
   `payload-v1.json` vectors to identical bytes; exported `SEMANTIC_HASH` /
   `NETWORK_HASH` must match across all three catalogs.
3. **Strategy exclusivity** — exactly one `generated`/`profile`/`custom` codec
   per message; a `generated` message can never carry a custom `implementation_id`.
4. **Observational-only safety frames** — e.g. `SYS/RT/MTR NODE_STATUS` are
   decode-only; they must never clear ESTOP, enable MTR, change mode, grant
   authority, or trigger motion (enforced by contract review + codec round-trip).
5. **Atomic failure** — a failed decode/encode must leave the caller's output
   object untouched (guarded in Python, C++, and TypeScript).
6. **Deterministic generation** — no wall-clock content; byte-for-byte stable.
7. **No bypass in vehicle profile** — CI `vehicle-build` + `bypass-audit` reject
   `CONFIG_BENCH_SOLO` / `TESTING` / `CONFIG_BYPASS` in vehicle builds.
8. **Diagnostics registry integrity** — `149` total / `43` IMPLEMENTED /
   `106` NOT_IMPLEMENTED; ESTOP causes are latching; hash is stable under
   reordering but shifts on snapshot-semantics change.

---

## 8. Non-goals and known gaps

- Repository tests prove **software** behavior; physical timing, I/O, and bus
  behavior require labelled bench/HIL captures (see evidence levels in
  `docs/testing_and_validation/protocol-testing-plan.md`).
- **SES version bytes** remain raw-only (`unsupported_semantics`) until a trusted
  vendor definition or known-hardware response exists.
- The protocol Python/C++ unit suites are **not yet wired into CI**; they are run
  locally via the commands in §5.
- `native-test/` and firmware `native` tests run on host (FreeRTOS-on-host /
  PlatformIO native), not on flashed hardware.
