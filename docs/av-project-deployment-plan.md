# E-Trike Autoware Deployment Plan

This document covers deployment into the synchronized `av_project` workspace.
It is separate from `docs/universe-migration-implementation.md`, which
describes the Autoware and CAN interface migration itself.

## Production readiness status

| Area | Status | Action required |
|---|---|---|
| Bridge code (Universe types, signs, gears, mode, emergency) | ✅ Implemented | Verify QoS below |
| Phase 2 protocol (`0x303`, `0x121`) | ✅ Implemented | — |
| RT firmware (standstill steering, motion report) | ✅ Implemented | — |
| Control-toolkit, simulation, debug-tool, vt-console | ✅ Implemented | — |
| Header stamps + `frame_id` on reports | ✅ Fixed | — |
| QoS compatibility | ✅ **Fixed in code** | VOLATILE durability connects to both publisher types |
| Engage topic source | ⚠️ **Must verify** | See integration-assumptions section |
| Single vehicle interface (no `control_cmd` conflict) | ⚠️ **Must verify** | Disable standard `autoware_vehicle_interface` |
| `colcon build` on Jetson | ❌ Not done | Step 3 in deployment sequence |
| `vcan` integration test | ❌ Not done | Step 5 |
| Hardware bench (propulsion disabled) | ❌ Not done | Phase 3 |
| Ground motion validation | ❌ Not done | Phase 3 |

**Blockers before first hardware test:** QoS verification, `colcon build`, `vcan` test.

**Scope:** This plan covers **one** Autoware subsystem — the vehicle interface
(Autoware commands ↔ CAN ↔ firmware feedback), replacing Universe's
`autoware_vehicle_interface`. Adding the bridge does **not** by itself make the
E-Trike fully autonomous at Autoware Universe level. Full autonomy additionally
requires the complete Autoware stack — sensing (LiDAR/camera), localization
(GNSS/IMU), perception, prediction, planning, and control — plus maps
(`map_path`), route, and `vehicle_info`/control-parameter tuning, all deployed
and configured separately from `av_project`'s Autoware overlay. Those pieces are
outside this document.

## Syncthing boundary

Syncthing syncs the `av_project` tree only. `E:\work\etrike` is outside that
tree, so the Jetson cannot build the bridge from this repository checkout.
Source packages must be migrated into the synchronized Autoware overlay.

## Source packages (as they exist in this repository)

Four ament packages provide the E-Trike vehicle interface:

| Package | Location in repo | Purpose |
|---|---|---|
| `autoware_vehicle_bridge` | `jetson/src/autoware_vehicle_bridge/` | ROS 2 lifecycle node converting Autoware commands to CAN frames and decoding feedback |
| `etrike_protocol` | `protocol/` | Generated CAN protocol headers (`protocol/generated/cpp/etrike_protocol.hpp`) |
| `etrike_vehicle_launch` | `jetson/src/etrike_vehicle_launch/` | Vehicle-interface launch wrapper that includes the bridge |
| `etrike_vehicle_description` | `jetson/src/etrike_vehicle_description/` | Xacro model and vehicle parameters (`vehicle_model:=etrike`) |

`etrike_vehicle_launch` depends on `autoware_vehicle_bridge` and
`etrike_vehicle_description` via `<exec_depend>` in its `package.xml`. The
bridge depends on `etrike_protocol` via `find_package(etrike_protocol REQUIRED)`
and links the exported INTERFACE target.

Keep vehicle launch and description packages separate so Autoware can discover
them from `vehicle_model:=etrike`. Do not add these packages to upstream
Autoware repositories.

> **`vehicle_model` discovery (resolved in code):** `etrike_vehicle_description` now
> installs `config/etrike/vehicle_info.param.yaml` (nested under `config/<vehicle_model>/`,
> matching Autoware Universe's expectation) plus `urdf/vehicle.xacro`. `vehicle_model:=etrike`
> now resolves. Keep the flat `config/vehicle_info.param.yaml` removed to avoid a second
> source of truth.

## Target layout in the av_project workspace

When migrated into the synchronized overlay, the packages should land under
`av_project/autoware/src/our_packages/` (or the equivalent directory that
workspace uses for custom packages). The exact destination path depends on the
`av_project` workspace conventions; this repository's layout (`jetson/src/` and
`protocol/`) does not directly match.

## Git and synchronization requirements

`av_project/.stignore` should exclude `.git`, build, install, and log
directories. Source files under `autoware/src/our_packages` will sync to Linux,
but Git metadata will not. Each custom package should therefore be a separately
maintained repository and listed in:

```text
av_project/repositories/our_autoware.repos
```

That manifest is required for clean recovery and for rebuilding the Jetson
workspace with `vcs import`; Syncthing alone is not a substitute for it.
(These files live in the `av_project` tree and cannot be verified from this
checkout.)

## Deployment sequence

### Phase 1 — Build and verify ROS integration

1. Copy or symlink the four packages from this repository into the
   `av_project/autoware/src/our_packages/` overlay (or use `vcs import` from
   `our_autoware.repos`).
2. Wait for Syncthing completion and verify the source on Linux.
3. Build: `colcon build --packages-select etrike_protocol autoware_vehicle_bridge etrike_vehicle_launch etrike_vehicle_description`. Fix any compilation errors. The bridge uses fields from `autoware_control_msgs/msg/Control` and `autoware_vehicle_msgs/*` — confirm these match the pinned Universe headers in `av_project/autoware/src`.
4. Source the overlay and confirm the packages are discoverable:
   ```bash
   ros2 pkg executables autoware_vehicle_bridge
   ros2 pkg executables etrike_vehicle_launch
   ```
5. Verify launch with `vehicle_model:=etrike`:
   ```bash
   ros2 launch etrike_vehicle_launch vehicle_interface.launch.xml can_interface:=vcan0
   ```
   Confirm the bridge node reaches the **active** lifecycle state.

### Phase 2 — QoS and topic verification

6. **Verify QoS compatibility** (critical — see QoS section below):
   ```bash
   # Check the durability advertised by upstream Autoware publishers
   ros2 topic info -v /control/command/control_cmd
   ros2 topic info -v /control/command/gear_cmd
   ros2 topic info -v /control/command/emergency_cmd
   ```
   If any publisher reports `VOLATILE` durability, the bridge subscriptions
   must be changed from `transient_local` to match. This is a **silent failure**
   mode — the bridge will receive no messages without visible errors.

7. Verify topic remapping:
   ```bash
   ros2 topic echo /vehicle/status/velocity_status
   ros2 topic echo /vehicle/status/steering_status
   ros2 topic echo /vehicle/status/control_mode
   ```
   Confirm headers include `stamp` and `frame_id="base_link"` on
   `VelocityReport`; confirm `stamp` is set on all other reports.

### Phase 3 — CAN bench testing (vcan)

 8. Run `vcan` integration: inject CAN frames matching the protocol golden
    vectors and verify ROS output. Test:
    - `0x121` motion report → `VelocityReport` (speed, heading_rate) **and**
      `GearReport` (authoritative gear source in code, `decode_motion`)
    - `0x310` steering diag → `SteeringReport` (sign-corrected)
    - `0x210` state report → `ControlModeReport` + confirmed AUTO
    - `0x011` safety status → turn/hazard reports + ESTOP state
    - `0x206` motor feedback → received for diagnostics only; its gear field is
      **not** surfaced (GearReport comes from `0x121`, not `0x206`)

9. Verify mode/engage gating:
   - Send engage=false → no CAN motion output
   - Send engage=true without confirmed AUTO → no CAN motion output
   - Send engage=true + confirmed AUTO → motion commands pass through
   - Send emergency=true → ESTOP frame sent, engage cleared
   - Send emergency=false → ESTOP de-asserted, waits for physical reset

10. Verify zero-yaw guard: inject control commands at `abs(speed) < 0.05 m/s`
    → confirm legacy yaw in `0x300` is 0 while `0x303` steering angle remains
    valid.

### Phase 4 — Hardware validation (propulsion disabled)

11. Connect to real CAN bus (not vcan). Verify heartbeat, liveness, and
    timeout behavior with real RT/SYS firmware.
12. Test mode request via `/control/control_mode_request` service:
    - Request AUTONOMOUS → confirm `0x111` sent → wait for `0x210` AUTO
    - Request MANUAL → confirm engage cleared
    - Request unsupported mode → `success=false`
13. Test with propulsion disabled: sign verification, gear mapping, PARK
    (neutral + brake hold), turn/hazard lights.

### Phase 5 — Ground motion

14. Wheels lifted: verify steering direction, speed limits, ESTOP/reset.
15. Ground motion: only after all previous phases pass and evidence is recorded.

This package migration, manifest work, and synchronized-overlay validation are
required deployment work, not optional post-processing.

## Required checks for each change

Every package or deployment change must include the smallest applicable check
before it is committed:

| Change | Required check |
|---|---|
| `etrike_protocol` message or codec | C++/Python golden-vector round-trip and protocol validation |
| `etrike_vehicle_interface` encoder/decoder | Bridge unit test for CAN bytes, signs, counters, and decoded ROS fields |
| Bridge lifecycle, QoS, or topic wiring | ROS 2 node/launch test with default Autoware QoS and topic discovery |
| Mode, engage, ESTOP, timeout, or gating logic | State-transition unit tests covering both accepted and rejected paths |
| `etrike_vehicle_description` geometry or parameters | YAML/schema validation plus vehicle-model discovery and URDF/Xacro build |
| `etrike_vehicle_launch` launch/remapping | Launch test confirming package lookup, arguments, remaps, and lifecycle activation |
| `our_autoware.repos` or Syncthing layout | Manifest parse/`vcs validate`, clean-overlay import, and source-presence check on Linux |
| CAN routing or firmware-facing behavior | Native/firmware dispatcher test plus vcan loopback where available |

The check result must be recorded with the commit. A package is not considered
deployed merely because its source synchronized; it must build, be discoverable,
and pass its applicable interface test in the Jetson overlay.

## QoS compatibility

**Current bridge code** (`vehicle_bridge_node.cpp:500`):
```cpp
// VOLATILE durability: connects to both VOLATILE and TRANSIENT_LOCAL publishers.
const auto command_qos = rclcpp::QoS(1).reliable();
```

All subscriptions (control, gear, turn, hazard, engage, emergency) now use
**VOLATILE** durability. All publishers use `rclcpp::QoS(1)` (reliable, volatile —
the default). **This is already fixed in code** (`transient_local` removed): a
VOLATILE subscription connects to publishers offering either VOLATILE or
TRANSIENT_LOCAL durability, so the bridge can no longer silently fail to receive
control commands.

**Background (why this matters):** a `transient_local` subscription only connects
to a publisher that also offers `transient_local` durability. If the upstream
Autoware `/control/command/*` publishers use default `VOLATILE` durability, the
bridge would have **silently received no control messages**. That risk is now
resolved by using VOLATILE.

**Verification command** (confirm, not fix):
```bash
ros2 topic info -v /control/command/control_cmd
```
The bridge subscription should now show durability `volatile` and connect
regardless of the publisher's durability.

## Integration assumptions to verify (silent-failure risks)

Beyond QoS, these bridge↔Autoware assumptions were checked against the source and
must be confirmed against the target Universe version before first motion:

- **Engage source (S1, new):** the bridge subscribes `~/input/engage` remapped to
  `/api/autoware/get/engage` (`vehicle_bridge_node.cpp:509`,
  `vehicle_bridge.launch.py:27`) and only flips `engaged_` true on that topic
  (`on_engage`, `:707-710`). In Autoware Universe `/api/autoware/get/engage` is
  normally an **external-API service**, not a published topic. If no topic of type
  `autoware_vehicle_msgs/msg/Engage` is published there, the subscription connects
  to nothing → `engaged_` stays false → gate stuck closed, **no error**. Verify with
  `ros2 topic info -v /api/autoware/get/engage`; if it is a service only, remap
  `~/input/engage` to the real engage topic (e.g. `/control/engage`) or add a service
  client. (See risk register + TODO.)
- **Single vehicle interface (S1):** the bridge consumes the high-level
  `/control/command/control_cmd` and performs its own conversion (replacing
  `raw_vehicle_cmd_converter` + `autoware_vehicle_interface`). Ensure the standard
  `autoware_vehicle_interface` is **disabled/overridden** so only this bridge consumes
  `control_cmd`; `etrike_vehicle_launch` is the replacement. The unused
  `raw_vehicle_cmd_converter_param_path` arg in `vehicle_interface.launch.xml` is a
  no-op by design.
- **Message field schema (lower risk):** the bridge uses the standard Universe
  `autoware_control_msgs/msg/Control` fields — `longitudinal.velocity`,
  `longitudinal.acceleration`, `longitudinal.is_defined_acceleration`,
  `lateral.steering_tire_angle` (`:197-219`,`:273-276`,`:851`) — and
  `VelocityReport.{longitudinal_velocity,lateral_velocity,heading_rate,header}`
  (`:319-325`,`:423-426`,`:952-953`). These match the published Universe schema, so
  the `colcon build` field-mismatch risk is **low** but still verify against pinned
  headers (Phase 1 step 3).
- **`confirmed_auto_` path (verified OK):** set true only from `0x210` when
  `mode == kModeAuto && safety_state == 0` (`:1015-1017`); requires RT firmware to
  send AUTO with `safety_state == 0` (firmware validation item).

## Header stamps and frame_id

The bridge now populates headers correctly:

| Report | stamp | frame_id | Source |
|---|---|---|---|
| `VelocityReport` | `now()` | `"base_link"` | `vehicle_bridge_node.cpp:952-953` |
| `GearReport` | `now()` | — | `vehicle_bridge_node.cpp:954` |
| `SteeringReport` | `now()` | — | `vehicle_bridge_node.cpp:1039` |
| `ControlModeReport` | **NOT set** | — | `decode_state()` sets `mode` only; `header.stamp` must be added before publish (`publish_vehicle_reports` line 1029-1032) |
| `DiagnosticArray` | `now()` | — | `vehicle_bridge_node.cpp:885,1050` |

## ControlModeReport state mapping

The bridge maps CAN `0x210` mode values to Universe `ControlModeReport`:

| CAN mode | Universe state | Notes |
|---|---|---|
| AUTO (1) + safety_state=0 | `AUTONOMOUS (1)` | Only when SYS confirms |
| MANUAL (0) | `MANUAL (4)` | |
| ESTOP (2) | `DISENGAGED (5)` | |

**Known limitation:** Universe defines 7 states including `NOT_READY (6)` (SYS
transitioning / steering not centred). E-Trike's CAN protocol does not carry a
distinct `NOT_READY` state. The bridge reports `DISENGAGED` for any non-AUTO,
non-MANUAL state, which is the safe fallback. If `NOT_READY` is required for
upstream `operation_mode_transition_manager`, SYS firmware must be extended —
this is **not** a bridge change.

## Key bridge behaviors verified against source code

**Zero-yaw guard** (`motion_conversion.hpp:39`):
When `abs(speed_mps) < low_speed_threshold` (default 0.05, configurable via
`etrike.param.yaml:9`), derived yaw is clamped to 0. Direct-angle steering
(`0x303`) remains valid at standstill.

**Mode/engage gating** (`vehicle_bridge_node.cpp:770-795`): Motion transmission
requires all of:
- `engaged` is true
- `confirmed_auto` is true (RT feedback confirms AUTO mode)
- `software_emergency` is false
- `sys_estop_active` is 0
- RT heartbeat, SYS status, state report, and motion report are all alive
- SYS heartbeat is OK

Loss of any gate sends neutral drive plus invalid steering and clears the
cached control command. Recovery requires a fresh post-fault control command.

**Emergency handling** (`vehicle_bridge_node.cpp:737-757`): Checks
`msg->emergency` boolean — `true` asserts CAN ESTOP, `false` clears the
software latch only (physical recovery remains with SYS/operator). ESTOP frames
are rate-limited to max 1 per 500 ms.

**Gear constants** (`vehicle_bridge_node.cpp:68-78`): Universe values
(NONE=0, NEUTRAL=1, DRIVE=2, REVERSE=20, PARK=22, LOW=23) are separate from
CAN bus values (CAN_N=0, CAN_D=1, CAN_S=2, CAN_R=3). DRIVE=1 bug from
Autoware.Auto era is fixed.

**PARK handling**: Universe PARK command is mapped to NEUTRAL gear + brake-hold
(`0x301`), with `park_requested_` flag used to override `GearReport` to PARK
(`vehicle_bridge_node.cpp:957-959`).

**ControlModeCommand** (`vehicle_bridge_node.cpp:511-514`): Implemented as a
service (`/control/control_mode_request`), not a subscription. Supports
AUTONOMOUS and MANUAL; returns `success=false` for unsupported modes. Sends
`0x111` with rolling counter via existing SYS mode-request path.

**Topic names** (`vehicle_bridge_node.cpp:501-525`):

| Function | Topic |
|---|---|
| control command | `/control/command/control_cmd` |
| gear command | `/control/command/gear_cmd` |
| turn-indicator command | `/control/command/turn_indicators_cmd` |
| hazard command | `/control/command/hazard_lights_cmd` |
| engage | `~/input/engage` (remapped to `/api/autoware/get/engage` in launch) |
| control-mode request | `/control/control_mode_request` (service) |
| emergency command | `/control/command/emergency_cmd` |
| velocity report | `/vehicle/status/velocity_status` |
| steering report | `/vehicle/status/steering_status` |
| gear report | `/vehicle/status/gear_status` |
| control-mode report | `/vehicle/status/control_mode` |
| turn-indicator report | `/vehicle/status/turn_indicators_status` |
| hazard report | `/vehicle/status/hazard_lights_status` |
| diagnostics | `~/output/diagnostics` |

**Lifecycle** (`vehicle_bridge.launch.py`): The bridge is a `LifecycleNode`.
Launch configures and activates it automatically via `TRANSITION_CONFIGURE`
and `TRANSITION_ACTIVATE`.

**QoS**: See dedicated QoS section above. Publishers use depth-1 reliable
(default volatile). Subscriptions use depth-1 reliable with **VOLATILE** durability
(**fixed in code** — compatible with both VOLATILE and TRANSIENT_LOCAL publishers).

## Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| QoS mismatch → bridge receives no control commands | **S1 (resolved)** | Fixed in code: subscriptions now VOLATILE; verify connect with `ros2 topic info -v` |
| `colcon build` fails due to message field mismatch | **S1** | Build early; fix against pinned Universe headers |
| Steering sign wrong → vehicle steers opposite | **S1** | Unit tests pass; verify on bench with wheels lifted |
| Gear DRIVE=1 vs DRIVE=2 → vehicle never moves | **S1** | Fixed in code; verify on bench |
| 10 Hz steering feedback marginal for MPC | **S3** | Monitor; consider `0x310` rate increase if needed |
| `NOT_READY` state unrepresentable | **S2** | Safe fallback to DISENGAGED; extend SYS only if required |
| PARK silently becomes NEUTRAL without brake hold | **S2** | Brake-hold path implemented; verify on hardware |
| Engage topic mismatch → bridge never receives engage → gate stuck closed | **S1** | `engage_topic` is now a launch arg; verify it is a published `Engage` topic in the target Universe version (set `engage_topic`, e.g. `/control/engage`, if `/api/autoware/get/engage` is only a service) |
| Standard `autoware_vehicle_interface` also consumes `control_cmd` → command conflict | **S1** | Disable/override it; only this bridge consumes `control_cmd` |
| Gear falls to NEUTRAL when `GearCommand::NONE` or `|v|<0.05 m/s` → no motion / gear thrash | **S2** | **Fixed in code**: latch DRIVE when engaged (gear-select block in `vehicle_bridge_node.cpp`) |
| Brake frame omitted when `is_defined_acceleration` is false → coast only, no active braking | **S3** | **Fixed in code**: undefined acceleration now sends explicit zero-brake frame (`encode_brake`) |
| Dual vehicle-geometry config (bridge `etrike.param.yaml` vs description `vehicle_info.param.yaml`) diverge | **S3** | Keep `wheel_base`/`max_steer_angle` in sync; single source of truth |

## Full-pipeline verification (integration test plan)

Goal: prove every segment works — Autoware topics → bridge → CAN → firmware →
CAN → bridge → ROS 2 reports — and the closed loop under mode/engage and faults.
This complements the phased deployment sequence (Phases 1–5 above) with the
concrete tests that must pass at each phase.

Pipeline segments:
```
Autoware pkg ─▶ [1] ROS2 topics ─▶ [2] Bridge encode ─▶ CAN ─▶ [3] Firmware decode/actuate
                                                        │
Firmware report ◀─ CAN ◀─ [5] Bridge decode ◀─ [4] Firmware encode ─┘
     │
     ▼
[6] ROS2 reports ─▶ Autoware control loop   [7] mode/engage + fault injection
```

### Existing coverage (verified in repo)
| Layer | Test artifact | Covers |
|---|---|---|
| Codec roundtrip | `protocol/tests/cpp/test_protocol.cpp`, `protocol/tests/cpp/test_generated_vectors.cpp` | encode/decode of all messages, golden-vector roundtrip |
| Bridge math | `jetson/src/autoware_vehicle_bridge/test/test_motion_conversion.cpp` | yaw guard, sign flip, `0x303` + `RtMotionRpt` codec |
| RT logic | `rt-esp32/test/*`, `native-test/test/test_rt_phase2_motion.cpp`, `test_rt_can_dispatch`, `test_rt_safety_monitor` | direct-steer apply, yaw est, dispatch, heartbeat |
| SYS logic | `sys-esp32/test/*` (`test_mode_manager`, `test_rt_sys_integration`, `test_safety_monitor` …) | mode mgr, RT/SYS integration, safety |
| MTR | `mtr-stm32/test/test_gear_throttle.cpp` | gear/throttle |
| Sim | `simulation/tests/{unit,integration,scenarios}` | TS protocol-level behavior |

### Gaps → new tests required

**L1 — Autoware → bridge (topics in) [MISSING]**
- `test_bridge_subscriptions_qos`: publish `/control/command/control_cmd` etc. with
  **Autoware-default QoS** (`rclcpp::QoS(1).reliable()`, VOLATILE) and assert the
  bridge receives them. Should now pass (subscriptions are VOLATILE) - automated form
  of Phase 2 step 6.
- `test_bridge_accepts_all_commands`: inject Control/Gear/Turn/Hazard/Emergency/
  Engage; confirm each callback fires and caches the latest command.

**L2 — Bridge encode → CAN frames [MISSING]**
- `test_bridge_encode_control`: from a known `Control` (v=1.0 m/s, steer=+0.1 rad),
  assert `0x300` (drive), `0x301`/`0x302` (brake/light), `0x303` direct-steer with
  correct sign flip (Universe left-positive → trike right-positive), rolling
  counters, DLC. Compare against golden vectors in `protocol/vectors/payload-v1.json`.
- `test_bridge_encode_gear`: Universe DRIVE/REVERSE → CAN_D/CAN_R
  (`vehicle_bridge_node.cpp:68-78`).
- `test_bridge_encode_emergency`: `emergency=true` → exactly one `0x001` ESTOP
  frame, second within 500 ms suppressed (`vehicle_bridge_node.cpp:737-757`).
  (`0x7FC` is the Host heartbeat, not ESTOP.)

**L3 — CAN → firmware decode/actuate [needs bridge-frame acceptance]**
- Feed **bridge-encoded golden frames** (real `0x300`/`0x303` bytes from L2) into
  RT's `can_rx_router`; assert setpoint applied, direct-angle steering honored at
  standstill, yaw derived only above `low_speed_threshold`. Closes bridge→RT using
  the actual encoder output (Phase 3/4).

**L4 — Firmware encode → CAN feedback [EXISTS]** (`make_motion_report`, SYS status)
— extend to assert exact byte layout matches the bridge decoder's expectation.
Phase 3 step 8 already exercises decode; add an encode-side golden-vector check.

**L5 — CAN → bridge decode → reports [MISSING]**
- `test_bridge_decode_feedback`: inject `RtMotionRpt`/`RtStateRpt`/`SysSafetySts`/
  `SysThrottleSts`; assert `/vehicle/status/velocity_status`, `steering_status`,
  `gear_status`, `control_mode` carry correct values and sign (trike→Universe flip).
- `test_bridge_zero_yaw_report`: at `speed < 0.05` assert reported yaw_rate == 0
  while `0x303` steering stays valid (Phase 3 step 10).

**L6 — Mode/engage gating [MISSING]**
- `test_bridge_gate_closed`: with `engaged=false` OR `confirmed_auto=false` OR
  `software_emergency` OR `sys_estop_active` OR any heartbeat dead → assert bridge
  sends **neutral drive + invalid steering** and clears cached command
  (`vehicle_bridge_node.cpp:770-795`, `send_safe_motion`). Mirrors Phase 3 step 9.
- `test_bridge_gate_open`: after engage=true + `ControlModeRequest`(AUTONOMOUS)
  accepted by RT (mock `confirmed_auto=true`) + all heartbeats alive → real motion.
- `test_bridge_fault_recovery`: drop SYS heartbeat → neutral/invalid sent; restore
  → motion resumes **only after a fresh command** (not the stale cached one).

**L7 — Closed-loop / HIL integration [MISSING, highest value]**
- `hil_loopback_vcan`: bring up `vcan0`; run the bridge node **and** the native
  firmware dispatchers (`native-test` builds `rt_can_dispatch`, `sys_can_dispatch`
  as native exes) on the same vcan. Drive a `Control` topic; assert firmware
  setpoints track and bridge reports track firmware feedback. Exercises L1–L6 on
  real sockets (Phase 3).
- `hil_full_autoware_sim`: launch Autoware Universe `control` + bridge + firmware
  sim; run a scripted route; assert velocity/steering reports converge to command
  and the ESTOP path works (Phase 4/5).
- `test_vehicle_model_discovery`: `ros2 launch` with `vehicle_model:=etrike`
  resolves `etrike_vehicle_description` (currently fails — discovery caveat above;
  Phase 1 step 5).

### How to run (entry points already in repo)
- Bridge unit + new node tests: `colcon test --packages-select autoware_vehicle_bridge`
  (add a gtest/launch_testing harness; today only the header test builds).
- Codec/firmware logic: `native-test` CMake + `ctest`; firmware:
  `platformio test -d rt-esp32 -d sys-esp32 -d mtr-stm32`.
- HIL: vcan + `native-test` dispatchers + bridge node.

### Sign-off criteria (all must pass)
1. L1 QoS test green with **Autoware-default** publishers (proves QoS defect fixed).
2. L2/L5 golden-vector encode/decode match `protocol/vectors/*`.
3. L3 firmware accepts bridge-encoded frames (real encoder output).
4. L6 gate closed by each of the 6 conditions; opens only on fresh command after
   AUTO confirmed.
5. L7 loopback: reported velocity/steering track commanded within tolerance; ESTOP
   asserted and cleared correctly.
6. `vehicle_model:=etrike` resolves (discovery defect fixed).

The math and firmware logic are well-tested; the bridge **node** (L1/L2/L5/L6) and
any **integrated** run (L7) have no tests today. Those gaps must be closed before
claiming the pipeline works end-to-end.

## Open actions (TODO)

Consolidated outstanding work. Severity references the Risk register. Items marked
[code] change source; [test] add a test from the integration plan above; [ops] are
deployment/validation steps.

**Blocking defects (must close before first hardware test)**
- [x] [code] Fix QoS: subscriptions now VOLATILE (`rclcpp::QoS(1).reliable()`,
  `vehicle_bridge_node.cpp:500`) — connects to both VOLATILE and TRANSIENT_LOCAL
  publishers. Verify with L1 test. **S1 — done**
- [x] [code] Fix `vehicle_model` discovery: params now nested under
  `config/etrike/vehicle_info.param.yaml` (flat file removed). **S1 — done**
- [ ] [ops] `colcon build` the four packages on the Jetson against pinned Universe
  headers; resolve any message-field mismatch. **S1**
- [ ] [code/ops] Verify engage source: `engage_topic` is now a launch arg (default
  `/api/autoware/get/engage`); confirm it is a published `Engage` topic in the target
  Universe version, or set `engage_topic` (e.g. `/control/engage`) if it is only a
  service. **S1 (silent failure)**
- [ ] [ops] Ensure the standard `autoware_vehicle_interface` is disabled/overridden so
  only this bridge consumes `/control/command/control_cmd`. **S1**
- [x] [code] Fix gear latch: when engaged and no explicit `GearCommand`, default to
  DRIVE (gear-select block in `vehicle_bridge_node.cpp`) — no NEUTRAL at standstill/low
  speed. **S2 — done**
- [x] [code] Fix brake: undefined `is_defined_acceleration` now sends an explicit
  zero-brake frame (`encode_brake`) instead of skipping. **S3 — done**
- [ ] [ops] Keep `wheel_base`/`max_steer_angle` consistent between bridge
  `etrike.param.yaml` and description `vehicle_info.param.yaml` (single source of truth). **S3**

**Bridge node tests (currently missing)**
- [ ] [test] L1 `test_bridge_subscriptions_qos` — Autoware-default QoS reaches bridge.
- [ ] [test] L1 `test_bridge_accepts_all_commands` — all command callbacks cache.
- [ ] [test] L2 `test_bridge_encode_control` / `_gear` / `_emergency` — golden CAN
  frames vs `protocol/vectors/*`.
- [ ] [test] L5 `test_bridge_decode_feedback` / `_zero_yaw_report` — reports correct
  with trike→Universe sign.
- [ ] [test] L6 `test_bridge_gate_closed` / `_gate_open` / `_fault_recovery` — gating
  per `vehicle_bridge_node.cpp:770-795`.

**Firmware bridge-frame acceptance (currently missing)**
- [ ] [test] L3 feed bridge-encoded golden frames into RT `can_rx_router`; assert
  setpoint/standstill-steering/yaw behavior.
- [ ] [test] L4 assert firmware feedback encode byte layout matches bridge decoder.

**Integration / HIL (currently missing)**
- [ ] [test] L7 `hil_loopback_vcan` — bridge + native firmware dispatchers on vcan.
- [ ] [test] L7 `hil_full_autoware_sim` — Autoware control + bridge + firmware sim.
- [ ] [test] L7 `test_vehicle_model_discovery` — `vehicle_model:=etrike` resolves.

**Firmware + safety validation**
- [ ] [ops] Flash + version-match RT/SYS/MTR firmware; validate mode-confirm AUTO and
  SYS heartbeat the bridge gating depends on.
- [ ] [ops] Engage + mode runbook and end-to-end safety/HIL sign-off (ESTOP path,
  fault recovery requiring fresh command).

**Full autonomy (outside this plan — separate workstream)**
- [ ] [ops] Deploy Autoware sensing/localization/perception/planning/control stack.
- [ ] [ops] Provide map (`map_path`) + route; sensor calibration.
- [ ] [ops] `vehicle_info` + control-parameter tuning for Autoware planning/control.

## Referenced documents

- `docs/universe-migration-implementation.md` — migration plan and verification results
- `docs/update-universe.md` — full migration plan and rollout phases
- `docs/communications/high-can-autoware-auto-vs-universe.md` — message type mapping and gap analysis
- `jetson/docs/bridge-update.md` — detailed porting guide for the bridge
- `docs/testing_and_validation/` — test plans and commissioning profiles
