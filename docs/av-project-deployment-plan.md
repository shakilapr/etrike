# E-Trike Autoware Deployment Plan

This document covers deployment into the synchronized `av_project` workspace.
It is separate from `docs/universe-migration-implementation.md`, which
describes the Autoware and CAN interface migration itself.

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

1. Copy or symlink the four packages from this repository into the
   `av_project/autoware/src/our_packages/` overlay (or use `vcs import` from
   `our_autoware.repos`).
2. Wait for Syncthing completion and verify the source on Linux.
3. Build and source the Autoware overlay with `colcon build --packages-select
   etrike_protocol autoware_vehicle_bridge etrike_vehicle_launch
   etrike_vehicle_description`.
4. Confirm the packages are discoverable by the Universe vehicle launch and
   start with `vehicle_model:=etrike` and vehicle-interface launch enabled.
   `etrike_vehicle_launch/launch/vehicle_interface.launch.xml` includes the
   bridge launch and passes `can_interface`.
5. Verify CAN transport, QoS, topics, mode/engage gating, and the bridge's
   zero-yaw guard before propulsion-enabled testing.

This package migration, manifest work, and synchronized-overlay validation are
required deployment work, not optional post-processing.

## Key bridge behaviors verified against source code

**Zero-yaw guard** (`jetson/src/autoware_vehicle_bridge/include/autoware_vehicle_bridge/motion_conversion.hpp:39`):
When `abs(speed_mps) < low_speed_threshold` (default 0.05, configurable via
`etrike.param.yaml:9`), derived yaw is clamped to 0. Direct-angle steering
(`0x303`) remains valid at standstill.

**Mode/engage gating** (`vehicle_bridge_node.cpp:780-795`): Motion transmission
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
CAN bus values (CAN_N=0, CAN_D=1, CAN_S=2, CAN_R=3).

**ControlModeCommand** (`vehicle_bridge_node.cpp:511-513`): Implemented as a
service (`/control/control_mode_request`), not a subscription. Supports
AUTONOMOUS and MANUAL; returns `success=false` for unsupported modes.

**Topic names** (`vehicle_bridge_node.cpp:501-525`):

| Function | Topic |
|---|---|
| control command | `/control/command/control_cmd` |
| gear command | `/control/command/gear_cmd` |
| turn-indicator command | `/control/command/turn_indicators_cmd` |
| hazard command | `/control/command/hazard_lights_cmd` |
| engage | `~/input/engage` (remapped in launch) |
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

**QoS**: All subscriptions use depth-1 reliable (via `rclcpp::QoS(1)`).
Publishers use depth-1 reliable defaults.

## Referenced documents

- `docs/universe-migration-implementation.md` — migration plan and verification results
- `jetson/docs/bridge-update.md` — detailed porting guide for the bridge
- `docs/communications/high-can-autoware-auto-vs-universe.md` — message type mapping
