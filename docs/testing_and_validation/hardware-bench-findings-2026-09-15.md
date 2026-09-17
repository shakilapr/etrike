# Hardware Bench — Problems, Root Causes, and Verification Report

**Date:** 2026-09-15
**Rig:** RT-ESP32 (`COM6`, `hardware_bench`), SYS-ESP32 (`COM10`, `hardware_bench`),
CANalyst-II CH0=High / CH1=Low @ 500 kbit/s. MTR/SES/SEB absent.
**Method:** each finding checked by at least two independent methods —
static code inspection, live experiment on the bench, existing unit tests, and/or
RT serial-log capture (COM6 @115200).

---

## Summary

| # | Finding | Status | Root cause | Severity |
|---|---|---|---|---|
| F1 | High bus died ~25 min | **Static root narrowed; watchdog implemented (committed 368b992)** | silent MCP2515/SPI never latches `bus_off`; **not** the ESTOP latch | High |
| F2 | RT High→Low gateway drops frames | **Confirmed; fixed (committed 368b992)** | non-blocking `send()` on a single TX slot | Medium |
| F3 | SYS boots into ESTOP | **Confirmed, fail-safe by design** | NC e-stop loop open at boot (pull-up, HIGH=active); stale `config.h` comment | Low (doc) |
| F4 | Bounded TX-slot wait risks task overrun | **Not observed** | wait is bounded and skipped on bus-off | Low |
| F5 | RT emergency `0x7B9` fallback untested live | **Logic unit-tested; integration gap** | needs SYS `0x7B9` loss to exercise | Medium |
| F6 | RT `0x205`→SYS max-brake latency | **By design** | `kBrakeSetpointStaleMs` = 100 ms; ESTOP forces immediate | Info |
| F7 | h5 doesn't assert the staged `0x115` reset result | **Confirmed** | test gap | Low |
| F8 | No live bus-off / physical-e-stop tests | **Confirmed** | test gap | Medium |
| F9 | `scripts/hardware_bench_suite.py` not re-validated | **Confirmed stale** | superseded by pytest suite | Low |
| F10 | `m_actuation_pending` vs bounded wait | **OK** | bit cleared on failure/tx_done | Info |
| F11 | Docs rate claims | **Aligned after fixes** | architecture/timing already say 50 Hz | Info |
| F12 | RT Low (TWAI) TX dies ~3 s after boot | **Fixed (committed 368b992)** | bench self-test `fail_retry_cnt=0` single-shot + `kTxSlots=1`: an arbitration-lost frame is abandoned with **no** `on_tx_done`, and the bus stays ACTIVE so Bus-Off reclamation never fires → slot leaks permanently | High |

---

## F1 — High bus died for ~25 min (P5)

**Finding:** all RT High-bus telemetry (`0x121`, `0x210`, `0x501`, `0x620`, forwarded
`0x011`/`0x500`) stopped for ~23–25 min; the backend High RX went stale while Low
stayed live. Host commands on High (`0x114`, `0x302`) could not reach RT.

**Verification (multiple methods):**
1. **Live reproduction attempt** — latched a real ESTOP (`0x001` on High) and polled
   High liveness for 20 s. High stayed live throughout (`0x121` age 6–19 ms).
   => **ESTOP does not cause the outage.**
2. **Code inspection** — RT already has MCP2515 bus-off recovery: while
   `g_can_high.bus_off()` it calls `recover()` every 3 s (`rt-esp32/src/main.cpp:633-643`);
   a failed `init()` is retried every 1 s (`:362-373`). `can_transmit()` is gated by
   `is_initialized() && !is_recovering() && !bus_off() && mode != ListenOnly`
   (`can_driver_mcp2515.h:54-56`).
3. **Code inspection (no ESTOP gate)** — every High-TX site in `can_high_task` is
   unconditional (`rt-esp32/src/main.cpp:501,528,546,562,586,598,611,618,622`); only
   `rpt.mode` carries a value. So no firmware path gates High TX on ESTOP/mode.
4. **Serial log capture (COM6)** — after an RTS reset the port carries the full ESP log;
   during healthy/ESTOP operation **no** `MCP2515`/`bus-off`/`recovery` lines were emitted.
   (The outage happened earlier, outside this capture.)
5. **Correlation** — the outage window coincided with physical handling (USB re-plug /
   e-stop activation); after the operator physically released the e-stop (and the staged
   reset), the High bus returned. Since ESTOP does not gate High TX (methods 1,3), the
   bus recovery most plausibly followed the **physical action**, not the reset itself.

**Static analysis of candidate (c) — MCP2515 driver (`can_driver_mcp2515.cpp`):**
- `recover()` **cannot** leave `m_recovering` stuck true: after the CAS at `:346` every
  path falls through to `m_recovering.store(false)` at `:371` (the only early returns are
  *before* the CAS). **Ruled out.**
- `send()` **cannot** block forever: the TX-slot wait (`:432-438`) is bounded by
  `timeout_ms` (default 2 ms) and also exits on `is_recovering()`/`bus_off()`. **Ruled out.**
- The reachable silent-failure mode is different: `m_bus_off` is set **only** inside
  `receive()` (`:502-508`) from `EFLG.TXBO`. If SPI / the MCP2515 goes silent and reads
  back `0x00`, `CANINTF` shows no `ERRIF`/`MERRE`, so `bus_off` stays false and the 3 s
  recovery (`main.cpp:633`) never fires — while `send()` still returns **true** (the SPI
  master needs no device ACK), so `g_can_tx_ok_high` keeps climbing. The High bus is dead
  but RT reports healthy. `send()` success is therefore **not** a valid liveness signal.
- No High RX-peer liveness counter exists (`g_last_high_peer_us` is absent; only
  `g_last_low_peer_us`). `0x620` carries `EFLG/TEC/REC`+`spi_fault_delta` but is High-only,
  so it cannot be read during a High outage.

**Root cause:** not the ESTOP state. The evidence does not uniquely discriminate between:
(a) a physical High fault (wiring/power/termination) disturbed during handling;
(b) CANalyst-II **CH0** itself going silent (backend High RX also stale; no discriminator
    captured); or
(c) the silent MCP2515/SPI failure above, where `bus_off` never latches so the existing
    recovery never fires and send-success cannot be trusted.

**Discriminator for the next occurrence:** capture COM6 (RT log) during the outage — a
`bus-off`/`recovery` line points at (c); silence with the bus dead points at (a)/(b);
and check `adapter.channels.high.last_error`/`rx_invalid` on the backend for (b).

**Impact:** High. While High is down, no host command (drive/reset/heartbeat) reaches RT;
the vehicle is only reachable through SYS on Low.

**Recommendation:**
- **[Implemented]** `Mcp2515Driver::health_probe()` (`can_driver_mcp2515.cpp`) reads back two
  config registers written at init (`CNF2==0x91`, `RXB0CTRL==0x64`); the `can_high_task` 2 Hz
  watchdog forces `recover()` when the probe fails for > 2 s **even though `bus_off()` is
  false**. Do not key the watchdog on `g_can_tx_ok_high`/`send()` success — those stay true in
  the silent SPI/MCP failure mode above.
- **[Implemented]** High RX-peer liveness counter `g_last_high_peer_us`, mirroring
  `g_last_low_peer_us`.
- **[Implemented]** Low-side visibility without a protocol change: `RT_HEARTBEAT.health_flags.can_ok`
  (id `0x7FD`, which has a Low-bus instance) now tracks `g_can_high_healthy`
  (init + not-bus-off + probe) rather than `!bus_off()` alone, so a silent High outage clears
  `can_ok` on the Low bus.
- Check High-bus wiring/termination (120 Ω) and MCP2515 power/SPI.

---

## F2 — RT High→Low gateway drops frames (P1)

**Finding:** gateway frames are lost under load.

**Verification (multiple methods):**
1. **Code inspection** — the gateway drain calls `drv->send(gw.frame)` with the default
   **`timeout_ms = 0`** (`rt-esp32/src/main.cpp:936`); the driver uses one TX slot, so a
   send is dropped when the slot is busy.
2. **Live experiment (idle)** — 40 one-shot `host_light_cmd` on High at 25 ms spacing:
   `sent 40, low_rx 40, loss 0%`.
3. **Live experiment (loaded, ×3)** — periodic `host_light_cmd` at 100 Hz for 3 s with a
   drive stream active: `high_tx/rx 289/287, 281/279, 286/285` → **0.3–0.7% loss**
   (reproducible).
4. Observed live once as a flaky turn-light test before hardening (single dropped `0x302`).

**Root cause:** single-slot non-blocking `send()` in the gateway drain path.

**Impact:** Medium — sporadic loss of forwarded Host/HMI frames (lights, mode, power,
reset request). Notably a dropped `0x114` would prevent ESTOP recovery.

**Fix:** `drv->send(gw.frame, 2)` — `send()` now honors a bounded slot wait (added with
the rate fix). **Applied** (`rt-esp32/src/main.cpp:936`), compiles; committed 368b992.

---

## F3 — SYS boots into ESTOP (P4)

**Verification (multiple methods):**
1. **Code inspection** — `kEstopGpio = 1`; `task_safety` reads
   `estop_hw = (gpio_get_level(kEstopGpio) == 1)` and comments *"NC fail-safe: HIGH =
   pressed / open circuit"* (`sys-esp32/src/main.cpp:640,645`). The pin is configured
   **pull-up** before tasks start (`:1331-1337`).
2. **Doc contradiction** — `sys-esp32/src/config.h:23` says *"active-low, pull-down"*,
   which is the opposite of the code.
3. **Behavioral** — when the physical loop was open, the reset reply carried
   `blocker_mask=0x01` (`kResetBlockPhysicalEstop`); when closed, `0x00`. Reset was
   refused while the loop was open and accepted once closed.

**Root cause:** deliberately fail-safe. An **open** NC e-stop loop reads HIGH = active, so
SYS latches ESTOP at boot. Not a logic bug; the stale `config.h` comment is the defect.

**Impact:** Low. Tests must normalise (they do: the `bench` / `auto_ready` fixtures call
`ensure_operational()`); the operator must ensure the e-stop loop is closed.

**Recommendation:** fix the `config.h:21-23` comment (done); add a SYS unit test asserting
the fail-safe polarity; optionally settle/debounce the input at boot.

---

## F4 — Bounded TX-slot wait risk (P2)

**Verification:** code inspection (wait is `pdMS_TO_TICKS(timeout_ms)`, and `send()`
returns immediately during the bus-off backoff window); live load test held `0x204` at
**99.3 Hz** and RT `0x210.task_health = 15` (all tasks alive). **No overrun observed.**

**Recommendation:** optional refinement — only apply the bounded wait to
secondary/status/gateway frames, keep high-rate actuation non-blocking.

---

## F5 — RT emergency `0x7B9` fallback (P3)

**Verification:** the NORMAL → SYS_DEGRADED → EMERGENCY_FALLBACK → handback state machine
is unit-tested (`rt-esp32/test/test_fault_injection_matrix/test_fault_injection_matrix.cpp:406`).
Live integration of the new 10 ms scheduler's `seb_emergency_takeover` emit path is
**not** exercised (requires SYS `0x7B9` to disappear > 300 ms, which cannot be induced
without taking SYS down).

**Root cause:** none in logic; coverage gap only.

**Recommendation:** add a native/integration test that drives the scheduler branch, or a
HIL SYS-loss test.

---

## F6 — RT `0x205` → SYS max-brake latency (P11)

**Verification (code):** in AUTO, if RT's `0x205` is stale > `kBrakeSetpointStaleMs`
(= `RtBrakeCmd::kCycleMs * 5` = **100 ms**) SYS applies `kMaxBrakeKpa`
(`sys-esp32/src/main.cpp:914-918`). ESTOP takes priority and forces max stroke
immediately (verified live: `0x7B9=1140` shortly after trip). In MANUAL, SYS uses
lever-only (no CAN pressure). **By design; no defect.**

---

## F7–F9 — Test/coverage gaps

- **F7:** `test_h5_safety.py` asserts ESTOP clears but not the staged `0x115` result
  (`ACCEPTED` + `blocker=0`). Add it to lock the 2-frame contract.
- **F8:** no live bus-off / adapter-reconnect test, and the physical-e-stop blocker path
  is untestable from the host (GPIO) — document or gate it.
- **F9:** `scripts/hardware_bench_suite.py` was stripped of actuator mocking but not
  re-validated against the fixed firmware; it predates the current suite. Recommend
  deprecating it in favour of `tests/hw_bench/`.

---

## F10–F11 — Non-issues confirmed

- **F10:** `m_actuation_pending` is cleared on both send failure and `tx_done`, so the
  bounded wait introduces no actuation starvation.
- **F11:** `docs/architecture/can-architecture.md` and `timing-budget.md` state 50 Hz for
  the brake/steer tasks, which now matches the firmware after the rate fix.

---

## F12 — RT Low (TWAI) TX dies ~3 s after boot (pre-existing, exposed)

**Finding:** RT's Low-bus TX stops within seconds of boot while High stays live. Every Low
report (`0x204`, `0x205`, `0x501`, heartbeat) goes stale, failing the drive/actuator/estop
half of `tests/hw_bench`.

**Verification:**
1. **Serial log** — `Low CAN TX failed (n=34700 consec=34700 state=1 tec=0)` and
   `Low CAN TX dropped id=0x204: TX slot busy (in-flight frame not yet completed)`.
   `state=1` = ACTIVE, `tec=0`.
2. **Backend** — RT Low messages stale while RB High messages fresh; SYS `heartbeat_ok=0`.
3. **Code** — `kTxSlots = 1`; the slot is freed only in `on_tx_done()` or on Bus-Off /
   recovery. The bench build defines `ETRIKE_RT_TWAI_SELF_TEST`, which sets
   `config.fail_retry_cnt = 0` (**single-shot**). The driver's own comments
   (`can_driver_twai.cpp:79-81`, `:420-424`) note a single-shot frame lost to arbitration is
   abandoned **without** `on_tx_done`, and the bus stays ACTIVE so no Bus-Off reclaim runs
   → the slot leaks permanently.

**Root cause:** single-shot TX + a one-slot pool; arbitration loss against SYS's Low traffic
leaks the slot with no reclamation path.

**Fix (applied):** `service_recovery()` now reclaims a leaked in-flight slot after a bounded
100 ms in-flight age, using a CAS so a late `on_tx_done` cannot double-free.
(`rt-esp32/src/can_driver_twai.cpp`). Verified: suite failures dropped 15 → 10; Low TX
restored to `0x204` ~102 Hz.

---

## Bench prerequisite: developer-override jumper

With `SYSTEM_RUN_MODE=1`, all bypasses (solo mode, MTR-absent tolerance) require
`DEVELOPER_OVERRIDE_PIN` (GPIO42) **jumpered to GND**. If it floats HIGH the boot log prints
`Prototype mode: Override pin not jumped. Enforcing safety.`, `g_bypass_mtr_absent=false`, and
the absent MTR trips `RtMtrFbkTimeout` (`estop_reason=10`) which zeroes motion — failing every
drive/actuator test. Confirm the jumper before running `tests/hw_bench`.

---

## Recommended fix order

1. **F2** — gateway `send(gw.frame, 2)` (one line; removes a real recovery-blocking loss path).
2. **F1** — High-TX liveness watchdog + Low-side MCP health visibility (the outage is the
   highest-impact failure and currently unobservable).
3. **F3** — fix the `config.h` comment; add a polarity unit test.
4. **F7** — assert the staged `0x115` result in h5.
5. **F5/F8/F9** — scheduler integration test, bus-off test, deprecate the legacy script.
