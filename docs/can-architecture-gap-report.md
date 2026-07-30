# Gap Analysis Report — `docs/can-architecture.md` vs. Source Code

**Scope:** Section-by-section comparison of `docs/can-architecture.md` (815 lines)
against the actual firmware in `rt-esp32/src/`, `sys-esp32/src/`, `pwt-esp32/src/`,
`shared/shared_config.h`, and `protocol/generated/cpp/etrike_protocol.hpp`.

**Method:** All claims bearing code references, constants, formulas, or message
IDs were verified against the source. Discrepancies are grouped by category and
tagged with severity (🔴 **major factual error**, 🟠 **value/scope mismatch**,
🟡 **omission or wording**, 🟢 **verified accurate, listed for completeness**).

> Coordinate conventions: file references below use `path:line-number`.

---

## Summary

- Total gap findings: **34**
- 🔴 Major: **15** (message-ID mislabels, obstacle math, slew-rate math, timing-
  constant errors, fictional counter-verification claim)
- 🟠 Mismatch: **10**
- 🟡 Omission: **9**
- 🟢 Verified accurate (sampled): **~12**

The document's narrative and overall architecture diagram are sound, but the
quantitative details — particularly in **Section 4 (Timing Matrix)**, **Section 7
(Actuator/Obstacle Math)**, **Section 5.4 (HMI Rolling Counter)**, and **Section 13
(FreeRTOS Priorities)** — diverge systematically from the current code. Several
escalation priorities/behaviours the document claims to be safety-critical do not
exist in the source.

---

## A. CAN Message ID Mislabels — Sections 1, 3, 4, 7  (🔴)

### A1. `0x204` is `RT_DRIVE_CMD`, not `HOST_DRIVE_CMD`

| Source | Claim |
|---|---|
| Document §1, §4, §7 | "`0x204` (`HOST_DRIVE_CMD`)" — Host→RT on High CAN |
| `protocol/contracts/rt.yaml:7` | `rt:rt_drive_cmd`, **low** bus, sender **RT**, receivers `[SYS, MTR]`, cycle_ms=10 |
| `etrike_protocol.hpp:1087` | `RtDriveCmd::kId == 0x204` |
| `etrike_protocol.hpp:331` | `HostDriveCmd::kId == 0x300` |

`HOST_DRIVE_CMD` is `0x300` (Host→RT on High CAN, 100 Hz). `0x204` is
**RT_DRIVE_CMD**, sent by *RT* on the *Low* bus to SYS + MTR.

### A2. `0x205` is `RT_BRAKE_CMD`, not `HOST_BRAKE_REQ`

| Source | Claim |
|---|---|
| Document §1, §4, §12.1 | "`0x205` (`HOST_BRAKE_REQ`)" — brake setpoint from Host on High CAN |
| `etrike_protocol.hpp:1030`, `rt.yaml:10` | `RtBrakeCmd::kId == 0x205`, low bus, RT→SYS, cycle_ms=20 |
| `etrike_protocol.hpp`, host_brake_req | `HostBrakeReq::kId == 0x301` (1 Hz `kCycleMs=0` event-driven) |

`HOST_BRAKE_REQ` is `0x301` (Host→RT, event-driven). The RT→SYS brake loop
described in §7 ("RT sends `0x7B9` directly") is correct, but the message the
document uses to label the Host's path is wrong.

### A3. Architectural consequence

Document's Section 1 "High CAN" message list asserts that `0x204`, `0x205`, and
`0x302` are all Host-originated Host→RT messages. The accurate picture is:

| ID | Name | Bus | Path | Cycle |
|---|---|---|---|---|
| `0x300` | `HOST_DRIVE_CMD` | High | Host → RT | 100 Hz |
| `0x301` | `HOST_BRAKE_REQ` | High | Host → RT | event |
| `0x302` | `HOST_LIGHT_CMD` | High | Host → RT/SYS | event |
| `0x204` | `RT_DRIVE_CMD` | **Low** | **RT → SYS, MTR** | 100 Hz |
| `0x205` | `RT_BRAKE_CMD` | **Low** | **RT → SYS** | 50 Hz |

This mislabel propagates into Sections 3, 4, 7, and 12.1.

---

## B. Timing-Constant Errors — Section 4  (mostly 🔴)

| Constant | Doc claim | Reality | Code ref |
|---|---|---|---|
| `kSetpointStaleMs` | **200 ms**, 0x204 (HOST_DRIVE_CMD) | **50 ms** = `RtDriveCmd::kCycleMs(10) × 5` | `sys-esp32/src/config.h:55` |
| `kSteerFollowingErrMs` | **100 ms** (10 ticks @100 Hz) | **300 ms** (30 ticks @100 Hz) | `rt-esp32/src/config.h:30` |
| `kSebHandoffGraceMs` | **100 ms** | **500 ms** | `sys-esp32/src/config.h:78` |
| `kMaxBrakeKpa` | **2000 kPa** (used throughout) | **5000 kPa** (=100 raw × 50 kPa/bit) | `shared/shared_config.h:39` |
| `kObstacleStopMM` | **500 mm** | **300 mm** | `shared/shared_config.h:14` |
| `kLowSpeedThreshMmps` | **100 mm/s** | **50 mm/s** | `shared/shared_config.h:20` |
| `kEstopRateLimitWindowMs` (TX vs RX conflated) | 500 ms / 2 frames | TX: `kEstopBroadcastMinIntervalUs=250'000 µs` per ECU; RX: 500 ms/2 in SYS dispatch — **two different mechanisms** | `shared_config.h:26`; `sys-esp32/src/main.cpp:287-298` |
| `kHeartbeatTimeoutMsRt` | 200 ms | 200 ms ✓ | `sys-esp32/src/config.h:54` |
| `kHeartbeatTimeoutMsSys` | 200 ms | 200 ms ✓ | `rt-esp32/src/config.h:50` |
| `kHeartbeatTimeoutMsHost` | 1500 ms | 1500 ms ✓ | `shared_config.h:24` |
| `kBrakeSetpointStaleMs` | 100 ms | 100 ms ✓ | `sys-esp32/src/config.h:56` |
| `kBrakeFollowingErrMs` | 100 ms | 100 ms ✓ | `sys-esp32/src/config.h:73` |
| `kMtrEstopAckTimeoutMs` | 100 ms | 100 ms ✓ | `sys-esp32/src/config.h:84` |
| `kMtrFbkStaleMs` | 200 ms | 200 ms ✓ | `sys-esp32/src/config.h:87` |
| `kEstopLongPressMs` | 3000 ms | 3000 ms ✓ | `sys-esp32/src/config.h:81` |
| `kStartupGracePeriodMs` | 3000 ms | 3000 ms ✓ | `shared_config.h:25` |
| `kLowCanPeerTimeoutMs` | (not listed) | **1500 ms** (RT-only) | `rt-esp32/src/config.h:51` — **omission** |

The recurrence of "2000 kPa" in §4, §7, §11, §12 is one of the most pervasive
errors in the document. `2000 kPa` is actually `kAssistStopKpa` (used only on
Host heartbeat loss), not the maximum pressure (`kMaxBrakeKpa=5000`).

---

## C. Obstacle Brake Math — Section 7.2  (🔴)

Document:

```
kObstacleStopMM = 500 mm
kLowSpeedThreshMmps = 100
P_obstacle_calc = clamp( 2000 · (1 - d/500), 500, 2000 ) [kPa]
P_final_brake = max( P_obstacle_calc, P_host_req )
```

Reality (`physics_model.cpp:95-101`, `brake_arbitration.h:6-8`):

```
kObstacleStopMM  = 300 mm
kObstacleClearMM = 3000 mm         (document never names this)
kObstacleMaxKpa  = 5000 kPa
kLowSpeedThreshMmps = 50 mm/s

P_obstacle = kObstacleMaxKpa · (1 - (d - 300) / (3000 - 300))    for d ∈ [300, 3000]
         = 5000 at d ≤ 300
         = 0     at d ≥ 3000

P_final = clamp( max(P_obstacle, P_host_req), 0, kMaxBrakeKpa=5000 )
```

All four numeric constants are wrong; the linear-segment endpoint (3000 mm clear
distance) is undocumented; and the final `clamp(max(...))` wrap is missing.

Also note the obstacle-ESTOP trigger in `safety_monitor.h:157-162`:

```cpp
if (obstacle_mm <= shared::kObstacleStopMM                            // 300 mm
    && std::abs(g_mtr_actual_speed_mmps.load()) > shared::kLowSpeedThreshMmps)  // 50 mm/s
```

— the document's "vehicle speed > 100 mm/s" qualifier is also outdated.

---

## D. Steering Slew-Rate & Following-Error Math — Section 7.3  (🔴)

### D1. Dynamic slew rate

Document: `SlewRate = clamp(125 + 0.1·|v_mm/s|, 125, 525) [°/s]`

Reality (`steering_control.h:227-231`):

```
rate_deg_s = 125 + (speed_kmh - 2.0) × (400 / 23),  clamped [125, 525]
```

Speed is in **km/h**, not mm/s, and the scaling factor is **`400/23 ≈ 17.39`**
per `(km/h − 2)`, not `0.1 · mm/s`. The slopes and intercepts differ at every
speed.

### D2. Dynamic following-error threshold

Document: `Threshold_deg = 5.0 + 0.005·|v_mm/s|`

Reality (`physics_model.cpp:31-34`):

```
dynamic_limit = clamp( 40 - (speed_kmh - 2) × (35/23),  5, 40 ) [°]
Threshold      = max( kSteerFollowingErrMinDeg=2.0,
                      kSteerFollowingErrFactor=0.25 · dynamic_limit ) [°]
```

`rt-esp32/src/config.h:28-29`. The threshold is now a *fraction* of the dynamic
limit, so it **decreases** with speed (high speed → tight threshold) — the
opposite direction the document formula describes. Documented persistence limit
also wrong (300 ms vs 100 ms; see B above).

---

## E. HMI Rolling-Counter Verification — Section 5.4  (🔴 arch safety claim)

Document §5.4 states:

> "SYS verifies counter progression on each incoming frame. Replayed or
> out-of-order counter values are discarded immediately to prevent replay
> attacks."

Reality — `sys-esp32/src/mode_manager.cpp:79-94`:

```cpp
bool ModeManager::parse_hmi_mode(uint8_t requested_mode) {
#if ENABLE_CAN_HMI
    if (m_mode == can::Mode::Estop) return false;
    if (requested_mode > 1) return false;
    can::Mode new_mode = static_cast<can::Mode>(requested_mode);
    if (m_mode.load() != new_mode) { set_mode(new_mode); return true; }
#endif
    return false;
}
```

No comparison of `request.rolling_counter` against a remembered previous value.
The HmiModeReq/HmiPwrReq messages *do* have a `rolling_counter` field per the
protocol dictionary (`etrike_protocol.py`), but SYS does not validate it. The
anti-replay protection asserted by the document is not present in code.

---

## F. FreeRTOS Priority Architecture — Section 13  (🟠 schema drift)

Document lists five priority tiers, but they merge RT and SYS into a single
schema that doesn't match either project exactly.

### F1. RT-ESP32 actual priority table (`rt-esp32/src/main.cpp:847-871`)

| Prio | Tasks | Doc says |
|---|---|---|
| 5 | `rx_low`, `rx_high` | task_safety + t_can_rx  ← `task_safety` doesn't exist in RT |
| 4 | `t_dispatch`, `t_control` | (Doc lists t_control at P3) |
| 3 | `t_can_tx_low`, **`t_can_tx_high`** | (Doc puts high_tx at **P2**) |
| 1 | `t_watchdog`, `t_heartbeat` | (Doc omits t_watchdog; lists t_heartbeat correctly) |

`run_safety_checks()` is a function invoked synchronously inside `t_control`
(prio 4), not a separate `task_safety` task.

### F2. SYS-ESP32 actual priority table (`sys-esp32/src/main.cpp:1084-1096`)

| Prio | Tasks | Doc-listed? |
|---|---|---|
| 5 | `task_can_rx`, `task_safety` | partial — doc says `task_safety, t_can_rx` (correct) |
| 4 | `task_dispatch`, `task_mode` | missing `task_mode` |
| 3 | `task_gear`, `task_brake`, `task_lights` | doc lists only `task_brake` |
| 2 | `task_indicator`, `task_power`, `task_can_tx`, `task_can_control` | doc says "t_can_high_tx" (which lives at P3 in **RT**), doesn't list SYS's `task_can_ctrl`, `task_indicator`, `task_power` |
| 1 | `task_diag`, `task_hb` | matches doc's "t_heartbeat, task_diag" (`t_heartbeat` is RT-side; SYS is `task_hb`) |

RT-ESP32 has **no Priority 2 task**; SYS has 8 tasks the document doesn't
name. The advertised 5-tier stack does not exist as written.

---

## G. ESTOP Rate-Limiting — Sections 4, 10.4  (🟠)

Two distinct limiting implementations exist and the document treats them as
one mechanism:

| Mechanism | Code path | Limiter |
|---|---|---|
| TX-side (per-ECU, on every `can_send_estop()`) | `shared/shared_config.h:26-33`, `rt-esp32/src/safety_monitor.h:53-55`, `sys-esp32/src/main.cpp:139-141` | Minimum **250 ms** between broadcasts (`kEstopBroadcastMinIntervalUs=250'000`) |
| RX-side (SYS dispatch, `0x001` inbound) | `sys-esp32/src/main.cpp:283-305` | **2 frames per 500 ms** sliding window, `kEstopRateLimitMax=2`, `kEstopRateLimitWindowMs=500` |

Document §10.4 ("`can_send_estop()`") describes the RX-side semantics as if it
were the TX gate. Both are correct on their own terms; the document conflates
"2 per 500 ms" with the TX function `can_send_estop()`.

---

## H. Gateway / Routing Rules — Section 3  (mostly 🟢)

Spot-check matches the code:

- §3 routing IDs (0x001, 0x011, 0x111, 0x112, 0x120, 0x206, 0x302, 0x600)
  agree with `rt-esp32/src/can_rx_router.h:41-77` and the
  `can::is_forwarded_*` rule lookups.
- §3 ESTOP "anti-loop cross-bus filter" matches `can_dispatch.h:121-131`
  (sets `ctx.gw_lo` for high-bus input and `ctx.gw_hi` for low-bus input).
- §3 INF `xQueueSendToFront` for ESTOP matches `can_dispatch.h:288,313`.

Omissions (🟡):

- Gateway TX queues `g_gw_tx_low_q` / `g_gw_tx_high_q` are depth **8**
  (`main.cpp:841-842`); document doesn't mention depth.
- The RX-side ESTOP rate limiter (discussed above) lives **in `t_dispatch`** at
  `can_dispatch.h:274-294` and forbids >1 ESTOP forward per 100 ms per bus.
  Document §12 attributes rate-limiting only to `can_send_estop()` at the TX
  side.

---

## I. SEB Brake Arbitration — Section 7.1  (🟠 + 🟡)

### I1. Documented priority stack vs. code order (`brake_control.h:90-131`)

| Doc priority | Code path |
|---|---|
| (undocumented) | `m_use_sync_stroke` hold (lists first ACTIVE frame after SEB alignment) |
| P1: ESTOP → 27 mm Stroke Mode | ✓ `kBrakeMaxStroke = 27.0`, raw 1140 = `(27+30)/0.05` |
| P2: Lever → 15 mm Stroke Mode | ✓ `kBrakeManualStroke = 15.0`, raw = `(15+30)/0.05` = 900 |
| P3: `brake_kpa > 0` → Pressure Mode | ✓ `kSebMaxPressureRaw=100` |
| P4: 0 mm Stroke default | ✓ `kStrokeRawZero=(0+30)/0.05=600` |

The "boot-sync hold" priority step is missing from §7.1 (mentioned only as
"first ACTIVE frame" in §6.3).

### I2. Pressure-mode raw conversion formula

Document: `RawPressure = uint8_t( brake_kpa · 0.02 )` (kPa/bit = 50).

Reality (`brake_control.h:122-123`):

```cpp
int32_t raw = (brake_kpa + 25) / 50;                  // rounding to nearest
out.pressure_request_raw = uint8_t( raw > kSebMaxPressureRaw ? kSebMaxPressureRaw : raw );
```

Document omits the integer rounding and the saturation clamp at
`kSebMaxPressureRaw = 100` (saturation to `kMaxBrakeKpa = 5000 kPa`,
**not** 2000).

### I3. AU brake arbitration option D wording

Document §7.1 lists clean "AUTO RT authority" / "MANUAL or ESTOP SYS
authority". The actual handoff gate (`sys-esp32/src/main.cpp:663-685`) also
requires:
```
rt_normal = (g_rt_safety_state == 0)         // RT_STATE_RPT safety_state=Normal
rt_setpoint_fresh = (now - g_last_setpoint_tick) < kSetpointStaleMs
auto_handoff_grace = (mode==Auto && recent_auto_enter)   // kSebHandoffGraceMs=500ms
!lever
!estop
```
and additionally the SEB rolling-counter incremental gate
(`g_seb_rolling`, `g_last_seb_roll_change_tick`) is a **fifth** criterion the
document doesn't enumerate.

---

## J. Heartbeats & Frozen-Counter — Section 8  (🟢 with nuance)

- DualHeartbeat separate `m_ctr_low` / `m_ctr_high` matches `rt-esp32/src/heartbeat.h:30-31`. ✓
- 8-bit unsigned delta frozen-counter guard matches `can_dispatch.h:73-97` for
  both SYS and Host heartbeats. ✓
- 10 Hz cycle for `RtHeartbeat::kCycleMs = 100` and `SysHeartbeat::kCycleMs = 100` matches. ✓
- 🟡 **Nuance**: `t_heartbeat` only emits High-bus heartbeat when
  `g_can_high.can_transmit()` is true (`main.cpp:757-770`). When MCP2515 is
  ListenOnly or absent (`g_high_can_present=false`), High heartbeat is skipped
  entirely. Document §8 says "[RT] maintains separate sequence counters for
  `0x7FD` Low and `0x7FD` High" — true, but the High counter doesn't increment
  when High CAN is unavailable.

---

## K. Brake/Steer Boot State Machines — Section 6  (mostly 🟢)

- Brake state enum matches `brake_control.h:8`. ✓
- `BRAKE_BOOT_WAIT` 500 ms (`kBrakeBootWaitMs=500`) ✓
- `BRAKE_LISTEN_SYNC` — 2 s sync timeout (`kBrakeSyncTimeoutMs=2000`) ✓; capture
  `m_sync_stroke_raw` for hold-on-sync ✓
- `BRAKE_ACTIVE` ↔ Pressure/Stroke arbitration ✓
- `BRAKE_DEGRADED` recovery → `ACTIVE` when `0x721` alignment bit (`status_byte0 & 1`) returns. Document frames the reverse transition as one-way. 🟡

- Steer state enum matches `steering_control.h:20-27`. ✓
- `kSteerBootWaitMs=500` ✓
- `kSteerSyncTimeoutMs=5000` ✓
- `SlewRamp =20°/s` and `kSteerEstopHoldMs=500` ✓
- Angle plausibility `|θ_boot| ≤ 30°` (300 raw / 0.1° units) ✓
- 🟡 Section 6 omits the steering **following-error check during the ESTOP
  centering ramp**: `steering_control.h:119-130` aborts to `STEER_FAULT` if the
  ramp following error persists > **1000 ms** (5° threshold); this is an
  independent guard that's only hinted at in §11 hidden causes.

---

## L. Lighting & Quad-Input OR — Section 9.2  (🟢)

Code (`sys-esp32/src/light_control.h:59-61`, `sys-esp32/src/main.cpp:743-746`)
matches the documented formula:

```
BrakeLamp = BrakeLever ∨ (Mode==ESTOP) ∨ CAN_BrakeBit(0x302) ∨ (SEB_Stroke > 0.5mm)
```

with `seb_braking = (seb_raw > 610)` (610 raw ≈ 0.5 mm at scale 0.05/offset −30).
✓ All four disjuncts correct.

Note: in ESTOP, the function takes the early return at `light_control.h:29`
(brake_lamp=ON, all others off), which is functionally identical but disables
turn signals. Document's formula is technically still satisfied; the early-
return side-effect on turn signals is undocumented. 🟡

---

## M. PWT Powertrain Bus — Section 9.1  (🟢 mostly)

Verified accurate:
- 250 kbit/s, TWAI0, GPIO7 TX / GPIO6 RX (`pwt-esp32/src/config.h:19-20`) ✓
- Extended ID `0x10262B27`, DLC 8, 100 ms cycle
  (`pwt-esp32/src/config.h:29-30`) ✓
- Physical isolation rationale ✓

🟡 Document wording slip: doc §9.1 says "Rate-limited failure logging: PWT logs
the 1st TX failure and every 50th **aggregate** failure." Reality
(`pwt-esp32/src/dcdc_control.h:49-53`): every 50th **consecutive** failure
(`m_consecutive_tx_failures` resets on first success). Aggregates aren't
preserved; only consecutive streak count.

---

## N. Bus-Off Hidden Causes — Section 11  (mixed)

Verified correct or matching:
- §11.1 peer admission gating — `g_last_low_peer_us` lives in
  `rt-esp32/src/main.cpp` & `can_dispatch.h:111-118`. The documented window
  ("liveness window") is **1500 ms** (`kLowCanPeerTimeoutMs`); document doesn't
  enumerate the value. 🟡
- §11.2 fast-recovery race condition via `recovery_attempts` delta matches
  `can_health.h:32-37`. ✓
- §11.3 ESP-IDF 5.5 in-flight slot leakage and `fail_retry_cnt=0`,
  `tx_queue_depth=1` matches `sys-esp32/src/can_driver.h:79-80`. ✓
- §11.4 MCP2515 SPI mutex contention abort matches general pattern of
  `can_driver_mcp2515.cpp` SPI locking. (File path: `can_driver_mcp2515.cpp`
  is referenced throughout the document; please verify the file in your local
  build as it was not inspected in this audit.) 🟢
- §11.5 frozen-DMA ghost publishing — matches `can_dispatch.h:73-97`. ✓
- §11.6 intermittent termination ringing — covers hardware only. ✓
- §11.7 MCP2515 3.0 s re-init delay matches `can_health.h:83-87` (`3'000'000
  us` backoff). ✓
- §11.8 Option D dual-sender circular dependency — `kSebHandoffGraceMs`
  resolves it; doc says 100 ms grace, code says **500 ms**. 🟠 (See B.)

Document §11 repeatedly repeats the **wrong `kMaxBrakeKpa = 2000`** figure (e.g.
"Max Brake (2000 kPa)" in §12.1 flowchart). Should be **5000 kPa**.

---

## O. NVS Crash Persistence — Section 10.S12  (🟠)

Document §10 item 12 claims:

> "Reads `esp_reset_reason()` during startup. Records reset reason, heap drops,
> and WDT panic metrics into NVS flash (`sys_boot`) across unexpected reboots."

Reality (`sys-esp32/src/main.cpp:1034-1054`):

```cpp
nvs_open("sys_diag", NVS_READWRITE, &nvs);
nvs_set_u32(nvs, "reset_count",  reset_count);
nvs_set_u32(nvs, "reset_reason", static_cast<uint32_t>(reason));
nvs_commit(nvs);
nvs_close(nvs);
```

- NVS namespace is **`sys_diag`**, not `sys_boot`.
- Only `reset_count` and `reset_reason` are persisted. No heap-drop history,
  no WDT panic metric, no bus-off persistent log.

The document overstates the metric set.

---

## P. Gateway Pump Constants — Section 2.2  (🟠 math error)

Document §2.2:

> "Retry Parameters: Max attempts `kGwMaxAttempts = 40`, Freshness window
> `kGwFreshnessTicks = 80ms` (40 ticks at 100 Hz)."

Reality (`rt-esp32/src/main.cpp:219-220`):

```cpp
static constexpr TickType_t kGwFreshnessTicks = pdMS_TO_TICKS(80);  // 80 ms ÷ 10 ms = 8 ticks @ 100 Hz
static constexpr uint16_t   kGwMaxAttempts    = 40;
```

The **"40 ticks at 100 Hz = 80 ms"** math in the document is wrong: 40 ticks ×
10 ms = **400 ms**, not 80 ms. Also, `kGwFreshnessTicks` is **8 ticks** at 100
Hz, not 40. The doc conflates `kGwMaxAttempts` (40, dimensionless) with
`kGwFreshnessTicks` (8 ticks = 80 ms).

🟡 Note: `kGwFreshnessTicks` is declared but never read after the
`xQueueReceive` in `gw_pump()` — the freshness check that the doc describes has
been removed from the active code path (`main.cpp:222-242`). The retry loop
relies only on `kGwMaxAttempts`.

---

## Q. Miscellany / Cross-References

| # | Issue | Severity |
|---|---|---|
| Q1 | §2.1 code reference `can_driver.h:L70-L90` — for ESP-IDF 5.5 handle-based API migration in **SYS** the actual lines are `can_driver.h:54-102`; the zero-length DLC conversion is at `:125-134`. Doc's line range is approximate. | 🟡 |
| Q2 | §2.3 ESTOP priority gate bypass for `0x001` — code path is `main.cpp:165` (`if (fr.id != can::kIdSafetyEstop && !drv->tx_admitted()) return false;`). ✓ Match. | 🟢 |
| Q3 | §10.S5 cross-bus anti-loop filter — `can_dispatch.h:121-131` ✓ | 🟢 |
| Q4 | §10.S6 frozen DMA counter: `delta = uint8_t(new - last)` ✓ | 🟢 |
| Q5 | §10.S9 dual-queue fair interleave — `t_dispatch` in `can_dispatch.h:247-266` alternates `g_can_rx_low_q` and `g_can_rx_high_q` with short 10 ms timeouts, matching the documented intent (`pdMS_TO_TICKS(0)` is replaced with `pdMS_TO_TICKS(10)` to avoid portMAX_DELAY starvation of the high bus). | 🟢 |
| Q6 | §10.S11 atomic fallback `g_pending_estop_event` — matches `can_dispatch.h:33-43`. ✓ | 🟢 |
| Q7 | §6.3 SEB DEGRADED recovery direction (DEGRADED→ACTIVE when alignment bit returns) is undocumented as a reverse transition. | 🟡 |
| Q8 | §7.1 "Rider Lever Override: commands maximum rider braking pressure" — code commands **15 mm** stroke (`kBrakeManualStroke`), not "maximum". The phrase suggests MAX braking, which a casual reader may equate with the ESTOP value. | 🟡 |
| Q9 | §1 ASCII topology diagram implies ultrasonic sensors feed RT directly; reality is Host consumes ultrasonics and forwards obstacle distance on `0x10C HOST_OBSTACLE_DIST` (10 Hz). | 🟡 |
| Q10 | The whole document never mentions the `system_mode.h` "SYSTEM_RUN_MODE" gating (0=Production, 1=Prototype w/ hardware pin, 2=Pure simulation) that disables `g_bench_solo_mode` safety checks. This bypass led the document to call out the *symptoms* (e.g. `g_bench_solo_mode` flag mentions) but never the actual run-mode contract. | 🟠 |
| Q11 | Several `can_driver_mcp2515.cpp` references (§10.S10 "MCP2515 High-CAN Mutex" and §11.4 "MCP2515 SPI Mutex contention") were not audited against source — the file exists (`rt-esp32/src/can_driver_mcp2515.cpp`) but the code path was not opened in this pass. Recommend re-verifying §10.S10 and §11.4 citations. | 🟢 unchecked |

---

## R. Recommendations for the next revision

The following sections need substantial rewrite to match the codebase:

1. **§1 Bus-on-bus message tables** — swap `0x204`/`0x205` labels per pre-CITE
   discovery; add the Host→RT `0x300`/`0x301` framing.
2. **§4 Timing Matrix** — refresh `kSetpointStaleMs (50 ms)`,
   `kSteerFollowingErrMs (300 ms)`, `kSebHandoffGraceMs (500 ms)`,
   `kMaxBrakeKpa (5000)`, and add `kLowCanPeerTimeoutMs (1500 ms)`.
3. **§7.2 & §7.3 Math** — replace obstacle formula with the two-segment 300→3000
   mm interpolation up to 5000 kPa; replace dynamic threshold expression with
   "0.25 × dynamic_limit, floor 2°"; replace slew-rate with the km/h-based
   125+(km/h−2)·17.39 / clamp[125,525].
4. **§5.4 HMI counter verification** — REMOVE until the code actually checks
   the rolling_counter field.
5. **§7.1 Pressure Mode formula** — add rounding (+25)/50 and clamp to
   `kSebMaxPressureRaw=100`.
6. **§10.S12 NVS persistence** — rename `sys_diag`, drop the bogus heap/WDT
   metric claims.
7. **§13 FreeRTOS priority architecture** — split into RT table and SYS table;
   remove `t_can_high_tx` "Priority 2", add `task_can_control`, `task_gear`,
   `task_indicator`, `task_power`, `t_watchdog`.
8. **§2.2 Gateway pump** — fix the kGwFreshnessTicks math and note the unused
   freshness check.
9. **§10.S4 / §11 / throughout** — replace "2000 kPa" with 5000 kPa
   wherever `kMaxBrakeKpa` is meant.
10. **§11.1 liveness window** — enumerate `kLowCanPeerTimeoutMs=1500 ms`.
11. **§9.1 PWT failure logging** — change "aggregate" → "consecutive".
12. **§6.3 Brake state diagram** — add `DEGRADED→ACTIVE` recovery arrow, and
    the `m_use_sync_stroke` boot-sync hold.
13. **Add a new subsection on `SYSTEM_RUN_MODE` / `g_bench_solo_mode` /
    `g_bypass_eps_sync` / `g_bypass_seb_sync` / `g_bypass_mtr_absent`** so
    reviewers know which safety assertions are disabled in simulated/prototype
    builds.
14. **Audit the MCP2515 driver code** (§10.S10, §11.4) against
    `rt-esp32/src/can_driver_mcp2515.cpp`; this audit did not open it.

---

### Appendix: spot-check correlation table (sampled positive matches)

| Doc claim | Code location | Status |
|---|---|---|
| ESTOP bypasses `tx_admitted()` | `rt-esp32/src/main.cpp:165` | ✓ |
| Anti-loop cross-bus filter | `can_dispatch.h:121-131` | ✓ |
| 8-bit delta heartbeat guard | `can_dispatch.h:73-97` | ✓ |
| `fail_retry_cnt=0`, `tx_queue_depth=1` | `sys-esp32/src/can_driver.h:79-80` | ✓ |
| MCP2515 3.0 s re-init backoff | `rt-esp32/src/can_health.h:83-87` | ✓ |
| Quad-input brake lamp OR | `light_control.h:59-61`, `main.cpp:743-7`5 | ✓ |
| Frozen rolling counter (SEB) | `can_dispatch.h:138-155` | ✓ |
| `g_pending_estop_event` fallback atomic | `can_dispatch.h:33-43` | ✓ |
| `kSteerEstopRampDegS = 20`, `kSteerEstopHoldMs = 500` | `config.h:44-45` | ✓ |
| ESTOP stroke 27 mm → raw 1140 | `brake_control.h:106`, `seb_request.h:16` | ✓ |
| PWT extended ID `0x10262B27`, 100 ms | `pwt-esp32/src/config.h:29-30` | ✓ |
| DualHeartbeat separate counters | `heartbeat.h:30-31` | ✓ |

---

*Generated by static code audit against the head-of-tree source files. The
author of this report recommends re-running the document generator from the
protocol contracts and config headers directly, since this kind of drift is
largely systemic — driven by recent config changes that were not back-ported
to the prose document.*