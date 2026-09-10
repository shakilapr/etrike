# rm-esp32-t12d — Protocol Compatibility & Pipeline Verification Report

**Scope:** Verification + report only (no firmware/emitter source changes).
**Target:** `e:\work\etrike\rm-esp32-t12d` (firmware `v0.8.0-clean-rm-t12d`).
**Date:** 2026-09-10
**Method:** Phase 1 = original YAML contract + canonical codec cross-check (Python) and
end-to-end emitter→consumer round-trip (C++). Phase 2 = consumer authority/freshness
harnesses driving the *real* rt-esp32 `SafetyStreamSupervisor` and faithful replicas of
mtr-stm32 / sys-esp32 gating. Phase 3 = static pipeline review with `file:line` evidence.

---

## How to reproduce (all run on this Windows box)

```powershell
# Phase 1a — YAML contract + canonical-codec round-trip
cd e:\work\etrike
py rm-esp32-t12d/verify/verify_yaml_contract.py

# Phase 1b — rm CanEmitter -> consumer decode (real encode path)
C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Wall -Wextra -Irm-esp32-t12d/src -I. -Ishared `
  rm-esp32-t12d/verify/verify_emitter_roundtrip.cpp -o rm-esp32-t12d/verify/verify_emitter_roundtrip.exe
.\rm-esp32-t12d\verify\verify_emitter_roundtrip.exe

# Phase 2 — consumer authority / freshness harnesses
C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Irt-esp32/src `
  rm-esp32-t12d/verify/verify_phase2_rt_authority.cpp -o rm-esp32-t12d/verify/verify_phase2_rt_authority.exe
.\rm-esp32-t12d\verify\verify_phase2_rt_authority.exe

C:\TDM-GCC-64\bin\g++.exe -std=c++17 `
  rm-esp32-t12d/verify/verify_phase2_mtr_ignition.cpp -o rm-esp32-t12d/verify/verify_phase2_mtr_ignition.exe
.\rm-esp32-t12d\verify\verify_phase2_mtr_ignition.exe

C:\TDM-GCC-64\bin\g++.exe -std=c++17 `
  rm-esp32-t12d/verify/verify_phase2_sys_freshness.cpp -o rm-esp32-t12d/verify/verify_phase2_sys_freshness.exe
.\rm-esp32-t12d\verify\verify_phase2_sys_freshness.exe
```

Verification artifacts live in `rm-esp32-t12d/verify/` (no firmware logic modified).

---

## Phase 1 — Protocol compatibility vs original YAML (PASS)

**All 17 emitted frames** (across BARE/SYS/RT) were cross-checked against the original
contracts (`protocol/contracts/{ses,seb,rt,sys,hmi,host}.yaml`): correct frame id, DLC,
byte order, codec strategy, bus, emulated sender role, and expected receiver. Each
generated-codec frame was also round-tripped (encode→decode) through the canonical codec,
and each custom ses/seb frame through the canonical vendor codec.

### Phase 1a — `verify_yaml_contract.py` (17 pass / 0 fail)
```
[PASS] BARE VCU_SES_REQ  id=0x169 dlc=8  bus=low
[PASS] BARE VCU_SEB_REQ  id=0x7B9 dlc=8  bus=low
[PASS] BARE RT_DRIVE_CMD id=0x204 dlc=5  bus=low
[PASS] BARE SYS_MODE_CMD id=0x110 dlc=2  bus=low
[PASS] BARE SYS_PWR_CMD  id=0x113 dlc=2  bus=low
[PASS] SYS  VCU_SES_REQ / VCU_SEB_REQ / RT_DRIVE_CMD  (contract+bytes OK)
[PASS] SYS  HMI_MODE_REQ id=0x111 / HMI_PWR_REQ id=0x112 / RT_HEARTBEAT id=0x7FD
[PASS] RT   HOST_STEER_CMD 0x303 / HOST_BRAKE_REQ 0x301 / HOST_DRIVE_CMD 0x300
[PASS] RT   HMI_MODE_REQ 0x111 / HMI_PWR_REQ 0x112 / HOST_HEARTBEAT 0x7FC
```

### Phase 1b — `verify_emitter_roundtrip.cpp` (17 pass / 0 fail)
Exercises the firmware's **actual** `rm::CanEmitter` and decodes every frame with the
consumer decode functions. Spot results: `0x204 speed=2500 gear=D`, `0x110 mode=AUTO(1)`,
`0x113 power=ON(1)`, `0x111/0x112 req_mode=AUTO(1)/req_start=ON(1)`, `0x301 pressure=10000 kPa`
(13.5/27.0×20000), `0x303 angle=200 (20.0°) valid`, `0x300 speed=2500 gear=D yaw=0`.
```
Round-trip checks: 17 pass, 0 fail
>>> ALL EMITTER ROUND-TRIP CHECKS PASSED <<<
```

**Conclusion:** Every wire frame rm emits is structurally and semantically compatible with
the canonical contracts and decodable by the downstream nodes.

### Cadence notes surfaced by Phase 1 (informational, not a wire incompatibility)
| Frame(s) | rm cadence | YAML `cycle_ms` | Note |
| --- | --- | --- | --- |
| `0x169` / `0x7B9` | 10 ms (100 Hz) | 20 ms | rm 2× faster than contract |
| `0x111` / `0x112` HMI | 100 ms (10 Hz) | 1000 ms | rm 10× faster than contract (`architecture.md` says 10 Hz; yaml says 1 Hz) |

Faster-than-spec is generally tolerated by freshness gates (see Phase 2 SYS), but the
**HMI 10 Hz vs 1 Hz doc/contract mismatch** should be reconciled (Phase 3 item 3).

---

## Phase 2 — "Flows seamlessly when connected" (3 scenarios)

### Scenario A — BARE connection instead of SYS+RT  →  actuators (SES/SEB/MTR)
- **SES/SEB:** `0x169`/`0x7B9` decode correctly (Phase 1 round-trip PASS).
- **MTR:** `0x110`/`0x113`/`0x204` are accepted, **but MTR ignition requires `0x011`
  SYS_SAFETY_STS** (`mtr-stm32/src/motor_manager.h:313` `ignition_on_ = power_valid_ &&
  power_state_on_ && safety_state_valid_ && mode_valid_`; `safety_state_valid_` set only by
  `handle_safety_status` on `0x011`, `:194-212`; filter installed at `can_driver.h:76`).
  **rm BARE mode does NOT emit `0x011`** (`src/can_emitter.h` BARE block only sends
  `0x169/0x7B9/0x204/0x110/0x113`).**
- **Harness `verify_phase2_mtr_ignition.cpp` (faithful replica of motor_manager.h gating):**
  ```
  [rm-BARE-only] ignition_on(ever)=NO  ignited@2s=NO     <-- MTR never ignites
  [rm-BARE+011]  ignition_on(ever)=YES ignited@2s=YES     <-- with 0x011, ignites
  ```
- **Result: NOT seamless.** MTR stays un-ignited (DAC zeroed) → motor never moves.
  Fix condition: rm BARE (and SYS) must also emit `0x011 SYS_SAFETY_STS` (estop_active=0)
  at the SYS cycle.

### Scenario B — SYS connection instead of RT  →  sys-esp32
- sys-esp32 decodes `0x204` (`main.cpp:288`), `0x111` (`main.cpp:303`), `0x112`
  (`main.cpp:325`), and requires `0x7FD RT_HEARTBEAT` within `kHeartbeatTimeoutMsRt = 1000 ms`
  (`config.h:50`); HMI freshness via `StreamValidity` 5 s (`kReqFreshTicks = 1000ms*5`).
- **Harness `verify_phase2_sys_freshness.cpp`:**
  ```
  [rm-SYS] rt_heartbeat_always_fresh=YES  hmi_always_fresh=YES   (hb@500ms hmi@100ms)
  [slow-sender] rt_heartbeat_always_fresh=NO  >> FAIL: enter_estop (hb@2000ms)
  ```
- **Result: SEAMLESS.** rm SYS cadence (0x7FD @500 ms, HMI @100 ms) keeps both watchdog
  gates fresh; sys-esp32 reaches AUTO and accepts drive. (Caveat: sys `0x204` staleness
  "200 ms→zero" is documented but unenforced — Phase 3 item 4.)

### Scenario C — RT connection instead of host  →  rt-esp32
- rt-esp32 grants motion only when
  `kMotionRequired = READY_BIT_SAFETY(0x011) | READY_BIT_MODE(0x110) | READY_BIT_HOST(0x300)`
  (`rt-esp32/src/safety_stream_loss.h:50-51`). rm RT mode emits `0x300/0x301/0x303/0x7FC/
  0x111/0x112` but **NOT `0x011` or `0x110`** (`src/can_emitter.h` RT block).
- **Harness `verify_phase2_rt_authority.cpp` (drives the REAL `SafetyStreamSupervisor`):**
  ```
  [rm-RT-only]   motion_ready(ever)=NO  ready@2s=NO  sys_safety_acquired=never
  [rm-RT+sys]    motion_ready(ever)=YES ready@2s=YES  sys_safety_acquired=200ms
  ```
- **Result: NOT seamless (by design).** Without a SYS authority source rt-esp32 stays
  UNACQUIRED; motion only proceeds if rt-esp32 is booted with `g_bench_solo_mode`
  (`main.cpp:9,1311` which disables the SYS/HOST authority + watchdog gates at
  `safety_monitor.h:167,211,227`, `main.cpp:1254`). Fix condition: rm RT mode must also
  emulate SYS by emitting `0x011` + `0x110` (or the operator must run rt in bench-solo).

---

## Phase 3 — Pipeline issues

1. **[GAP] BARE mode missing `0x011` SYS_SAFETY_STS** → MTR never ignites
   (`mtr-stm32/src/motor_manager.h:313`, `:194-212`; `rm-esp32-t12d/src/can_emitter.h` BARE block).
2. **[GAP] RT mode missing `0x011` + `0x110`** → rt-esp32 grants no motion authority
   (`rt-esp32/src/safety_stream_loss.h:50-51`; `can_emitter.h` RT block; only `g_bench_solo_mode`
   bypasses, `rt-esp32/src/main.cpp:1311`).
3. **[DOC/CONTRACT] HMI cadence mismatch** — `0x111/0x112` emitted at 10 Hz but
   `hmi.yaml` `kCycleMs=1000` (1 Hz); `architecture.md` states 10 Hz. Reconcile contract vs docs.
   `0x169/0x7B9` also 2× faster than `cycle_ms=20`. Confirm consumers tolerate over-frequency
   (Phase 2 SYS shows they do; verify rt HMI path similarly).
4. **[DOC vs CODE] sys-esp32 `0x204` staleness "200 ms→zero" is documented but unenforced** —
   `g_last_setpoint_tick` is written at `sys-esp32/src/main.cpp:293` and never read. Drive setpoint
   is not zeroed on stale `0x204`. Either implement the watchdog or fix the doc.
5. **[DOC] architecture.md omits SYS-authority requirement for RT mode** — §3.3 says RT mode
   "emulates the autonomous Host" but does not mention that rt-esp32 additionally needs `0x011`/
   `0x110` (SYS) for motion authority (Scenario C).
6. **[INTEGRATION] rm is not wired into any existing harness** — `testbench`, `simulation/
   full_system`, and `native-test` contain **zero** references to `rm::CanEmitter` / `can_emitter`
   / `emit_cluster`. The natural injection point is `HostModel` (testbench / simulation) so rm can
   exercise the real sys-esp32 / rt-esp32 decode paths continuously. Recommend adding it.
7. **[HYGIENE] rolling-counter / freshness sanity** — all consumers use 8-bit counters for
   `0x7FD`/`0x7FC` (alive_ctr) and `0x111`/`0x110` (rolling_counter); rm emits 8-bit counters
   (phase 1 round-trip PASS). No width mismatch found; flag only for regression coverage.

---

## Summary

| Phase | Check | Result |
| --- | --- | --- |
| 1 | 17 frames vs original YAML (id/dlc/bus/sender/receiver/codec) | **PASS** (0 fail) |
| 1 | 17 frames emitter→consumer decode round-trip | **PASS** (0 fail) |
| 2A | BARE → MTR (no `0x011`) | **GAP** — MTR never ignites |
| 2B | SYS → sys-esp32 (freshness) | **SEAMLESS** |
| 2C | RT → rt-esp32 (no `0x011`/`0x110`) | **GAP** — no motion authority |
| 3 | Pipeline issues | 7 items (2 real gaps, 5 doc/integration) |

**Bottom line:** Wire/codec compatibility is fully proven (Phase 1). The only functional
blockers to "seamless" standalone operation are the missing `0x011` SYS_SAFETY_STS in BARE
mode (blocks MTR ignition) and the missing `0x011`+`0x110` in RT mode (blocks rt-esp32 motion
authority) — both gated by the SYS node that rm is standing in for. SYS mode is seamless as-is.
