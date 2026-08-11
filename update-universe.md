# E-Trike → Autoware Universe Migration — Complete Plan

Status: verified against the actual Universe workspace at `E:\work\av_project\autoware`
(ROS 2 Humble, `ghcr.io/autowarefoundation/autoware:universe-cuda-humble`, overlay build).
Supersedes the Autoware.Auto assumptions in `docs/communications/io-autoware.md` and
refines `jetson/docs/bridge-update.md`.

Contents: verified Universe contract (§1), corrections to earlier plans (§2), direct
compatibility answers (§3), per-component change lists with file-level detail (§4),
Autoware-side config (§5), state machine (§6), size estimate and rollout (§7),
validation plan (§8).

---

## 1. Verified Universe vehicle-interface contract

Evidence paths are relative to `E:\work\av_project\autoware\src`.

### 1.1 Message packages (no `autoware_auto_*` exists in the workspace)

| Need | Universe package | Verified in |
|---|---|---|
| Control command | `autoware_control_msgs/msg/Control` | `core/autoware_msgs/autoware_control_msgs/msg/Control.msg` |
| Vehicle status/commands | `autoware_vehicle_msgs` | `core/autoware_msgs/autoware_vehicle_msgs/msg/*.msg` |
| Mode service | `autoware_vehicle_msgs/srv/ControlModeCommand` | `core/autoware_msgs/autoware_vehicle_msgs/srv/ControlModeCommand.srv` |
| Emergency | `tier4_vehicle_msgs/msg/VehicleEmergencyStamped` (`stamp`, `bool emergency`) | `universe/external/tier4_autoware_msgs/tier4_vehicle_msgs/msg/VehicleEmergencyStamped.msg` |
| Actuation (optional path) | `tier4_vehicle_msgs/msg/ActuationCommandStamped` | used by `universe/autoware_universe/vehicle/autoware_raw_vehicle_cmd_converter` |

`VehicleKinematicState` does not exist anywhere (only CHANGELOG mentions). `our_packages/` is empty.

### 1.2 `Control` message (replaces `AckermannControlCommand`)

```
Control:        stamp, control_time, Lateral lateral, Longitudinal longitudinal
Lateral:        stamp, control_time, float32 steering_tire_angle (ALWAYS valid),
                float32 steering_tire_rotation_rate, bool is_defined_steering_tire_rotation_rate
Longitudinal:   stamp, control_time, float32 velocity (ALWAYS valid),
                float32 acceleration, float32 jerk,
                bool is_defined_acceleration, bool is_defined_jerk
```

- No `is_defined_speed`, no `is_defined_steering_tire_angle` — both values are always present.
- Sign convention: positive steering = **left** (`Lateral.msg`), positive velocity = forward.

### 1.3 Command topics (vehicle_cmd_gate outputs, `tier4_control_launch/launch/control.launch.xml:60-63,116-119`)

| Topic | Type | QoS (publisher) |
|---|---|---|
| `/control/command/control_cmd` | `Control` | transient_local depth 1 (vehicle_cmd_gate) / reliable depth 5 (control_command_gate) |
| `/control/command/gear_cmd` | `GearCommand` | transient_local depth 1 |
| `/control/command/turn_indicators_cmd` | `TurnIndicatorsCommand` | transient_local depth 1 |
| `/control/command/hazard_lights_cmd` | `HazardLightsCommand` | transient_local depth 1 |
| `/control/command/emergency_cmd` | `tier4_vehicle_msgs/VehicleEmergencyStamped` | transient_local depth 1 |
| `/control/command/actuation_cmd` | `tier4 ActuationCommandStamped` | only if `raw_vehicle_cmd_converter` is launched |

Bridge subscribers must use **volatile** reliability (compatible with transient_local publishers,
and avoids acting on latched stale commands after restart).

### 1.4 Feedback topics (canonical set from `simple_planning_simulator.launch.py:46-64`)

| Topic | Type | Notes |
|---|---|---|
| `/vehicle/status/velocity_status` | `VelocityReport` | has `std_msgs/Header` — **frame_id must be `base_link`, stamp must be set**; feeds `autoware_vehicle_velocity_converter` → gyro_odometer localization (`core/autoware_core/sensing/autoware_vehicle_velocity_converter/src/vehicle_velocity_converter.cpp`, header passed through verbatim) |
| `/vehicle/status/steering_status` | `SteeringReport` | `stamp` + `steering_tire_angle` [rad], **positive = left** |
| `/vehicle/status/gear_status` | `GearReport` | |
| `/vehicle/status/control_mode` | `ControlModeReport` | consumed by operation_mode_transition_manager, command_mode_switcher/decider, mrm_handler |
| `/vehicle/status/turn_indicators_status` | `TurnIndicatorsReport` | constants start at DISABLE=1 (0 is invalid) |
| `/vehicle/status/hazard_lights_status` | `HazardLightsReport` | constants start at DISABLE=1 |
| `/vehicle/status/actuation_status` | `tier4 ActuationStatusStamped` | only needed if raw_vehicle_cmd_converter is used — **skip for E-Trike** |

### 1.5 Services and engage

- **`/control/control_mode_request`** — `autoware_vehicle_msgs/srv/ControlModeCommand`.
  Client: `autoware_operation_mode_transition_manager` (`node.cpp:27`, remapped in
  `operation_mode_transition_manager.launch.xml:22`); also `autoware_command_mode_switcher`
  (`selector_interface.cpp:45`). Server in sim: `simple_planning_simulator_core.cpp:138`.
  Request: `mode ∈ {NO_COMMAND=0, AUTONOMOUS=1, AUTONOMOUS_STEER_ONLY=2, AUTONOMOUS_VELOCITY_ONLY=3, MANUAL=4}`.
  Response: `bool success`.
- **Engage topic is `/vehicle/engage`** (not `/control/command/engage`):
  `simple_planning_simulator.launch.py:54`, `joy_controller.launch.xml:13`,
  `vehicle.launch.xml` arg `initial_engage_state` describes "`/vehicle/engage` state".
- `ControlModeReport` constants: `NO_COMMAND=0, AUTONOMOUS=1, AUTONOMOUS_STEER_ONLY=2,
  AUTONOMOUS_VELOCITY_ONLY=3, MANUAL=4, DISENGAGED=5, NOT_READY=6`.
- `GearCommand`/`GearReport` constants: `NONE=0, NEUTRAL=1, DRIVE=2, REVERSE=20, PARK=22, LOW=23`.

### 1.6 Mode-transition behavior the bridge must satisfy

From `autoware_operation_mode_transition_manager` (`node.cpp`, `state.cpp`,
`config/operation_mode_transition_manager.param.yaml`):

1. It calls the service; `success=false` ⇒ transition cancelled (`is_engage_failed`).
2. After success it waits until `/vehicle/status/control_mode` reports AUTONOMOUS
   (`getCurrentControlMode()`, node.cpp:353-358), with `transition_timeout: 10.0 s`.
3. Completion additionally requires stable speed/yaw/distance vs trajectory
   (`stable_check.duration: 0.1`, speed thresholds ±2.0 m/s).
4. `enable_engage_on_driving: false` by default ⇒ engage only from standstill.
5. Disengage sends `MANUAL` over the same service.

⇒ The bridge must answer the service **immediately** (accept/reject), and only publish
`AUTONOMOUS` in the report once RT/SYS feedback confirms it.

### 1.7 Vehicle launch integration point

`tier4_vehicle_launch/launch/vehicle.launch.xml` includes
`$(var vehicle_model)_launch/launch/vehicle_interface.launch.xml` with args
`vehicle_id`, `raw_vehicle_cmd_converter_param_path`, `initial_engage_state`.
`sample_vehicle_launch` ships an empty stub — E-Trike must provide a real one.

`vehicle_info.param.yaml` fields required (sample_vehicle_description): `wheel_radius,
wheel_width, wheel_base, wheel_tread, front_overhang, rear_overhang, left_overhang,
right_overhang, vehicle_height, max_steer_angle`.

---

## 2. Corrections to the earlier plan (`jetson/docs/bridge-update.md`)

| bridge-update.md claim | Verified reality |
|---|---|
| Engage via `/control/command/engage` | **`/vehicle/engage`** |
| `tier4_vehicle_msgs` only for emergency | also owns `ActuationCommandStamped`/`ActuationStatusStamped` (core `autoware_vehicle_msgs` has its own Actuation types, but raw_vehicle_cmd_converter uses the tier4 ones) |
| Emergency topic "verify against deployed stack" | confirmed: `/control/command/emergency_cmd`, published by vehicle_cmd_gate (`use_control_command_gate` defaults to **false**) |
| Actuation command "not implemented" | correct to skip: subscribe to `Control` directly like simple_planning_simulator; do not launch raw_vehicle_cmd_converter |
| QoS "review per interface" | concrete answer: subscribe volatile depth 1; commands arrive transient_local — compatible |
| Missing: steering sign | Universe is **positive-left**; E-Trike internals are **+right** (`rt-esp32/src/physics_model.h:19`, `steering_control.h:162`) — bridge must negate on both TX and RX paths |
| Missing: PARK is actually commanded | `autoware_shift_decider.cpp:78` sends `GearCommand::PARK` — cannot be ignored |

---

## 3. Direct compatibility answers

### 3.1 Is RT compatible with Universe?

**It is drivable, but contract-incompatible.** The existing Autoware.Auto bridge already
drives RT from an Autoware stack by converting steering angle → yaw rate. So RT *moves*
under Universe control with zero firmware changes. But it violates the Universe vehicle
interface contract in 6 concrete ways (§3.3), so a bridge-only port ships with known
deficiencies (no steering at standstill, PARK silently ignored, optimistic mode service,
assert-only emergency, 10 Hz steering feedback, no heading_rate).

### 3.2 If RT is changed, is SYS automatically compatible?

**No.** SYS's gaps are independent of RT's:

| SYS gap | Why RT changes don't fix it |
|---|---|
| Mode authority: Universe mode service requests must be arbitrated against the physical MODE button | SYS owns the mode state machine (`mode_manager.h`) and broadcasts `0x110 SYS_MODE_CMD`. RT only *receives* mode. |
| ESTOP latch/clear semantics | SYS owns the latch (`safety_monitor`); `0x001` is DLC=0 assert-only on the wire. |
| PARK policy (brake hold) | SYS owns the brake in MANUAL/ESTOP. |

Additionally RT and SYS are **coupled through shared enums** that must change in lockstep
(same protocol revision, flashed together):

- `0x110 SYS_MODE_CMD` mode range `{0..2}` is consumed by RT *and* MTR — widening it touches all three.
- SYS decodes `0x210 RT_STATE_RPT` (`safety_state`) to gate its own `0x7B9` brake sends — the widened mode enum must stay consistent on both sides.

So: **RT and SYS each need their own work, plus one synchronized protocol revision.**

### 3.3 RT gaps vs the Universe contract

| # | Gap | Universe expectation | Current RT behavior |
|---|---|---|---|
| R1 | Steering input | `Control.lateral.steering_tire_angle` always defined, must act at standstill | `HOST_DRIVE_CMD` carries yaw rate only; bridge zeroes yaw below 0.05 m/s (`vehicle_bridge_node.cpp:172`); RT decays steering to 0 at low speed (`physics_model.cpp:66-70`) |
| R2 | Mode report | `ControlModeReport` needs AUTONOMOUS / MANUAL / DISENGAGED / NOT_READY semantics | `rt_state_rpt.mode` is `{0:MANUAL, 1:AUTO, 2:ESTOP}` — no NOT_READY/DISENGAGED distinction, no request/ack |
| R3 | Mode service | `/control/control_mode_request` must return success/failure based on vehicle acceptance | No host→vehicle mode path exists (`sys_mode_cmd` 0x110 is SYS-originated) |
| R4 | PARK | `shift_decider` sends `GearCommand::PARK` | Gear enum `{N,D,S,R}` — PARK would be silently mapped to N |
| R5 | heading_rate | `VelocityReport.heading_rate` feeds localization (`vehicle_velocity_converter` → gyro_odometer) | No yaw-rate signal vehicle→host; bridge hard-codes 0 |
| R6 | Steering feedback rate | MPC consumes `steering_status` at control rate | `STEER_DIAG` 0x310 at 100 ms (10 Hz) |

### 3.4 SYS gaps (independent of RT)

| # | Gap | Detail |
|---|---|---|
| S1 | Host mode request arbitration | `ModeManager` accepts physical button + HMI 0x111 (`parse_hmi_mode`). Universe host request needs the same arbitration path **plus** reject reasons and a "requested vs confirmed" distinction so the bridge can answer the service and delay the AUTONOMOUS report. |
| S2 | ESTOP latch/clear | **By design, clear is physical-only** (START button or mode long-press 3s — `architecture.md` §3, §14). CAN 0x001 is assert-only; HMI 0x111 is ignored in ESTOP. This is the intended safety interlock — software must NOT be able to clear a hardware ESTOP. Bridge asserts on `emergency=true`; on `false` it stops asserting and gates on SYS `estop_active` (`0x011` byte0), reporting DISENGAGED until physical reset. No firmware change required. |
| S3 | PARK brake hold | **Bridge-handled**: on `GearCommand::PARK`, bridge sends `0x301` brake-hold + reports `GearReport::PARK`; SYS/MTR execute the brake. No firmware change needed for basic PARK. A firmware `PARK_REJECTED` (brake not achieved) is deferred. |
| S4 | Mode enum lockstep | `0x110` mode range widening must ship with RT/MTR in the same revision. |

### 3.5 What already fits (no change)

Frame format, XOR8 profile, heartbeats 0x7FC/0x7FD/0x7FE, EGAS layering, bus split,
light command/feedback round trip (0x302/0x011), brake kPa path, velocity feedback
(0x120 @ 10 ms), gear feedback (0x206), diagnostics (0x600).

---

## 4. Per-component change lists (file-level)

### 4.1 `protocol/` — contracts + regeneration

Protocol deltas P1–P6 (**all OPTIONAL — deferred to a later phase; not required to make the stack drivable**):

| # | Change | Universe driver | Blocks "make it work"? |
| --- | --- | --- | --- |
| P1 | NEW `host_steer_cmd` (0x303) with `steer_angle_mdeg`; keep 0x300 as-is | steering angle always defined in `Control`; angle→yaw→angle round trip loses steering below `kLowSpeedThreshMmps=50` and blocks pull-out from standstill | No — degraded (no standstill steering) but drivable |
| P2 | New `host_mode_req` (0x304) + `rt_mode_ack` (0x211) | `/control/control_mode_request` is a **service**; bridge can answer it from existing `0x210` feedback | No — bridge answers service locally |
| P3 | ESTOP handling is **bridge-only** (no new frame) | `0x001` asserts on `emergency=true`; hardware clear is physical-only by design (`architecture.md` §3/§14) — must NOT be CAN-clearable. Bridge gates on `estop_active` (`0x011`) + reports DISENGAGED. Optional only: add `estop_source` to `0x011` for diagnostics | No protocol change required |
| P4 | New `rt_motion_rpt` (0x121) with yaw rate | `VelocityReport.heading_rate` feeds localization | No — localization degrades, control still works |
| P5 | `steer_diag` 100 ms → 20 ms | MPC needs `steering_status` at control rate | No — 10 Hz is marginal, not fatal |
| P6 | Gear enum: `PARK_REJECTED` + reject reason (optional) | Universe `DRIVE=2` is a **bridge fix**; PARK is **bridge-handled** via `0x301` brake-hold + report `GearReport::PARK`; only a *reject reason* needs firmware later | DRIVE + PARK are **bridge must-fix**; `PARK_REJECTED` is deferred |

File-level edits:

> **Strategy: bridge-first, no protocol changes required to make it work.** The minimal
> viable path changes **only the Jetson bridge** against the existing 17 high-bus frames and
> makes the stack drivable end-to-end. A later additive phase (new frames 0x303/0x304/0x305/
> 0x211/0x121) can close the remaining *contract-fidelity* gaps, but it is **optional** — the
> stack works without it. Note: `decode_*` enforces strict DLC equality, so if we ever do add
> frames, they must be **new IDs**, never in-place widens (an unflashed receiver would reject
> a widened in-service frame outright).

| File | Change |
| --- | --- |
| `contracts/host.yaml` | **P1** NEW `host_steer_cmd` (0x303, DLC 4) — `steer_angle_mdeg` i16 (±45000 mdeg = ±45°, +right; E-Trike `kSteerLimitDeg=40` fits), `valid` u8. Do NOT repack 0x300 (it is fully packed: speed i32 b0-3, yaw i24 b4-6, gear b7). **P2** NEW `host_mode_req` (0x304) — `req_mode` u8, `req_ctr` u8. |
| `contracts/rt.yaml` | **P2** NEW `rt_mode_ack` (0x211) — `mode_confirmed` u8 (7-state), `mode_requested` u8, `reject_reason` u8. **P4** NEW `rt_motion_rpt` (0x121, 10 ms) — `yaw_rate_mrad_s` i16, `gear_actual` u8 (incl. PARK/PARK_REJECTED). **P5** `steer_diag` `cycle_ms: 100 → 20` (timing only, no layout change). **P6** gear enum: add `4: PARK_REJECTED`. |
| `contracts/sys.yaml` | **P3** `sys_safety_sts`: add `estop_source` u8, `clear_permitted` u8 (keep 0x011 layout, append fields — no in-place widen). |
| `contracts/network.yaml` | **P3** NEW `host_estop_cmd` (0x305) — `assert` u8, `source` u8 (do NOT widen 0x001; keep DLC=0 assert as legacy). Add routes for 0x303/0x304 high→low. |
| `tools/protocol.py` | No generator changes expected (signal layouts only). |
| Regenerate | `generated/cpp|python|typescript`, `generated/dbc/*`, `generated/docs/*`, `capabilities.json`. |
| `vectors/*.json`, `tests/python/*` | New golden vectors for 0x303/0x304/0x305/0x211/0x121; update `test_contracts.py`, `test_vectors.py`, `test_golden_vectors.py`. Existing 0x300/0x301/0x302/0x120/0x210/0x001 vectors stay valid (layouts unchanged). |

### 4.2 `rt-esp32/` (~4.6 kloc; ~300–450 lines touched)

| File | Change |
|---|---|
| `src/direct_resolver.cpp/.h` (62 lines) | Add angle-passthrough mode: when `0x303 steer_angle_mdeg` present, skip yaw→angle conversion; forward angle to SES `VCU_SES_Tgt_StrAngle`; clamp to `kSteerLimitDeg` (40°). Keep yaw path for legacy/HMI sources. Remove low-speed steering dead zone in angle mode (steer at v≈0, speed still gated to 0). |
| `src/physics_model.cpp` | Unchanged as fallback resolver; bypass low-speed decay when angle mode active. |
| `src/main.cpp` (798 lines) | Resolver selection from decoded 0x300; PARK reject (report `PARK_REJECTED`, zero speed); publish yaw rate (compute `v·tan(δ)/L` — RT has both authoritatively); widened mode + `mode_requested`/`reject_reason` in `RT_STATE_RPT` TX. |
| `src/can_dispatch.h` (319 lines) | New/updated TX builders for widened `rt_state_rpt`, motion report (P4), 50 Hz `STEER_DIAG` (P5). |
| `src/config.h`, `src/resolver_config.h` | Angle-mode enable flag, PARK policy constants. |
| `src/can_rx_router.h` | Decode `0x303 host_steer_cmd` + `0x304 host_mode_req` (route mode req to SYS via low bus per new network route; ack flows back through `0x211 rt_mode_ack`). |
| Tests | `rt-test/`, SIL: angle-mode vectors, PARK reject, mode ack timing, 50 Hz steer diag bus load. |

### 4.3 `sys-esp32/` (~2.3 kloc; ~200–350 lines touched)

| File | Change |
|---|---|
| `src/mode_manager.h/.cpp` (125 lines) | Add host-request input mirroring the existing `parse_hmi_mode` pattern: `parse_host_mode(req)` with arbitration vs physical button, reject reasons (ESTOP active, heartbeat lost, button policy), `requested_mode()` accessor so 0x110/0x600 can carry requested-vs-confirmed. |
| `src/safety_monitor.h/.cpp` (75 lines) | ESTOP latch + source enum (button / CAN host / heartbeat / fault) + `clear_permitted` state. |
| `src/main.cpp` (996 lines) | RX `host_mode_req` (forwarded high→low by RT); TX widened `sys_safety_sts`; PARK → brake-hold in brake task; keep 0x110 enum in lockstep with RT/MTR. |
| `src/brake_control.h` | PARK hold entry (hold pressure until mode leaves PARK-rejected state). |
| Tests | native tests: arbitration matrix (button × host × estop), latch/clear sequences. |

### 4.4 `jetson/` — full bridge rewrite (~1.1 kloc → ~1.4 kloc; largest single item)

| File | Change |
|---|---|
| New package `etrike_autoware_vehicle_bridge` | rename dir, CMake project, C++ namespace, include guards (`ETRIKE_AUTOWARE_VEHICLE_BRIDGE_*`) |
| `CMakeLists.txt` | `autoware_control_msgs`, `autoware_vehicle_msgs`, `tier4_vehicle_msgs`, `etrike_protocol` (real ament dep, drop the `../../..` include hack); library split (driver/encoder/decoder/state) + tests; install launch dir. |
| `package.xml` | Same dep swap + Universe description. |
| `include/.../vehicle_bridge_node.hpp` | `Control` replaces `AckermannControlCommand`; `rclcpp::Service<ControlModeCommand>` member; drop `VehicleKinematicState`; add state-machine struct (requested mode, engage, confirmed mode, emergency latch, freshness). |
| `src/vehicle_bridge_node.cpp` | Field mapping (`longitudinal.velocity`, drop `is_defined_speed`/`is_defined_steering_tire_angle`, keep `is_defined_acceleration`); NaN/Inf validation → safe-stop; **steering sign flip** (Universe +left vs E-Trike +right) on TX and RX; gear constants from messages (`GearCommand::DRIVE` = 2, never raw numbers); PARK → forward + report feedback + diagnostic until P6 reject path lands; emergency honors `msg->emergency` bool (assert on true, defined latch/clear policy on false), rate-limit without delaying first assertion; mode service answers immediately (`success=false` for STEER_ONLY/VELOCITY_ONLY initially), report AUTONOMOUS only after CAN confirm; engage via `/vehicle/engage`; `VelocityReport` header (stamp + `frame_id="base_link"`) + real `heading_rate` (P4); `SteeringReport.stamp` + raised rate (P5); turn/hazard reports never publish 0 (constants start at DISABLE=1); volatile QoS depth 1; private topic names + launch remaps. |
| `config/etrike.param.yaml` | Keep existing 12 params; add steering/yaw/velocity sign multipliers, PARK policy, startup mode policy, estop latch/reset policy, odometry enable flag + frame names. |
| `launch/vehicle_bridge.launch.xml` | Load params (currently not loaded), lifecycle configure/activate management (or convert to plain `rclcpp::Node`), remaps to `/control/*` and `/vehicle/status/*`, safe shutdown on configure failure. |
| Odometry | Delete `VehicleKinematicState` publisher; optional `nav_msgs/Odometry` (`odom`→`base_link`) only if another component needs it; do not publish `/localization/kinematic_state`. |
| Tests | Unit: conversions/saturation, gear map incl. PARK, sign flip, emergency true/false, timeout/re-enable, heartbeat boundaries, malformed frames. Integration on `vcan`: lifecycle, frames↔topics, CAN disconnect, heartbeat loss, clean shutdown. |

### 4.5 `control-toolkit/` — small, catalog-driven

The toolkit reads the protocol catalog (`control_toolkit.protocol_bridge.CATALOG`), so
layout changes propagate automatically. Touchpoints:

| Area | Change |
|---|---|
| Codecs | Regenerate (automatic via protocol regen). |
| `services/control_intent.py` | Builds `{speed_mmps, yaw_rate_mrad_s, gear}` by field name — keeps working if field names preserved; add optional `steer_angle_mdeg` passthrough for angle-mode testing. |
| `tests/test_bit_layout.py` | **Breaks**: asserts bytes 0-3 all owned by `speed_mmps` — repack invalidates this; rewrite against new layout. |
| `tests/test_firmware_alignment.py`, `test_encoder.py`, `test_injections.py`, `test_api_surface.py` | Audit for hard-coded 0x300 byte offsets / payload dicts; expect ~6-10 test files touched. |
| `frontend/src/catalog.ts`, UI signal tables | Auto-refresh from catalog; add steer_angle signal display. |
| `scripts/*` (smoke, QA probes) | Payload dicts by field name — mostly fine; grep for hard-coded bytes. |

### 4.6 Others (minor)

- `simulation/`, `debug-tool/`, `vt-console/`: regenerate codecs; add new signals where displayed.
- Docs: `docs/communications/io-autoware.md` (rewrite for Universe),
  `docs/communications/autoware-auto-communication-architecture.md`, `can-dictionary.md`,
  `docs/safety/traceability-matrix.md`, `architecture.md` protocol tables.

### 4.7 New packages in `E:\work\av_project\autoware\src\our_packages`

1. `etrike_autoware_vehicle_bridge` — the ported bridge (§4.4).
2. `etrike_protocol` — ament package exporting `protocol/generated/cpp/etrike_protocol.hpp`
   (+ pinned revision used by RT/SYS/MTR/Jetson).
3. `etrike_vehicle_launch` — provides `launch/vehicle_interface.launch.xml` (the stub in
   sample_vehicle_launch is empty); starts the bridge, loads params, remaps to `/control/*`
   and `/vehicle/status/*`, handles lifecycle transitions, safe shutdown on configure failure.
4. `etrike_vehicle_description` — `config/vehicle_info.param.yaml`:
   `wheel_base: 1.5`, `max_steer_angle: 0.698` (= 40°, `rt-esp32/src/config.h:91`),
   `wheel_radius/width/tread/overhangs/height` from the trike CAD; URDF for
   `robot_state_publisher` (vehicle.launch.xml xacro).

---

## 5. Autoware parameter overrides for the trike

| Param file | Change | Reason |
|---|---|---|
| `vehicle_cmd_gate.param.yaml` | `nominal.vel_lim: 25.0 → 3.0`, reverse limit 0.5, steer/accel limits to trike envelope | gate must not pass commands the trike cannot execute (`shared/shared_config.h:18-20`: 3000 mm/s fwd, 500 mm/s rev) |
| `operation_mode_transition_manager.param.yaml` | keep `transition_timeout ≥ 10`, verify `stable_check` vs trike dynamics | mode handshake timing (§1.6) |
| `control/trajectory_follower` (MPC) | prediction/control horizons vs 50 Hz steering feedback + 10 ms drive cmd | P5 latency budget |
| localization | gyro_odometer twist from `vehicle_velocity_converter` relies on honest `heading_rate` | P4 |

---

## 6. Bridge state machine (single source of truth)

CAN drive output enabled only when ALL hold:

1. mode service accepted **and** RT/SYS feedback confirms AUTO,
2. engaged (`/vehicle/engage`),
3. no emergency latch,
4. control command fresh (< `command_timeout_ms`),
5. RT heartbeat alive.

Re-engagement after any fault requires a fresh post-fault control command.
Report `NOT_READY` while SYS is still transitioning, `DISENGAGED` on ESTOP/heartbeat
loss, never `AUTONOMOUS` before confirmation. Service `success=true` means *accepted*,
not *already autonomous*.

Safety behaviors to preserve/strengthen (from `bridge-update.md`): command timeout safe
stop, heartbeat generation + RT monitoring, saturation on all conversions, immediate
ESTOP assertion, disengaged startup, physical mode gating, safe shutdown; plus: no motion
before valid mode+engage+feedback+fresh command, defined stale gear/light behavior,
separate reverse speed limit, CAN bus-off handling, diagnostics distinguishing
timeout/heartbeat/ESTOP/CAN-IO/invalid-frame.

---

## 7. Size estimate and rollout

### 7.1 Effort table

| Component | Files touched | Lines changed/added | Effort (1 dev) | Risk |
|---|---|---|---|---|
| Protocol contracts + regen + vectors | ~8 + generated | ~150 + regen | 1–2 d | Low (well tested) |
| RT firmware | ~7 | 300–450 | 3–5 d | **Med-high** (motion path, needs SIL) |
| SYS firmware | ~5 | 200–350 | 2–4 d | **High** (safety latch, mode authority) |
| Jetson bridge | ~7 (new pkg) | ~1400 rewrite | 5–8 d | Med (mechanical, well specced) |
| control-toolkit | ~10 | ~150 | 1–2 d | Low |
| av_project packages + params | 4 new pkgs | ~600 | 2–3 d | Low-med |
| Docs | ~5 | — | 1 d | Low |
| Integration + staged validation (vcan → sim → lifted wheels → ground) | — | — | 3–5 d | — |
| **Tier 2 total (full compliance)** | | | **~3–4 weeks** | |
| **Tier 1 (bridge-only, firmware untouched)** | jetson + av_project only | | **~1.5–2 weeks** | |

Tier 1 ships the 6 known deficiencies (§3.3) but proves the whole Universe integration
end-to-end before any firmware moves.

### 7.2 Flash/rollout constraint

RT, SYS, MTR and Jetson must run the **same protocol revision** (new frames 0x303/0x304/
0x305/0x211/0x121 added; existing frames keep their layouts). Old firmware ignores the new
IDs silently, so the bridge can be deployed before firmware if needed — but mode/steering/
emergency gaps stay latent until firmware lands. Roll out as one tagged firmware set; keep
the old Autoware.Auto bridge available for rollback until the Universe port is validated
(already required by `bridge-update.md` definition of done).

### 7.3 Recommended sequence

1. Protocol P1–P6 + vectors (unlocks parallel work).
2. Jetson bridge port against **vcan + synthetic peers** (control-toolkit's synthetic
   peers can emit the new frames) — no firmware needed.
3. RT angle mode + mode ack; SYS arbitration + latch (parallel).
4. control-toolkit test fixes + regen.
5. SIL/HIL regression, then staged vehicle validation (§8).

---

## 8. Verification sequence

1. Protocol: contract edits P1–P6 → regenerate → `python -m pytest protocol/tests` + golden vectors.
2. RT/SYS: resolver mode, mode handshake, ESTOP latch; native/SIL tests (`rt-test`, `native-test`).
3. Bridge: unit tests (conversions, gear map incl. PARK, emergency true/false, sign flip,
   timeout) + `vcan` integration tests (lifecycle, frames↔topics, heartbeat loss).
4. `colcon build --packages-select etrike_protocol etrike_autoware_vehicle_bridge` in the
   Humble docker (`av_project/docker/build.sh`).
5. Launch full stack in sim (simple_planning_simulator replaced by bridge on `vcan`);
   verify with `ros2 topic info -v` / `ros2 service type` that types **and QoS** match.
6. Engage path: operation_mode_transition_manager → service → report AUTONOMOUS within 10 s.
7. Jetson + physical CAN, wheels lifted: ESTOP, timeout, heartbeat loss, manual takeover —
   only then enable propulsion.

## 9. Unchanged

CAN frame format, XOR8 profile, heartbeats (0x7FC/0x7FD/0x7FE), EGAS layering, high/low bus
split, SEB/SES proprietary frames, brake kPa path, `raw_vehicle_cmd_converter` (not used).

---

## 10. What to change in the bridge vs what truly needs a CAN change

The wire format of 17 high-bus frames is largely correct; the mismatches are mostly in the
ROS plumbing. **Fix in the bridge (no firmware/protocol change) whenever possible** — these
ship first and carry zero wire risk.

### 10.0 Gaps that actually STOP it from working (the gate — all bridge-fixable)

If any of these is missing, the stack does not drive correctly. Every one is a **bridge-only**
fix, so the minimal viable path touches only `jetson/`:

1. **Compile** — bridge uses `is_defined_speed` / `is_defined_steering_tire_angle` /
   `VehicleKinematicState`, none of which exist in Universe. Port to `Control` + delete
   `VehicleKinematicState`. (Without this it won't even build.)
2. **Steering sign** — E-Trike is +right, Universe is +left. Unfixed → vehicle steers
   **opposite**. Negate on TX and RX.
3. **Gear enum** — bridge hard-codes `DRIVE=1`; Universe `DRIVE=2` (=NEUTRAL in Universe).
   Unfixed → commanded DRIVE is read as NEUTRAL, **vehicle never moves**. Translate enums
   from the message headers both ways.
4. **Mode service** — `/control/control_mode_request` is a service; bridge must answer
   `success` and report `AUTONOMOUS` (map existing `0x210` AUTO→AUTONOMOUS). Without it,
   `operation_mode_transition_manager` never completes engage.
5. **Engage topic + QoS** — subscribe `/vehicle/engage`; subscribe commands volatile depth 1
   against transient_local publishers. Otherwise engage never arrives / stale commands act.
6. **Emergency bool** — act on `VehicleEmergencyStamped.emergency` (assert on true). Without
   it, the bridge ignores the emergency path entirely.

Close these six and the trike is **drivable under Universe end-to-end** on the existing
frames. Everything in §10.2 is a deferred capability/compliance gap, not a gate.

### 10.1 Bridge-only (do NOT touch the protocol)

| Item | Fix |
| --- | --- |
| `0x300` / `0x206` DRIVE=1 vs DRIVE=2 | Read `GearCommand::DRIVE` from the message header; never hard-code `1` |
| `0x310` steering sign | Negate on TX and RX (E-Trike internals are +right, Universe is +left) |
| `0x120` / `0x310` missing `header.stamp` + `frame_id` | Populate in the bridge (required by `vehicle_velocity_converter`) |
| `VehicleKinematicState` publish | Delete (type deleted upstream); optional `nav_msgs/Odometry` |
| Engage topic | `/control/command/engage` → `/vehicle/engage` |
| Mode request | `ControlModeCommand` message → `rclcpp::Service` returning `success` |
| Emergency | On `VehicleEmergencyStamped.emergency=true` → send `0x001` (assert, DLC=0). On `false` → **stop asserting only**; do NOT attempt to clear the hardware latch (that requires the physical START button / mode long-press — `architecture.md` §3/§14 — and clearing it via CAN would defeat the safety interlock). Gate driving on SYS `estop_active` (`0x011` byte0) and report `ControlModeReport::DISENGAGED` while latched. |
| QoS | Subscribe volatile depth 1 to transient_local publishers |
| `speed`→`velocity`; drop `is_defined_speed`/`is_defined_steering_tire_angle`; honour real `is_defined_acceleration` | Field rename + drop nonexistent flags |
| Turn/hazard reports | Never publish `0` (constants start at DISABLE=1) |
| `GearCommand::PARK` (22) | Bridge-only: on PARK, latch and send brake-hold via `0x301 HOST_BRAKE_REQ` (SEB stroke/pressure), set `0x300.gear = NEUTRAL`, and report `GearReport::PARK` from bridge memory until a different gear command arrives. No PARK value exists on `0x300`, but NEUTRAL + held brake is functionally equivalent for park-hold. SYS owns the actuator; bridge only commands it. |

### 10.2 Cannot be fixed in the bridge — deferred (these need a protocol/firmware change)

Per the bridge-first decision **we keep all existing frames and add none**, so the items
below are recorded as deferred protocol work, not blockers. **None of them stop the minimal
stack from being drivable** — they are contract-fidelity / capability gaps.

| Item | Why the bridge alone fails | Deferred fix (later phase) |
| --- | --- | --- |
| Standstill steering | `0x300` carries yaw-rate only; at v≈0 yaw≈0, so RT has no angle to act on. **No signal on the existing bus carries a tire angle.** | new `0x303 host_steer_cmd` (angle) — or repack `0x300` |
| `emergency=false` handling | **Not a gap.** Clearing the hardware ESTOP is *physical-only by design* (START button / mode long-press 3s; `architecture.md` §3/§14) — software must not clear it. Bridge asserts on `true`; on `false` it stops asserting and gates on SYS `estop_active` (`0x011` byte0) + reports DISENGAGED. No new frame. | none — handled in bridge |
| Host→vehicle mode request frame | No carrier frame exists on the high bus | new `0x304 host_mode_req` |
| `ControlModeReport` 7-state + ack | Bridge can't report states SYS/RT never send | new `0x211 rt_mode_ack` |
| `heading_rate` (real) | RT must compute yaw rate from SES angle + speed | new `0x121 rt_motion_rpt` |
| ESTOP `source`/`reason` (optional) | Bridge already has `estop_reason` (`0x210` byte1) + `estop_active` (`0x011`); reporting *why* is nice-to-have | optional `estop_source` in `0x011` |
| PARK_REJECTED feedback only | Bridge can't know if the brake-hold actually achieved/held (no firmware feedback for PARK state) | deferred `PARK_REJECTED` in gear enum + `0x121` |
| `0x310` 10 Hz → 20 Hz | RT publishing cadence, not bridge | timing-only change |

### 10.3 Must firmware (RT/SYS) changes — honest verdict

**Under the bridge-first / keep-frames constraint, NO firmware change is required to make the
stack drivable end-to-end.** The bridge alone clears the functional gate (§10.1). The
firmware gaps above are *deferred* because every one of them needs either a new frame or a
`0x300` repack — both excluded by the current decision.

The only firmware change that becomes a **practical must for real operation** (not a trivial
demo) is:

- **RT: standstill-steering / angle pass-through.** `enable_engage_on_driving:false` means
  engage only from standstill, and most pull-outs/garage maneuvers steer at v≈0. Today both
  the bridge (`:172`) and RT (`physics_model.cpp:66-70`) zero steering at low speed. Because
  `0x300` has no angle signal, RT cannot recover it — so this gap is **only closable via a
  protocol change** (new `0x303` or repack). Until then, accept: drivable while moving, no
  standstill steering.

All other firmware items (heading_rate, 20 Hz, ESTOP clear, PARK, 7-state ack) are
quality/safety-compliance gaps that do not block motion.

**Rule of thumb:** if the data already exists on the CAN bus → fix in the bridge. If it must
be *produced* by RT/SYS/ECU and no existing frame carries it → it is a deferred protocol
change, not a blocker.

---

## 11. Orphans that are not out-of-the-box Autoware, but are wanted for other work

Five high-bus items have no Autoware producer/consumer and were flagged "orphan" — yet
several are valuable for internal tooling, diagnostics, and future Autoware features. Keep
them; do not delete.

| Item | Not needed by stock Universe, but wanted because… |
| --- | --- |
| `0x400 HOST_OBSTACLE_DIST` | No vehicle-interface topic today, **but** it is the natural carrier for an AEB / forward-collision or speed-limit input from a future perception node; control-toolkit and debug-tool already display it. Keep wired (even if the bridge leaves it unpublished for now) rather than removing the dictionary entry. |
| `0x220 RT_PID_RPT` | Reserved today, but RT's PID/ resolver telemetry is exactly what tuning and regression tooling need; reusing it later avoids a new frame. Keep reserved, document intent. |
| `0x111 HMI_MODE_REQ` | Host receives but never decodes, **yet** it is the closest existing precedent for a host-request frame and its `parse_hmi_mode` arbitration in SYS is the pattern `0x304 host_mode_req` should reuse. Keep it as the HMI path. |
| `0x302 headlight` bit / `0x011 headlight` echo | No Autoware light source, **but** headlight control matters for manual/night operation and any future `ExteriorLights` interface; harmless to keep round-tripping. |
| `0x311 BRAKE_DIAG` / `0x600 SYS_DIAG_RPT` | Diagnostics-only (no Autoware command contract), **but** essential for the maintenance/QA workflow and for feeding `diagnostic_aggregator` if we later adopt it. Keep. |

Decision: **no orphan is removed.** Orphans are either retained (future use) or, where a
real Autoware consumer exists later, promoted to a mapped signal.

---

## 12. By-wire ECU feasibility (from `by-wire - steering.csv` / `by-wire - brake.csv`)

The raw SES (steering) and SEB (brake) ECU contracts confirm that every protocol gap in
§10.2 is *physically supported by the ECU* — the additive frames are just pass-throughs.

### 12.1 Steering ECU (SES) — angle-mode is native

- `VCU_SES_Tgt_StrAngle` is a **signed 16-bit, 0.1°/bit** target (±700°). E-Trike's
  `0x303 steer_angle_mdeg` (i16, ±45000 mdeg = ±45°) maps to SES centidegrees with a /10 —
  no precision loss, and `kSteerLimitDeg=40` is well inside range.
- `VCU_SES_Control_Enable` (bit1) Rising-edge → **Angle Control Mode**; `SES_Status`
  `SES_Control_Mode_Status` reports 0=Manual / 1=Automatic. So RT forwarding `0x303` to SES
  is a supported, advertised mode — no ECU firmware change needed.
- `VCU_SES_Alignment_Enable` + `SES_INF_Angle_Status` (0=centering, 1=found) → RT can gate
  `AUTONOMOUS` report on `NOT_READY` until center-found (closes the missing NOT_READY gap).
- Feedback `SES_StrAngle` (signed 0.1°, ±700°) + `SES_Tgt_StrAngleSpd` give RT everything to
  derive `heading_rate` (`v·tan(δ)/L`) and to raise `0x310` to 20 Hz from real ECU data.

**Verdict:** standstill steering, NOT_READY gating, heading_rate, and 20 Hz steer feedback
are all **achievable via additive frames + RT pass-through** — no ECU changes.

### 12.2 Brake ECU (SEB) — PARK-hold and emergency stop are native

- `VCU_SEB_Stroke_Value_Req` (signed 0.05 mm, −30..27 mm) and `VCU_SEB_AutoBrake` /
  `SEB_Control_Mode` give SYS a direct brake-stroke command. **PARK = keep stroke applied**
  is implementable in SYS brake task with no ECU change.
- `SEB_Pressure_Value` / `SEB_Stroke_Value` feedback → SYS can confirm hold; reject reason
  for PARK (e.g. stroke not achieved) can be reported on `0x121 gear_actual=PARK_REJECTED`.
- `SEB_AutoBrake_Status` + `SEB_Error_Status` (L1/L2/L3) feed the ESTOP *trigger reason*
  (`estop_reason` on `0x210`, optional `estop_source` on `0x011`) from real ECU fault state.
  **Note:** this is diagnostics only — the hardware ESTOP *clear* remains physical-only by
  design and must never be CAN-driven (`architecture.md` §3/§14).

**Verdict:** PARK brake-hold and PARK_REJECTED reporting are **achievable via SYS logic over
existing SEB commands** — no ECU changes. ESTOP clear is intentionally physical-only; the
bridge asserts via `0x001` and gates on `estop_active`, which is already sufficient.

### 12.3 What the ECU docs show is NOT possible without firmware

- The ECUs expect their own proprietary IDs (0x169/0x201/0x202/0x203, 0x7B9/0x721/0x731/
  0x741). E-Trike's high-bus `0x300`/`0x210`/etc. are a separate abstraction layer; RT/SYS
  already translate. **No new ECU-side work is implied** by the additive frames.
- `VCU_Veh_Spd_Value` in SES frames is an 8-bit 0..255 speed the ECU wants — RT already
  supplies speed; consistent with current flow.
- Nothing in the ECU docs contradicts the +right internal convention; the sign flip (§10.1)
  remains a bridge concern only.

**Net:** the by-wire ECUs are not a blocker. Every Universe gap is closable at the RT/SYS
abstraction layer via additive frames; the ECUs already speak angle, stroke, and fault
state in the form RT/SYS need.

