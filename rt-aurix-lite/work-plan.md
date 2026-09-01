# RT-AURIX-Lite Work Plan

**Purpose:** Implement the consolidated RT-only AURIX TC375 controller defined in
[`architecture.md`](architecture.md), validated host-first, with the target runtime model
decided only after the TC375 feasibility gate.

**Principle (non-negotiable):**
> *Do not port the ESP32 firmware's execution architecture. Port its required behavior. The
> execution architecture belongs to the TC375 and remains unresolved until target bring-up.*

---

## Non-negotiable phase rule

Phases are sequential. Do not start phase N+1 until all code, tests, documentation, and the
exit gate for phase N pass. If a later phase exposes a regression, return to the phase that
owns the broken contract, fix it, and rerun every gate from there forward.

Each phase is a small reviewable change, committed vertically (implementation + tests in the
same commit) so the tree is always green and bisectable.

## Cleanup rule for every phase

Cleanup is part of implementation, not a final sweep. Every phase must:

1. identify the files, generated artifacts, and documentation it replaces;
2. migrate every required consumer before deleting the legacy provider;
3. remove the superseded path in the same phase unless a documented compatibility window is
   required;
4. search the repository for stale imports, names, and documentation references;
5. run the relevant build/tests (`native-test` for host logic);
6. prove through tests that the replacement works without the removed path.

Do **not** modify `protocol/tools/` (shared RT/SYS tooling). The AURIX protocol subset has
its own parallel generator (`rt-aurix-lite/protocol/tools/generate.py`).

---

## Phase A — Foundation (host, deterministic)

### A0. Deterministic core types, time, config split

- `src/config/control_config.h`, `src/config/safety_config.h`, `src/config/timing_config.h`
  (split by concern; reuse `shared/shared_config.h` where values are semantically identical).
- `src/core/types.h` — typed domain I/O (`DriveDemand`, `MotorFeedback`, `BrakeDemand`,
  `ModeRequest`, …).
- `src/core/time.h` — `using TimeUs = uint64_t`; time is **passed in**, never fetched by domain.
- `src/core/result.h` — `state + fault bitmask` (no logging).
- Test scaffolding: `tests/CMakeLists.txt` wired into the existing `native-test` build via
  `add_subdirectory` (one FreeRTOS FetchContent, one `virtual_can`, one CTest).

**Exit gate:** `rta_core` builds as a pure STATIC library (no FreeRTOS dependency); test
targets register in CTest.

### A1. Kinematics model

- `src/domain/kinematics.*` — inverse-bicycle model (behavior-preserving port).
- **Differential test** vs the old `rt-esp32/src/physics_model.*` (read-only oracle):
  `EXPECT_EQ(old, new)` over input ranges (three speed regimes, obstacle limit).

### A2. Steering controller

- `src/domain/steering.*` — 6-state FSM + dynamic angle clamp + follow-error.
- Tests: transitions (BOOT_WAIT→LISTEN_SYNC→ACTIVE→ESTOP_RAMP/HOLD/SILENT→FAULT), clamp,
  follow-error threshold, checksum-before-L3 interaction.

### A3. Brake controller

- `src/domain/brake.*` — 4-state FSM; stroke/pressure **encoding stays in the protocol
  layer**, domain deals in typed values.
- Tests: FSM transitions, degraded recovery, lever/ESTOP/arbitration semantics.

### A4. Mode and ESTOP state model

- `src/domain/mode.*` — mode FSM + ESTOP overlay + exit semantics + HMI request (typed).
- Tests: MANUAL↔AUTO, ESTOP latch, START/MODE-long-press exit, HMI request handling.

### A5. Safety and liveness supervision

- `src/domain/safety.*` — EGAS L2 comparison, fault escalation.
- `src/domain/liveness.*` — frozen-counter detection, timeout logic (explicit `TimeUs`).
- Tests: EGAS mismatch, checksum-before-L3, L3 fault escalation, heartbeat frozen-counter,
  timeout boundaries (e.g. `observe(0,5)` … `observe(301'000,5)`).

### A6. Protocol adapters and gateway policy

- `src/protocol/decode_adapter.*`, `encode_adapter.*`, `route_table.*`.
- Uses the generated subset codecs (`protocol/generated/cpp/etrike_protocol.hpp`) and the
  shared `protocol/codecs/{ses,seb}.hpp` (read-only dependencies, not modified).
- Tests: adapter round-trips against generated codecs + SES/SEB vectors; gateway
  Category 1/2/3 forwarding; ESTOP rate limiting.

### A7. Watchdog health supervisor

- `src/app/watchdog_supervisor.*` — `service_allowed()` (health decision), **not** WDI toggling.
- Target `Watchdog::service()` is a hal concern, deferred.
- Tests: health policy, staleness, task/unit health aggregation.

### A8. IPC abstractions

- `src/ipc/messages.h`, `snapshot.h`, `spsc_channel.h` — typed cross-domain transport.
- Host: `std::atomic` SPSC. Target: LMU/DLMU + DSYNC later, without changing app code.
- Tests: SPSC correctness, snapshot staleness semantics, queue-full policy.

### A9. App orchestration controllers

- `src/app/motion_controller.*`, `safety_supervisor.*`, `body_controller.*`,
  `gateway_controller.*` — the composition layer the simulation drives.
- Tests: end-to-end typed flow (input → orchestration → domain → typed output).

### A10. HAL interfaces

- `src/hal/can.h` — `transmit(Bus, Frame, TxClass{Normal,Urgent})`, `receive(Bus)`.
- `src/hal/gpio.h`, `src/hal/clock.h`, `src/hal/watchdog.h` — interfaces only.
- No "direct mailbox write" in the portable HAL (that is a target/iLLD detail).
- No WDI "toggle" in the portable layer (`Watchdog::service()` performs the hardware action).

**Exit gate (Phase A):** all domain/app/protocol/ipc logic builds and passes CTest on the
host; no FreeRTOS-shaped layer exists.

---

## Phase B — Native validation harness

- `src/platform/host/` — `virtual_can_adapter`, `virtual_gpio`, `virtual_clock`,
  `virtual_watchdog` (reuse existing `native-test/can/virtual_can_bus.*`; **no legacy
  HAL-shadow trick** — new code injects explicit interfaces).
- Unit + differential tests as listed in Phase A, all green under `ctest`.

**Exit gate:** `ctest` green across the new targets; coverage of FSM/timeout/fault paths.

---

## Phase C — Deterministic three-domain system simulation

Replaces any premature "15 FreeRTOS task shells." Executors call the same app/domain
functions the future runtime will call.

```
            Virtual monotonic clock
                     │
     ┌───────────────┼───────────────┐
     ▼               ▼               ▼
 CPU0 executor   CPU1 executor   CPU2 executor
 data-plane      safety/motion   body/HMI
     │               │               │
     └──────── IPC / snapshots ──────┘
                     │
              virtual CAN_HIGH
              virtual CAN_LOW
              virtual GPIO
              virtual watchdog
```

- Periods from architecture §6.3 expressed as **executor periods** (CPU1 control 10 ms,
  brake 20 ms, safety 50 ms, health 100 ms; CPU2 lights 50 ms, mode 100 ms, …).
- **Fault injection** (VirtualCanBus already supports `DROP_FRAME`, `CORRUPT_DATA`,
  `SET_BUS_OFF`, `INJECT_ERROR_FRAME`):
  - ESTOP arrives during a drive-command update;
  - CAN_LOW bus-off during a brake request;
  - stale snapshot while CPU0 progresses;
  - CPU1 misses two execution periods;
  - CPU0 stalls but CPU1 continues;
  - counter wrap, queue full;
  - frame duplicated, reordered, CRC corrupt;
  - MTR feedback freezes; host heartbeat freezes.

**Exit gate:** deterministic simulation reproduces expected degradation for each scenario
(asserted, not eyeballed).

---

## TARGET GATE (deferred — toolchain + iLLD required)

Prerequisite: install a TriCore toolchain + TC375 iLLD (or AURIX Development Studio).
**Status (2026-09-01): AURIX Studio 1.10.36 installed at `E:\Infineon\AURIX-Studio-1.10.36`
(HighTec `tricore-gcc11` + TASKING compilers, TC37A iLLD 1.20.0, TC375LK template). DAS
driver still needs its bundled installer run (see `board/README.md`).**

### D0. ADS/iLLD walking skeleton
- Minimal project: clock init, port init, LED or UART blinky, on-board CAN0 loopback.
- Freeze the exact MCMCAN module/node: **CAN_LOW = CAN0 Node 0** (`P20.8` alt5 TX,
  `P20.7` RxSel_b RX), **CAN_HIGH = CAN0 Node 2** (`P15.0` alt5 TX, `P15.1` RxSel_a RX)
  — from iLLD `IfxCan_PinMap_TC37x_LQFP176` (done, in `architecture.md` §9.1).

### D1. Multicore startup
- CPU0 boot + CPU1/CPU2 bring-up; verify per-core execution (lockstep cores CPU0/CPU1,
  non-lockstep CPU2).

### D2. Real CAN_LOW / CAN_HIGH
- On-board TLE9251VSJ on CAN_LOW (CAN0 Node 0); external transceiver (part per
  architecture §9.1.2) on CAN_HIGH (CAN0 Node 2). Verify TX/RX at 500 kbit/s, standby
  handling, termination.

### D3. LMU + barriers + SRI + MPU/SMU
- Shared-memory placement (LMU/DLMU), alignment, publication ordering, **DSYNC**/compiler
  barriers, SRI service requests, MPU/BMP access, SMU safety handling.

### D4. Determine runtime model — decision gate
- Candidates: 3 AMP FreeRTOS kernels; 1–2 kernels + cyclic executors; cyclic executors only.
- Choose based on measured WCET, interrupt latency, and multicore contention.
- **Do not** assume three FreeRTOS kernels; prove it experimentally.

### D5. Production runtime shells
- Only after D4: implement the chosen runtime as an adapter around the Phase A–C app/domain
  code (`cpu1_10ms_tick()`, etc.).

**Exit gate (target):** walking skeleton + multicore + CAN + IPC + safety mechanism all
demonstrated on real hardware; runtime model selected and documented.

---

## Phase E — Bench / HIL

- CAN0 loopback → virtual-CAN bench (reuse `can-test`/`sim_engine`) → ESTOP button → per-core
  health via `0x210`/`0x600`.
- Wiring per [`wiring.md`](wiring.md): second transceiver, TPS3850-Q1, relay board, rider
  harness.

---

## Commit sequence (vertical, each builds + passes)

| # | Commit | Includes |
|---|--------|----------|
| 1 | `feat(rt-aurix-lite): add deterministic core types, time, config split` | config/*, core/*, CMake skeleton |
| 2 | `feat(rt-aurix-lite): port kinematics model` | domain/kinematics + differential test |
| 3 | `feat(rt-aurix-lite): add steering controller` | domain/steering + tests |
| 4 | `feat(rt-aurix-lite): add brake controller` | domain/brake + tests |
| 5 | `feat(rt-aurix-lite): add mode and estop state model` | domain/mode + tests |
| 6 | `feat(rt-aurix-lite): add safety and liveness supervision` | domain/safety, liveness + tests |
| 7 | `feat(rt-aurix-lite): add protocol adapters and gateway policy` | protocol/* + generated-codec integration + vectors |
| 8 | `feat(rt-aurix-lite): add watchdog health supervisor` | app/watchdog_supervisor + tests |
| 9 | `feat(rt-aurix-lite): add portable ipc abstractions` | ipc/* + tests |
| 10 | `feat(rt-aurix-lite): add app orchestration controllers` | app/* + tests |
| 11 | `feat(rt-aurix-lite): add hal interfaces` | hal/* (can, gpio, clock, watchdog) |
| 12 | `test(rt-aurix-lite): add deterministic system simulation` | platform/host + 3-domain simulator + fault injection |
| *(gated)* | `feat(rt-aurix-lite): add target walking skeleton` | ADS/iLLD project, multicore, CAN |
| *(gated)* | `feat(rt-aurix-lite): add target runtime shells` | chosen runtime adapter |
| *(gated)* | `docs(rt-aurix-lite): bring-up checklist` | bench/HIL |

---

## References

- [`architecture.md`](architecture.md) — full design (hardware identity, runtime model,
  CAN, pins, error responses, config, implementation strategy).
- [`wiring.md`](wiring.md) — harness/wiring source of truth.
- [`aurix.md`](aurix.md) — board manual transcription.
- [`protocol/README.md`](protocol/README.md) — RT-only protocol subset + regeneration.
