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
- **UPDATE (found during testbench integration):** MTR additionally requires a
  `0x113` **power REARM edge** (OFF→ON) after boot — `ignition_on_` includes
  `(!rearm_required_ || rearm_observed_)` and `rearm_observed_` is set only after
  an observed OFF→ON power command (`mtr-stm32/src/motor_manager.h:166-180`). rm BARE
  emitted an always-ON `0x113`, so it never re-armed. Fix requires **both** `0x011`
  **and** a startup `0x113` rearm edge.
- Fix condition: rm BARE must emit `0x011 SYS_SAFETY_STS` (estop_active=0) plus a
  one-shot `0x113` OFF→ON edge at boot (not required in SYS mode — the real SYS owns these).

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

1. **[FIXED] BARE mode missing `0x011` SYS_SAFETY_STS and `0x113` rearm edge** → MTR never ignites
   (`mtr-stm32/src/motor_manager.h:313/:194-212/:166-180`; `rm-esp32-t12d/src/can_emitter.h` BARE block).
2. **[FIXED] RT mode missing `0x011` + `0x110`** → rt-esp32 grants no motion authority
   (`rt-esp32/src/safety_stream_loss.h:50-51`; `can_emitter.h` RT block; only `g_bench_solo_mode`
   bypasses, `rt-esp32/src/main.cpp:1311`).
3. **[DOC/CONTRACT] HMI cadence mismatch** — `0x111/0x112` emitted at 10 Hz but
   `hmi.yaml` `kCycleMs=1000` (1 Hz); `architecture.md` states 10 Hz. Reconcile contract vs docs.
   `0x169/0x7B9` also 2× faster than `cycle_ms=20`. Confirm consumers tolerate over-frequency
   (Phase 2 SYS shows they do; verify rt HMI path similarly).
4. **[FIXED] sys-esp32 `0x204` staleness "200 ms→zero" is documented but unenforced** —
   `g_last_setpoint_tick` was written at `sys-esp32/src/main.cpp:293` and never read. `task_safety`
   now zeroes `g_setpoint_speed_mmps` and forces neutral on stale `0x204`.
5. **[FIXED] architecture.md omits SYS-authority requirement for RT mode** — §3.1 states RT mode
   "emulates the autonomous Host" but does not mention that rt-esp32 additionally needs `0x011`/
   `0x110` (SYS) for motion authority (Scenario C).
6. **[FIXED] rm is not wired into any existing harness** — `testbench` now includes
   `RmOperatorModel` (wraps `rm::CanEmitter`) and a Section 4 integration suite exercising the real
   sys-esp32 / rt-esp32 / MTR models.
7. **[HYGIENE] rolling-counter / freshness sanity** — all consumers use 8-bit counters for
   `0x7FD`/`0x7FC` (alive_ctr) and `0x111`/`0x110` (rolling_counter); rm emits 8-bit counters
   (phase 1 round-trip PASS). No width mismatch found; flag only for regression coverage.

---

## Resolution (implemented after verification)

| Item | Fix | Commit |
| --- | --- | --- |
| BARE missing `0x011` | rm BARE emits `SYS_SAFETY_STS` (estop_active=0, valid E2E CRC) | `43e8b6e` |
| BARE never rearms | rm BARE emits one-shot `0x113` power OFF→ON REARM edge | `3e4ef5d` |
| RT no authority | rm RT also emits `0x011` + `0x110` (emulated SYS) | `320625b` |
| sys `0x204` staleness unenforced | `task_safety` zeroes speed + forces neutral on stale `0x204` | `ad631d5` |
| Doc gaps | `architecture.md` §3.1/§3.3 document `0x011`/`0x110` authority | `c8e8dcf` |
| No rm harness | `RmOperatorModel` wired into `testbench` (Section 4 integration suite) | testbench integration |
| Cadence mismatch (`hmi.yaml` 1 Hz vs 10 Hz) | Doc-only; YAML intentionally untouched (avoids shared-code regen) | `c8e8dcf` |

**Post-fix verification:**
- `verify_emitter_roundtrip.cpp`: **75/75** checks pass (incl. `0x011` decode + `[0,0,1]` rearm edge).
- `testbench` full suite: **ALL TESTS PASSED** — BARE ignites MTR, SYS reaches AUTO, RT reaches rt-esp32.

## End-to-End Signal Flow (per mode)

Testbench Section 5 injects operator demands (steer **20.0°** → 30200 raw, throttle
**2000 mm/s**, brake **15.0 mm**) into rm and traces them through the intermediate
controllers to the end units. Models were extended to mirror the real controllers
(`RtNode` forwards `0x303`→`0x169` and `0x301`→`0x205`; `SysNode` applies `0x205`
to `0x7B9`).

| Mode | Path | SES angle | SEB stroke | MTR | Result |
| --- | --- | --- | --- | --- | --- |
| BARE | rm → SES/SEB/MTR (direct) | 30197 (30200) | 15.0 mm | traction=YES, dac=1544, gear=D | **PASS** |
| SYS  | rm→SES ; rm→SYS→SEB ; rm→MTR | 30197 | 15.0 mm | traction=YES, AUTO | **PASS** |
| RT   | rm→RT→SES ; rm→RT→SYS→SEB ; rm→RT→MTR | 30197 | 15.0 mm | traction=YES, AUTO, cmd=2000 | **PASS** |

**Finding fixed here:** in SYS mode rm originally emitted `0x7B9` directly *and*
`sys-esp32` emits `0x7B9` (its sole SEB command), so the two producers collided and
operator braking was unreliable. rm SYS now sends `0x205 RT_BRAKE_CMD` (kPa) and lets
SYS apply it to SEB (commit `7cd41e1`). One SYS test nuance: `SYS` requires a healthy
`0x7FD` heartbeat before leaving MANUAL, and rm emits it at only 2 Hz, so START must be
pressed after the first heartbeat.

## Host-Driven Vehicle Behavior (no rm)

Testbench Section 6 drives the vehicle through the **real Host node** (`Host→RT→SYS→
actuators`) with rm inert, confirming the vehicle behaves as commanded:

| Behavior | Command | Observed | Result |
| --- | --- | --- | --- |
| Forward | gear D, 1500 mm/s | MTR ready, gear D, DAC=1333, target=1500 | **PASS** |
| Reverse | gear R, −400 mm/s | MTR gear R, DAC=1712, target=−400 | **PASS** |
| Steering | 20.0° | SES rack angle 30197 (30200 raw) | **PASS** |
| Brake | 10000 kPa | SEB stroke 13.5 mm (RT `0x205` → SYS `0x7B9`) | **PASS** |
| ESTOP | drive + hardware button | wheels stop (DAC→0), SEB full 27 mm, estop latched | **PASS** |

Signals to the intermediate controllers were verified to exit to the units in all
three rm modes (Section 5) and via the direct Host path (Section 6). rm and Host
paths produce identical actuator outcomes for the same demand.

## Summary

| Phase | Check | Result |
| --- | --- | --- |
| 1 | 17 frames vs original YAML (id/dlc/bus/sender/receiver/codec) | **PASS** (0 fail) |
| 1 | frames emitter→consumer decode round-trip | **PASS** (75 checks after fixes) |
| 2A | BARE → MTR | **FIXED** — `0x011` + `0x113` rearm edge; MTR ignites |
| 2B | SYS → sys-esp32 (freshness) | **SEAMLESS** |
| 2C | RT → rt-esp32 | **FIXED** — rm emits `0x011`/`0x110`; motion authority granted |
| 3 | Pipeline issues | 7 items (2 functional fixed in rm, sys watchdog fixed, docs updated, rm integrated) |

**Bottom line:** Wire/codec compatibility is proven (Phase 1). The two functional blockers
(BARE: MTR never ignited — needed `0x011` *and* a `0x113` rearm edge; RT: no motion authority —
needed `0x011`+`0x110`) are now fixed in `rm-esp32-t12d`, the sys-esp32 `0x204` watchdog is
enforced, and rm is integrated into the testbench with all scenarios passing.
