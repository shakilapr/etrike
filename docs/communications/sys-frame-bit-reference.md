# SYS frame bit reference — `0x500 SYS_NODE_STATUS` & `0x600 SYS_DIAG_RPT`

Bit-level reference for the two SYS observational status frames introduced by
commit `0d9be0b6` (`feat(protocol)!: redesign 0x500 SYS_NODE_STATUS + 0x600 SYS_DIAG_RPT`).

| Source of truth | Location |
|---|---|
| Contract (field layout, DLC, cycle, buses) | `protocol/contracts/sys.yaml` (`sys_node_status`, `sys_diag_rpt`) |
| Blocker vocabulary constants | `sys-esp32/src/node_status.h` (`sys::kBlk*`) |
| Frame builders | `sys-esp32/src/main.cpp` (`build_sys_node_status()` @ ~1235, `task_diag()` @ ~1328) |
| Hardware pins / polarity | `sys-esp32/src/config.h` |
| Old-family sibling frames | `0x501 RT_NODE_STATUS`, `0x502 MTR_NODE_STATUS` — intentionally **unchanged** (8-byte family with `node_state`) |

## Conventions

- **Byte order:** big-endian (Motorola). Multi-byte fields transmit MSB first.
- **Bit numbering:** bit 0 = least-significant bit of each byte. `byte.bit` in this doc means *byte index, bit index within that byte*. The absolute start bit on the wire is `byte*8 + bit`.
- **Reserved bits:** transmitted as 0. The generated codec rejects frames where a reserved bit is nonzero (`constant_mismatch`), so decoders can rely on them being 0.
- **Booleans:** every 1-bit field is a plain 0/1 flag — never multi-bit packed enums unless shown as such.

## Frame summary

| Frame | CAN ID | DLC | Cycle | Buses | Sender → Receivers |
|---|---|---|---|---|---|
| `SYS_NODE_STATUS` | `0x500` | 6 | 200 ms (5 Hz) | Low; High via RT same-frame forward | SYS → RT (→ Jetson) |
| `SYS_DIAG_RPT` | `0x600` | 8 | 1000 ms (1 Hz) | Low; High via RT same-frame forward | SYS → RT, Host |

Both frames are **strictly observational**: they never change mode or authority.
Authority (ESTOP latch, mode, power) lives on `0x011` / `0x110` / `0x113`.

---

# 0x500 SYS_NODE_STATUS (DLC 6)

Execution outcome, readiness, bypass flags, the drive-blocker bitmask, raw
cockpit hardware inputs, and the **final executed** relay/lamp outputs.

## Byte 0 — command execution (from `0x204 RT_DRIVE_CMD`)

| Bit | Signal | 1 means | 0 means | Driven by |
|---|---|---|---|---|
| 0 | `command_received` | last `0x204` drive command arrived ≤ 200 ms ago (fresh) | no setpoint stream, or stale > 200 ms | `g_last_setpoint_tick` (task_dispatch) |
| 1 | `command_nonzero` | latest `0x204` speed magnitude ≠ 0 | commanded speed is 0 (idle) | `g_setpoint_speed_mmps` |
| 2 | `command_executing` | the setpoint is actually being passed to RT this cycle: nonzero **and** no ESTOP **and** no traction inhibit **and** run-latch enabled **and** mode == AUTO | command not propagated (idle, blocked, or estopped) | `build_sys_node_status()` |
| 3 | `command_rejected` | a nonzero command is present but *not* executing → consult block mask | nothing to execute, or executing normally | derived: `nonzero && !executing` |
| 4 | `driver_override` | human input (brake lever) is overriding the autonomous path | no human override | `g_safety.brake_lever_pressed()` |
| 5–7 | *(reserved)* | — | — | must transmit 0 |

`command_received == 1` while `command_executing == 0` and `command_rejected == 1`
is the "loop closed but blocked" signature — the reason lives in bytes 2–3.

## Byte 1 — readiness & developer overrides

| Bit | Signal | 1 means | 0 means | Driven by |
|---|---|---|---|---|
| 0 | `system_ready` | readiness level == **Full** (after `kSystemReadyHoldMs` debounce) → green READY lamp | readiness degraded | `g_sys_ready_level` (observational READY feature) |
| 1 | `bypass_active` | a developer bypass is armed (same source as bit 2 today) | no bypass | `g_bench_solo_mode` |
| 2 | `bench_solo_mode` | SYS running solo on the bench (no RT/MTR/SEB companions) | normal vehicle mode | `g_bench_solo_mode` |
| 3 | `bypass_mtr_absent` | MTR unplugged **and** its absence bypass is accepted (deliberate, not a fault) | MTR expected present | `g_bypass_mtr_absent` |
| 4 | `bypass_seb_sync` | SEB sync traffic absent **and** bypass accepted | SEB expected present | `g_bypass_seb_sync` |
| 5 | `degraded` | running with reduced supervision: readiness level is `MtrAbsent` or `HostAbsent`, **or** latched brake fault active, **or** any traction inhibit set | fully supervised | readiness level + `g_brake_fault_active` + `any_inhibit()` |
| 6–7 | *(reserved)* | — | — | must transmit 0 |

## Byte 2 — `block_mask_low` (transient blockers)

Self-clearing conditions. A bit is set while its cause is present and clears by
itself when the cause disappears. Constants in `sys-esp32/src/node_status.h`.

| Bit | Value | Constant | Condition (set while) | Clears when |
|---|---|---|---|---|
| 0 | `0x01` | `kBlkBrakeLever` | rider holds the handlebar brake lever (GPIO2) | lever released |
| 1 | `0x02` | `kBlkSebSyncing` | no valid `0x721 SEB_STATUS` seen since boot/reset (`g_seb_seen == false`) | first valid 0x721 acquired |
| 2 | `0x04` | `kBlkMtrFbkUnacked` | MTR ESTOP-ack watchdog pending: an ESTOP was issued and MTR has not yet echoed `ESTOP_ACTIVE` in `0x206.fault_flags` | MTR acknowledges, or watchdog resolves |
| 3 | `0x08` | `kBlkRtSetpointStale` | no `0x204` for > 200 ms (`kBlkSetpointStaleMs`, matches task_safety enforcement) | next fresh 0x204 |
| 4 | `0x10` | `kBlkStartUnlatched` | cockpit run latch not enabled (operator has not latched START) | `g_mode_mgr.run_enabled()` true |
| 5 | `0x20` | `kBlkStartupAcquire` | boot/startup acquisition window (first 1000 ms, `kSebStartupAcquireMs`) reserved for SEB detection | window elapsed |
| 6–7 | — | — | unused (0) | — |

## Byte 3 — `block_mask_high` (latched faults)

Sticky faults. Bits stay set until the operator reset path clears them
(`MODE` button held 5 s with the underlying fault gone). Same constants file.

| Bit | Value | Constant | Condition | Clears when |
|---|---|---|---|---|
| 0 | `0x01` | `kBlkSebL3` | SEB Level-3 critical brake fault latched (`kLatchedSebL3`) — cut traction power, 0x110 clamped MANUAL | operator reset path, only after SEB healthy |
| 1 | `0x02` | `kBlkEgasMismatch` | **RESERVED — always 0.** The throttle/EGAS correlation detector is compiled out on the current encoder-less vehicle (`config.h`) | n/a |
| 2 | `0x04` | `kBlkMtrFbkTimeout` | no `0x206 MTR_MOTOR_FBK` for > 200 ms (`kMtrFbkStaleMs`) **and** MTR not bypass-absent | 0x206 stream returns (fault clears itself; not operator-only) |
| 3 | `0x08` | `kBlkTaskDeadline` | any safety-critical FreeRTOS task missed its 1.5 s alive deadline (see `0x600` byte 3) | all critical tasks loop again |
| 4–7 | — | — | unused (0) | — |

Note: `kBlkMtrFbkTimeout` self-clears on recovery (it is a *stream* watchdog);
`kBlkSebL3` and `kBlkTaskDeadline` follow the latched/recovered flags they mirror.

## Byte 4 — raw cockpit hardware inputs

Ground truth straight from the GPIO pins — **raw, unqualified** (no debounce,
no safety gating). Bit 7 is the one *derived* signal in this byte.

| Bit | Signal | GPIO | Contact type | 1 means |
|---|---|---|---|---|
| 0 | `hw_estop_btn_pressed` | 1 | NC mushroom, pull-up, **active-high-on-open** | mushroom pressed *or wire broken* (fail-safe HIGH) |
| 1 | `hw_start_btn_latched` | 41 | NC latching, pull-up, pressed = HIGH | START button latched (raw state — not the qualified run latch) |
| 2 | `hw_brake_lever_pulled` | 2 | active-low, pull-up | lever pulled |
| 3 | `hw_mode_btn_pressed` | 11 | momentary | mode button held |
| 4 | `hw_sw_left_turn` | 9 | handlebar switch | left-turn switch on |
| 5 | `hw_sw_right_turn` | 6 | handlebar switch | right-turn switch on |
| 6 | `hw_sw_headlight` | 7 | handlebar switch | headlight switch on |
| 7 | `run_latch_enabled` | — | derived | qualified run enable (`g_mode_mgr.run_enabled()`) — START survived the safety gauntlet |

## Byte 5 — relay & lamp outputs (executed state)

The **FINAL executed output state** (what the pins actually drive, whether
commanded manually or by AUTO), not the request. Relay module is active-LOW at
the pin; positive logic is inverted in `set_relay()` (`kRelayOutputActiveLow`).

| Bit | Signal | GPIO / source | 1 means |
|---|---|---|---|
| 0 | `power_12v_relay_on` | GPIO40 | main 12 V power relay energized |
| 1 | `ready_bulb_on` | GPIO17 (green READY) | readiness indicator lit |
| 2 | `bypass_bulb_on` | GPIO14 (amber) | bypass indicator lit |
| 3 | `estop_bulb_on` | GPIO18 (red) | ESTOP indicator lit |
| 4 | `light_left_on` | `g_light_state` bit0 (`out.left_lamp`) | left-turn output active (arbitrated) |
| 5 | `light_right_on` | `g_light_state` bit1 (`out.right_lamp`) | right-turn output active (arbitrated) |
| 6 | `light_brake_on` | `g_light_state` bit2 + brake relay GPIO21 | brake lamp energized |
| 7 | `light_head_on` | `g_light_state` bit3 (`out.head_lamp`) | headlight output active (arbitrated) |

Turn/brake/head outputs are the result of the light arbitration in
`task_lights` (`LightControl::tick`, 20 Hz): manual switches, HOST `0x302`
requests, AUTO hazards and brake precedence (including SEB-is-braking stroke
detection, raw stroke > 610 ≈ 0.5 mm). Only the brake lamp has a dedicated
GPIO relay on current hardware (`kLightBrake` = 21); left/right/head are
arbitration outputs packed into `g_light_state`. The AUTO/MANUAL panel bulbs
(GPIO10/39) and onboard WS2812 (GPIO48) are not reported on this frame.

---

# 0x600 SYS_DIAG_RPT (DLC 8)

Pure ECU/bus health. No mode/brake/estop content — those live on `0x011` and
`0x500`. Published by `task_diag` at 1 Hz.

## Byte 0 — overflow + controller state

| Bits | Signal | Encoding |
|---|---|---|
| 0–5 | `rx_overflow` | TWAI RX queue overflow count, **saturating at 63**. Monotonic growth = queue too small or bus storm. |
| 6–7 | `can_state` | 2-bit enum of the TWAI controller state: |

| Value | Name | Meaning |
|---|---|---|
| 0 | `ACTIVE` | error-active, normal operation |
| 1 | `WARNING` | TEC or REC ≥ 96, still error-active |
| 2 | `PASSIVE` | error-passive (TEC or REC ≥ 128), transmitting restricted |
| 3 | `RECOVERING` | bus-off entered; recovery sequence driving the controller back |

Mapped straight from the driver's `HealthState { Active, Warning, Passive, BusOff }`
(`sys-esp32/src/can_driver.h:24`) — a bus-off controller reports 3 while
`service_recovery` drives it back; five consecutive 1 Hz samples in bus-off
force ESTOP (`kEstopReasonCanBusoff`).

## Byte 1 — `tec` (transmit error counter)

TWAI TEC, 0–255. Telemetry only (bus-off reaction is state-driven, see above).
Guidance: < 96 nominal, 96–127 warning zone, ≥ 128 error-passive.

## Byte 2 — `rec` (receive error counter)

TWAI REC, 0–255. Same thresholds; a lone REC ≥ 128 with TEC < 128 usually
indicates reception-side electrical problems (termination, wiring).

## Byte 3 — `task_health_mask` (FreeRTOS task liveness)

Each bit = 1 when that task's loop ran within the last **1500 ms** deadline;
0 when the task is presumed stalled. Evaluated in `task_diag` every 1 s.

| Bit | Value | Task | Role |
|---|---|---|---|
| 0 | `0x01` | `task_safety` | polls ESTOP pin + RT heartbeat; safety gating |
| 1 | `0x02` | `task_brake` | **sole normal producer of `0x7B9`** brake command |
| 2 | `0x04` | `task_dispatch` | feeds every RX frame decoder |
| 3 | `0x08` | `task_can_tx` | publishes `0x011` authority + `0x500` node status |
| 4 | `0x10` | `task_can_control` | TWAI recovery supervisor |
| 5 | `0x20` | `task_hb` | publishes `0x7FE` heartbeat |
| 6 | `0x40` | `task_mode` | publishes `0x110`/`0x113` authority |
| 7 | `0x80` | `task_gear` | gear adjudication (50 Hz) |

**Critical subset = `0x4F`** (`safety | brake | dispatch | can_tx | mode`).
If any critical bit is missing for ≥ 2 consecutive 1 Hz diag cycles, SYS can
not guarantee a safe stop and latches ESTOP (`kEstopReasonTaskDeadline`) and
broadcasts `0x001`. The same condition also sets `0x500.block_mask_high` bit 3.
Loss of `can_control`, `hb`, or `gear` is handled downstream, not by ESTOP here.

## Byte 4 — `free_heap_kb`

Free heap in whole kilobytes, u8, **saturating at 255** (any value ≥ 256 KB
reports 255). Memory-leak tripwire: a downward trend across boots indicates a leak.

## Byte 5 — `mcu_reset_reason`

Last MCU reset cause, mapped from `esp_reset_reason()`.

| Value | Name | `esp_reset_reason_t` mapping |
|---|---|---|
| 0 | `POWER_ON` | `ESP_RST_POWERON` |
| 1 | `SW_RESET` | `ESP_RST_SW` |
| 2 | `TASK_WDT` | `ESP_RST_TASK_WDT` **and** `ESP_RST_WDT` (RTC WDT family) |
| 3 | `BROWNOUT` | `ESP_RST_BROWNOUT` |
| 4 | `PANIC` | `ESP_RST_PANIC` (abort/assert/exception) |
| 5 | `UNKNOWN` | anything else (incl. `ESP_RST_USB`, `ESP_RST_JTAG`, …) |

`1` or `4` after a field incident = software decided to restart; `2` = a task
starved the watchdog; `3` = supply problem.

## Bytes 6–7 — `uptime_seconds`

u16, big-endian (`data[6]` = MSB). Seconds since boot; wraps at 65536 s
(≈ 18.2 h). Pair with `mcu_reset_reason` for boot forensics.

---

# Consumers & forwarding

- RT gateway transparently forwards both frames low → high (same-frame), so the
  Jetson sees them on the high bus.
- Jetson `vehicle_bridge_node.cpp` decodes `0x600` into ROS diagnostics
  (`can_state`/`tec`/`rec`/`task_health_mask`/`free_heap_kb`/`mcu_reset_reason`/
  `uptime_seconds`) and consumes `0x500` for lamp state (turn/hazard).
- control-toolkit derives SYS ESTOP latch from `0x011.estop_source` (not from
  these frames) and reads lights/lever/relays from `0x500`.
- The old fields removed from `0x600` (`mode`, `brake_engaged`, `brake_fault`,
  `heartbeat_ok`, `estop_active`, lights) have new homes: authority on `0x011`,
  execution/blockers/lamps on `0x500`.

---

# Worked examples

## Decode `0x500` payload `07 21 23 01 04 42`

| Byte | Bits | Interpretation |
|---|---|---|
| `0x07` | B0 | `command_received`=1, `command_nonzero`=1, `command_executing`=1 (AUTO run with a live nonzero setpoint), `command_rejected`=0, `driver_override`=0 |
| `0x21` | B1 | `system_ready`=1 (Full), `bypass_active`=0, `bench_solo`=0, bypass bits 0, `degraded`=1 |
| `0x23` | B2 | transient blockers: `0x01` brake lever **+** `0x02` SEB not yet seen **+** `0x20` startup acquire window |
| `0x01` | B3 | latched: SEB L3 fault (needs operator reset) |
| `0x04` | B4 | raw inputs: only `hw_brake_lever_pulled` (GPIO2); run latch enabled bit would be `0x80` |
| `0x42` | B5 | outputs: `ready_bulb_on` (0x02) + `light_brake_on` (0x40) |

Plausible story: startup window still open, lever pulled (brake lamp lit), a
previous SEB L3 is still latched, but 12 V/READY are up and executing is on.
In reality `command_executing` with `block_mask != 0` would be transient — the
mask here is the "why it would stop" view.

## Decode `0x600` payload `00 05 00 FF 78 03 04 D2`

| Byte | Bits | Interpretation |
|---|---|---|
| `0x00` | B0 | `rx_overflow`=0, `can_state`=0 (ACTIVE) |
| `0x05` | B1 | TEC=5 (nominal) |
| `0x00` | B2 | REC=0 |
| `0xFF` | B3 | all 8 tasks alive within 1.5 s |
| `0x78` | B4 | free heap = 120 KB |
| `0x03` | B5 | reset reason = BROWNOUT (last boot was a supply dip) |
| `0x04D2` | B6–7 | uptime = 0x04D2 = 1234 s (~20 min) |

## Encode formulas

- `0x600` byte 0: `data[0] = (rx_overflow & 0x3F) | (can_state << 6)`
- `0x500` byte 2/3: `low |= 0x01` per bit; `high` same — masks are plain bit-OR of the vocabulary values.
- `0x600` uptime: `data[6] = (s >> 8) & 0xFF; data[7] = s & 0xFF`
