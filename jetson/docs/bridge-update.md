# E-Trike Vehicle Bridge Update for Autoware Universe

## Purpose

This document describes the changes required to port the Jetson E-Trike vehicle bridge from the legacy Autoware.Auto interfaces to the Autoware Universe workspace in `E:\work\av_project`.

The current bridge is located at:

- Package: `E:\work\etrike\jetson\src\autoware_vehicle_bridge`
- Launch file: `E:\work\etrike\jetson\launch\vehicle_bridge.launch.xml`

The recommended target package name is `etrike_autoware_vehicle_bridge`. Put the ported package under `autoware/src/our_packages/etrike_autoware_vehicle_bridge` and integrate its launch file through the E-Trike vehicle launch package.

This is an interface port, not only a rename. The CAN transport, protocol conversion, safety timers, and vehicle kinematics can largely remain, but the Autoware-facing types and lifecycle integration must change.

## Current bridge summary

The bridge currently:

- subscribes to Autoware.Auto control, gear, indicator, hazard, engage, control-mode, and emergency commands;
- converts those commands to generated E-Trike CAN protocol frames;
- reads feedback through Linux SocketCAN;
- publishes velocity, steering, gear, control-mode, light, kinematic-state, and diagnostic reports;
- uses a ROS 2 lifecycle node;
- implements command timeout, E-stop transmission, host/RT heartbeat monitoring, and dead reckoning.

Preserve those responsibilities during the port.

## Mandatory package rename

Rename the package consistently:

| Existing | Recommended |
|---|---|
| directory `autoware_vehicle_bridge` | `etrike_autoware_vehicle_bridge` |
| package name `autoware_vehicle_bridge` | `etrike_autoware_vehicle_bridge` |
| CMake project `autoware_vehicle_bridge` | `etrike_autoware_vehicle_bridge` |
| C++ namespace `autoware_vehicle_bridge` | `etrike_autoware_vehicle_bridge` |
| include directory `include/autoware_vehicle_bridge` | `include/etrike_autoware_vehicle_bridge` |
| include guard `AUTOWARE_VEHICLE_BRIDGE_*` | `ETRIKE_AUTOWARE_VEHICLE_BRIDGE_*` |

The executable may remain `vehicle_bridge_node`, although `etrike_vehicle_bridge_node` is clearer. Update every package, executable, include, launch, and install reference together.

## Message-package migration

Replace these dependencies in `package.xml`, `CMakeLists.txt`, the header, and the implementation:

| Autoware.Auto dependency | Autoware Universe dependency |
|---|---|
| `autoware_auto_control_msgs` | `autoware_control_msgs` |
| `autoware_auto_vehicle_msgs` | `autoware_vehicle_msgs` |

Keep these dependencies where used:

- `rclcpp`
- `rclcpp_lifecycle`
- `diagnostic_msgs`
- `lifecycle_msgs`
- `tier4_vehicle_msgs` if the emergency topic is retained

Also declare the generated E-Trike protocol as a real dependency. Do not rely on the current `../../..` include path.

## Control-command conversion

Replace `autoware_auto_control_msgs::msg::AckermannControlCommand` with `autoware_control_msgs::msg::Control` in:

- `CanEncoder::encode_drive`;
- `CanEncoder::encode_brake`;
- `VehicleBridgeNode::sub_control_`;
- `VehicleBridgeNode::latest_control_`;
- `VehicleBridgeNode::on_control`;
- local control pointers in `tick_control`.

Use the following field mapping:

| Existing bridge access | Universe access or behavior |
|---|---|
| `cmd.longitudinal.is_defined_speed` | remove; Universe velocity is always present |
| `cmd.longitudinal.speed` | `cmd.longitudinal.velocity` |
| `cmd.longitudinal.is_defined_acceleration` | keep as `cmd.longitudinal.is_defined_acceleration` |
| `cmd.longitudinal.acceleration` | unchanged |
| `cmd.lateral.is_defined_steering_tire_angle` | remove; Universe steering angle is always present |
| `cmd.lateral.steering_tire_angle` | unchanged |
| command timestamp | use `cmd.stamp` or the relevant nested stamp consistently |

Continue clamping velocity and steering to the configured vehicle limits. Validate NaN and infinity before converting values to integer CAN signals. Treat invalid commands as a safety fault and send the existing safe-stop output.

The current braking rule uses negative acceleration. Preserve it initially, but verify that the E-Trike brake-pressure conversion matches the controller's acceleration convention and the configured `max_deceleration`.

## Vehicle message conversion

Change every `autoware_auto_vehicle_msgs::msg::*` reference to `autoware_vehicle_msgs::msg::*` for:

- `Engage`;
- `GearCommand` and `GearReport`;
- `HazardLightsCommand` and `HazardLightsReport`;
- `TurnIndicatorsCommand` and `TurnIndicatorsReport`;
- `VelocityReport`;
- `SteeringReport`;
- `ControlModeReport`.

Do not retain numeric constants copied into the bridge unless the CAN mapping requires an internal enum. Prefer constants from the current message types. In particular, the current Universe `GearCommand` values include `NONE=0`, `NEUTRAL=1`, `DRIVE=2`, `REVERSE=20`, `PARK=22`, and `LOW=23`. The source bridge's hard-coded `DRIVE=1` does not match the current Universe value and must be corrected.

Keep CAN gear values in a separate internal enum so Autoware values and CAN values cannot be confused.

## Control-mode command redesign

The old bridge subscribes to `autoware_auto_vehicle_msgs::msg::ControlModeCommand` on `/control/control_mode_request`. In this Universe workspace, `autoware_vehicle_msgs/srv/ControlModeCommand` is a service, not a message.

Required changes:

1. Remove the `ControlModeCommand` subscription and callback signature.
2. Create an `rclcpp::Service<autoware_vehicle_msgs::srv::ControlModeCommand>` using the control-mode endpoint expected by the vehicle launch configuration.
3. Map `AUTONOMOUS`, steer-only, velocity-only, and `MANUAL` requests to supported E-Trike behavior.
4. Return `success=false` for unsupported partial-autonomy modes unless they are deliberately implemented.
5. Do not report autonomous mode until the vehicle feedback confirms it. A successful request means the request was accepted, not that the physical mode already changed.
6. Continue publishing `ControlModeReport` from CAN feedback.

The existing `Engage` subscription can remain if the deployed Autoware launch still publishes it, but its relationship to the control-mode service must be explicit. Define one state machine for requested mode, engage state, physical mode feedback, emergency state, heartbeat state, and command freshness. CAN drive output should be enabled only when all required gates are true.

## Emergency handling

The current `tier4_vehicle_msgs::msg::VehicleEmergencyStamped` type exists in the Universe workspace. Its fields are `stamp` and `emergency`.

Update `on_emergency` to inspect `msg->emergency`. The current implementation sends E-stop for every received message, including `emergency=false`, which is incorrect.

Required behavior:

- assert the CAN E-stop when `emergency=true`;
- define and document whether `emergency=false` clears a software latch or whether physical/operator reset is required;
- rate-limit repeated E-stop frames without delaying the first assertion;
- prevent stale control commands from resuming motion after emergency clearance;
- publish a diagnostic status showing the emergency source and latched state.

Verify the final emergency topic against the selected Universe launch stack. Keep `/control/command/emergency_cmd` only if that is the actual deployed endpoint.

## Kinematic-state publication

`autoware_auto_vehicle_msgs::msg::VehicleKinematicState` is not provided by the current `autoware_vehicle_msgs` package. Remove `pub_kinematic_state_` or replace it with a current standard interface.

Recommended approach:

- keep `VelocityReport` as the required vehicle-interface output;
- publish dead reckoning as `nav_msgs::msg::Odometry` only if another component needs it;
- use an odometry frame such as `odom` for `header.frame_id` and `base_link` for `child_frame_id`;
- publish the matching TF only when this bridge is the authoritative odometry source;
- avoid publishing localization-owned `/vehicle/status/kinematic_state` data from the CAN bridge.

If odometry is added, declare `nav_msgs` and optionally `tf2_ros` dependencies. Confirm steering sign, yaw sign, timestamp source, covariance, and reset behavior before using this odometry for localization.

## Topic and service interface

Use private input/output names in the node and remap them in launch files. This makes the package reusable and matches normal Autoware integration patterns. Avoid hard-coded global topic names in C++.

Recommended external mappings:

| Function | Universe endpoint |
|---|---|
| control command | `/control/command/control_cmd` |
| gear command | `/control/command/gear_cmd` |
| turn-indicator command | `/control/command/turn_indicators_cmd` |
| hazard command | `/control/command/hazard_lights_cmd` |
| engage | `/control/command/engage` if used by this stack |
| control-mode request | current vehicle-interface service endpoint selected by launch |
| emergency command | verify against deployed control stack |
| velocity report | `/vehicle/status/velocity_status` |
| steering report | `/vehicle/status/steering_status` |
| gear report | `/vehicle/status/gear_status` |
| control-mode report | `/vehicle/status/control_mode` |
| turn-indicator report | `/vehicle/status/turn_indicators_status` |
| hazard report | `/vehicle/status/hazard_lights_status` |
| diagnostics | `/diagnostics` through launch remapping or aggregation |

Check every endpoint with `ros2 topic info -v` or `ros2 service type` after launching Autoware. Matching names are insufficient; publisher/subscriber types and QoS must also match.

## QoS

The bridge currently uses `rclcpp::QoS(1)` everywhere. Review QoS per interface:

- control commands and live reports: depth 1, reliable unless the surrounding stack specifies otherwise;
- diagnostics: use the convention expected by the diagnostics aggregator;
- mode/engage state: consider transient-local only if the corresponding Autoware publisher uses it;
- do not introduce incompatible durability or reliability settings.

Record the selected QoS in tests and verify it with `ros2 topic info -v`.

## Generated CAN protocol dependency

The source includes `protocol/generated/cpp/etrike_protocol.hpp`, but that header is not inside the current bridge package tree. The CMake file compensates with `${CMAKE_CURRENT_SOURCE_DIR}/../../..`, making the build depend on an undocumented external directory layout.

Before building the Universe version:

1. Locate the protocol source and generator output.
2. Prefer a separate `etrike_protocol` CMake/ament package exporting an include target.
3. Add `<depend>etrike_protocol</depend>` and `find_package(etrike_protocol REQUIRED)`.
4. Link the exported target or add only its exported include directories.
5. Pin or record the protocol revision used by the Jetson, SYS, RT, and motor controllers.
6. Add compile-time or unit checks for CAN IDs, DLCs, scaling, signedness, endianness, and range saturation.

Do not manually duplicate generated protocol definitions in the bridge.

## CMake updates

Update `CMakeLists.txt` to:

- use `project(etrike_autoware_vehicle_bridge LANGUAGES CXX)`;
- find `autoware_control_msgs` and `autoware_vehicle_msgs`;
- remove both `autoware_auto_*` dependencies;
- find the packaged E-Trike protocol dependency;
- add `nav_msgs`/`tf2_ros` only if odometry/TF is retained;
- remove `${CMAKE_CURRENT_SOURCE_DIR}/../../..` from include paths;
- install the launch directory as well as config and headers;
- install/export library targets if encoder, decoder, and driver code are split into a testable library;
- enable compiler warnings used by the surrounding workspace;
- add unit and integration tests under `BUILD_TESTING`.

The recommended layout is a library containing CAN driver abstractions, encoder, decoder, safety/state logic, and a small executable containing the ROS node entry point.

## package.xml updates

Update `package.xml` to:

- rename the package;
- describe Autoware Universe rather than Autoware.Auto;
- replace `autoware_auto_control_msgs` with `autoware_control_msgs`;
- replace `autoware_auto_vehicle_msgs` with `autoware_vehicle_msgs`;
- add the E-Trike protocol dependency;
- add optional odometry dependencies only if used;
- add test dependencies for unit tests and launch tests;
- keep a valid maintainer and Apache-2.0 license declaration.

## Launch and lifecycle integration

The current XML launch starts a lifecycle node as a normal `<node>` but does not configure or activate it. Without lifecycle transitions, CAN opening, timers, the receive thread, and lifecycle publishers will not become active.

Choose and implement one approach:

- retain `LifecycleNode` and add explicit configure/activate management with restart/error behavior; or
- convert to a normal `rclcpp::Node` if the vehicle stack does not manage lifecycle nodes.

For a safety-critical hardware interface, retaining lifecycle behavior is useful only when transitions are reliably managed and tested.

The launch file must:

- load `etrike.param.yaml` (the current launch file does not load it);
- remap all private interface names;
- expose `can_interface` as a launch argument or parameter override;
- include lifecycle management;
- shut down or enter a safe state if configuration/activation fails;
- integrate through an E-Trike vehicle launch package under the project vehicle-launch structure.

## Parameter updates

Retain and validate:

- `wheel_base`;
- `max_speed_forward`;
- `max_speed_reverse`;
- `max_steering_angle`;
- `max_brake_pressure_kpa`;
- `max_deceleration`;
- `low_speed_threshold`;
- `loop_rate`;
- `command_timeout_ms`;
- `heartbeat_interval_ms`;
- `rt_heartbeat_timeout_ms`;
- `can_interface`.

Add or consider:

- command and report frame IDs;
- steering and yaw sign multipliers;
- velocity sign convention;
- emergency latch/reset policy;
- startup mode policy;
- CAN receive-error threshold;
- diagnostic update rate;
- odometry publication enable flag;
- frame names if odometry is enabled.

Use the Universe parameter conventions and schema validation used by nearby packages where practical. Reject unsafe or inconsistent combinations at configure time.

## Concurrency and shutdown review

Preserve the separation between ROS callbacks and the CAN receive thread, but review all shared state:

- protect engage and requested-mode state consistently;
- ensure time objects shared across threads are protected or confined;
- stop and join the receive thread before destroying publishers, decoder, or CAN objects;
- ensure SocketCAN close unblocks receive promptly;
- prevent publishing through inactive lifecycle publishers;
- define behavior for CAN bus-off, read errors, write errors, and interface disappearance;
- never allow a failed send to look like a successful command application.

Use ROS time for message timestamps and steady time for watchdog intervals so simulation time changes cannot defeat safety timeouts.

## Safety behavior to preserve and strengthen

The port must preserve:

- command timeout safe stop;
- heartbeat generation and RT heartbeat monitoring;
- speed, steering, brake, and conversion saturation;
- immediate emergency-stop assertion;
- disengaged startup;
- physical mode gating;
- safe shutdown and CAN closure.

Additionally verify:

- startup sends no motion command before valid mode, engage, feedback, and a fresh control command;
- stale gear/light commands have defined behavior;
- reverse speed is limited separately;
- loss of heartbeat, CAN failure, or invalid feedback forces a safe state;
- re-engagement requires a fresh post-fault control command;
- diagnostics clearly distinguish command timeout, heartbeat timeout, E-stop, CAN I/O failure, and invalid frames.

## Tests required before vehicle use

Add unit tests for:

- speed conversion and saturation;
- steering/yaw conversion, including zero and near-zero speed;
- forward, neutral, reverse, park, and low gear mapping;
- braking conversion and saturation;
- lights and hazard priority;
- heartbeat counters and timeout boundaries;
- every CAN decoder and malformed ID/DLC/frame case;
- emergency true/false behavior;
- command timeout and re-enable behavior;
- Universe message-field mapping.

Add integration tests using `vcan` for:

- lifecycle configure/activate/deactivate/cleanup;
- expected CAN frames from ROS commands;
- expected ROS reports from CAN frames;
- CAN disconnect and reconnect behavior;
- heartbeat loss;
- emergency assertion;
- clean shutdown with a blocked receiver.

Before hardware motion testing:

1. Build only the package and dependencies with `colcon`.
2. Run lint and unit tests.
3. Run on Linux with a `vcan` interface.
4. Launch with Autoware and verify topic/service types and QoS.
5. Test on the physical CAN bus with propulsion disabled or wheels lifted.
6. Verify E-stop, timeout, heartbeat loss, and manual takeover before enabling autonomous motion.

## Suggested implementation sequence

1. Copy and rename the package without changing CAN behavior.
2. Package and link the generated E-Trike protocol correctly.
3. Replace message packages and port the `Control` command mapping.
4. Correct gear constants and all report mappings.
5. Convert control-mode input to the Universe service.
6. Fix emergency boolean handling and formalize the engagement state machine.
7. remove or replace `VehicleKinematicState` with optional standard odometry.
8. Convert hard-coded topics to private names plus launch remaps.
9. load parameters and manage lifecycle transitions from launch.
10. add unit tests and `vcan` integration tests.
11. integrate the package into the E-Trike vehicle launch package.
12. perform staged Linux/Jetson validation before vehicle motion.

## Definition of done

The migration is complete only when:

- no `autoware_auto_*` includes or dependencies remain;
- the bridge builds in the target Autoware Universe/ROS distribution;
- all commands and reports match current Universe types and QoS;
- control-mode requests use the expected service and confirmed feedback is reported;
- the protocol dependency is reproducible without relative repository include hacks;
- lifecycle activation and parameter loading occur automatically and safely;
- unit and `vcan` integration tests pass;
- emergency, timeout, heartbeat-loss, CAN-failure, and manual-takeover tests pass on the Jetson;
- the original bridge remains available for comparison until the Universe port is validated.
