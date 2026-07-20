# Autoware Communication Architecture & Interfaces (Tabular Reference)

This document contains a structured reference of the communication interfaces, CAN commands, internal states, and serialization protocols for the Autoware vehicle interface stack.

---

## 1. ROS 2 Topic Interfaces (C++ Lifecycle Node)

> **Source Location:** `vehicle_interface/include/vehicle_interface/vehicle_interface_node.hpp`

### A. Subscriptions (Inputs to `vehicle_interface`)
| Topic Name | ROS 2 Message Type | Key Member Fields | Internal Target Variable | Description |
|:---|:---|:---|:---|:---|
| `~/input/control_cmd` | `autoware_control_msgs/msg/Control` | `lateral.steering_tire_angle` (float)<br>`lateral.steering_tire_rotation_rate` (float)<br>`longitudinal.speed` (float)<br>`longitudinal.acceleration` (float) | `control_cmd_ptr_` | Planning targets from the trajectory follower pipeline |
| `~/input/actuation_cmd` | `tier4_vehicle_msgs/msg/ActuationCommandStamped` | `actuation.accel_cmd` (float)<br>`actuation.brake_cmd` (float)<br>`actuation.steer_cmd` (float) | `actuation_cmd_ptr_` | Normalized pedal commands from `raw_vehicle_cmd_converter` |
| `~/input/gear_cmd` | `autoware_vehicle_msgs/msg/GearCommand` | `command` (uint8) | `gear_cmd_ptr_` | Request gear shifts (e.g. Drive, Reverse, Park) |
| `~/input/turn_indicators_cmd` | `autoware_vehicle_msgs/msg/TurnIndicatorsCommand` | `command` (uint8) | `turn_indicators_cmd_ptr_` | Request turn signal indicators activation |
| `~/input/hazard_lights_cmd` | `autoware_vehicle_msgs/msg/HazardLightsCommand` | `command` (uint8) | `hazard_lights_cmd_ptr_` | Request hazard lights activation |
| `~/input/emergency_cmd` | `tier4_vehicle_msgs/msg/VehicleEmergencyStamped` | `emergency` (bool) | `emergency_cmd_ptr_` | Triggers immediate safety stops and system disengagement |
| `~/input/from_can_bus` | `can_msgs::msg::Frame` | `id` (uint32)<br>`data` (uint8[8])<br>`dlc` (uint8) | N/A (Callback trigger) | Raw incoming frames received via SocketCAN |

---

### B. Publications (Outputs from `vehicle_interface`)
| Topic Name | ROS 2 Message Type | Key Member Fields | Internal Source Variable | Description |
|:---|:---|:---|:---|:---|
| `~/output/to_can_bus` | `can_msgs::msg::Frame` | `id` (uint32)<br>`data` (uint8[8])<br>`dlc` (uint8) | N/A (Encoded inline) | Outgoing CAN frames dispatched to SocketCAN |
| `~/output/velocity_status` | `autoware_vehicle_msgs/msg/VelocityReport` | `longitudinal_velocity` (float)<br>`lateral_velocity` (float)<br>`heading_rate` (float) | `speed_status_` | Speed and yaw feedback for localized Odometry |
| `~/output/steering_status` | `autoware_vehicle_msgs/msg/SteeringReport` | `steering_tire_angle` (float) | `steering_status_.actual_angle` | Confirms current tire direction angle |
| `~/output/gear_status` | `autoware_vehicle_msgs/msg/GearReport` | `report` (uint8) | `current_gear_` | Active transmission gear status |
| `~/output/control_mode` | `autoware_vehicle_msgs/msg/ControlModeReport` | `mode` (uint8) | `is_engaged_` | Reports active mode (`1 = Autonomous`, `4 = Manual`) |
| `~/output/turn_indicators_status`| `autoware_vehicle_msgs/msg/TurnIndicatorsReport` | `report` (uint8) | N/A (Stubbed/Read from CAN) | Confirms physical turn signal status |
| `~/output/hazard_lights_status` | `autoware_vehicle_msgs/msg/HazardLightsReport` | `report` (uint8) | N/A (Stubbed/Read from CAN) | Confirms physical hazard lights status |
| `~/output/actuation_status` | `tier4_vehicle_msgs/msg/ActuationStatusStamped` | `status.accel_status` (float)<br>`status.brake_status` (float)<br>`status.steer_status` (float) | `throttle_status_`<br>`brake_status_`<br>`steering_status_` | Feedback of actual pedal strokes |

---

## 2. Default CAN Bus Protocol Specifications

> **Source Location:** `vehicle_interface/include/vehicle_interface/can_protocol.hpp`

### A. Commands (Tier 2 Node $\rightarrow$ Tier 3 Actuation ECUs)
| CAN ID | Signal Name | Bytes Used | Data Type | Encoding Formula / Values |
|:---|:---|:---|:---|:---|
| **`0x200`** | Steering target angle | `0..1` | `int16_t` | $\text{Raw} = (\delta_{\text{tire\_rad}} + 3.14159) / 0.001$ |
| | Steering target rate | `2..3` | `int16_t` | $\text{Raw} = (\dot{\delta}_{\text{rad\_s}} + 10.0) / 0.01$ |
| | Steering enable flag | `4` | `uint8_t` | `0x01` = Active control, `0x00` = Manual bypass |
| | Unused | `5..7` | `uint8_t` | `0x00` (Reserved) |
| **`0x201`** | Brake target pressure | `0..1` | `uint16_t` | $\text{Raw} = P_{\text{brake\_ratio}} \times 10000.0$ (Range: $0.0 - 1.0$) |
| | Brake enable flag | `2` | `uint8_t` | `0x01` = Active control, `0x00` = Manual bypass |
| | Unused | `3..7` | `uint8_t` | `0x00` |
| **`0x202`** | Throttle target position | `0..1` | `uint16_t` | $\text{Raw} = T_{\text{accel\_ratio}} \times 10000.0$ (Range: $0.0 - 1.0$) |
| | Throttle enable flag | `2` | `uint8_t` | `0x01` = Active control, `0x00` = Manual bypass |
| | Unused | `3..7` | `uint8_t` | `0x00` |
| **`0x203`** | Gear selection | `0` | `uint8_t` | `0` = NONE, `1` = NEUTRAL, `2` = DRIVE, `3` = LOW, `10` = REVERSE, `20` = PARK |
| | Unused | `1..7` | `uint8_t` | `0x00` |
| **`0x204`** | Turn Signal command | `0` | `uint8_t` | `0` = NONE, `1` = LEFT, `2` = RIGHT, `3` = HAZARD |
| | Unused | `1..7` | `uint8_t` | `0x00` |
| **`0x206`** | System autonomous command | `0` | `uint8_t` | `0x01` = Enable override authority, `0x00` = Disable |
| | Clear override flag | `1` | `uint8_t` | `0x01` = Reset override blockages, `0x00` = Neutral |
| | Unused | `2..7` | `uint8_t` | `0x00` |

---

### B. Feedback Status (Tier 3 Actuation ECUs $\rightarrow$ Tier 2 Node)
| CAN ID | Signal Name | Bytes Used | Data Type | Decoding Formula / Values |
|:---|:---|:---|:---|:---|
| **`0x300`** | Actual steer angle | `0..1` | `int16_t` | $\theta_{\text{tire\_rad}} = (\text{Raw} \times 0.001) - 3.14159$ |
| | Actual steer rate | `2..3` | `int16_t` | $\dot{\theta}_{\text{rad\_s}} = (\text{Raw} \times 0.01) - 10.0$ |
| | Steering system fault | `4` | `uint8_t` | `0` = Nominal (OK), `1` = Fault, `2` = Auto-Initializing |
| | Unused | `5..7` | `uint8_t` | N/A |
| **`0x301`** | Actual brake pressure | `0..1` | `uint16_t` | $P_{\text{actual}} = \text{Raw} / 10000.0$ |
| | Brake system fault | `2` | `uint8_t` | `0` = Nominal, `1` = Hardware fault |
| | Unused | `3..7` | `uint8_t` | N/A |
| **`0x302`** | Actual throttle position | `0..1` | `uint16_t` | $T_{\text{actual}} = \text{Raw} / 10000.0$ |
| | Throttle system fault | `2` | `uint8_t` | `0` = Nominal, `1` = Hardware fault |
| | Unused | `3..7` | `uint8_t` | N/A |
| **`0x303`** | Actual gear | `0` | `uint8_t` | `0` = NONE, `1` = NEUTRAL, `2` = DRIVE, `3` = LOW, `10` = REVERSE, `20` = PARK |
| | Unused | `1..7` | `uint8_t` | N/A |
| **`0x320`** | Longitudinal Speed | `0..1` | `int16_t` | $v = \text{Raw} \times 0.01$ [m/s] |
| | Yaw rate (Heading rate) | `2..3` | `int16_t` | $\omega = \text{Raw} \times 0.001$ [rad/s] |
| | Unused | `4..7` | `uint8_t` | N/A |
| **`0x340`** | Core System Status flags | `0` | `uint8_t` | Bit `0` (0x01): `autonomous_enabled`<br>Bit `1` (0x02): `override_active`<br>Bit `2` (0x04): `system_fault`<br>Bit `3` (0x08): `estop_active` |
| | Diagnostic Fault code | `1` | `uint8_t` | Actuator or microcontroller fault code value |
| | Unused | `2..7` | `uint8_t` | N/A |

---

## 3. Node Configuration Parameters (YAML Config)

> **Source Location:** `vehicle_interface/config/vehicle_interface.param.yaml`

Parameters configured inside the YAML map to the C++ struct `Params`:

| Parameter Name | Data Type | Default Value | Units / Range | Description |
|:---|:---|:---|:---|:---|
| `loop_rate` | `double` | `30.0` | Hz | Publish command frame execution frequency |
| `command_timeout_ms` | `int` | `1000` | Milliseconds | Triggers emergency stop on command loss |
| `max_throttle` | `double` | `0.4` | $0.0 - 1.0$ (Pedal ratio) | Upper throttle command clamp |
| `max_brake` | `double` | `0.8` | $0.0 - 1.0$ (Pedal ratio) | Upper brake pressure command clamp |
| `emergency_brake` | `double` | `0.7` | $0.0 - 1.0$ (Pedal ratio) | Brake pressure applied during failsafe events |
| `max_steering_angle` | `double` | `1.0` | Radians | Maximum allowable physical tire angle |
| `max_steering_rate` | `double` | `5.0` | Rad/sec | Steering rotation velocity limit |
| `steering_offset` | `double` | `0.0` | Radians | Calibration alignment adjustment |
| `vgr_coef_a` | `double` | `15.713` | Constant | Variable Gear Ratio: base offset coefficient |
| `vgr_coef_b` | `double` | `0.053` | Constant | Variable Gear Ratio: velocity scaling coefficient |
| `vgr_coef_c` | `double` | `0.042` | Constant | Variable Gear Ratio: angle scaling coefficient |
| `wheel_base` | `double` | `2.79` | Meters | Axle center-to-center wheelbase span |
| `wheel_radius` | `double` | `0.383` | Meters | Active rolling wheel radius |
| `margin_time_for_gear_change` | `double` | `2.0` | Seconds | Anti-gear-chattering shift lockout window |
| `base_frame_id` | `std::string`| `"base_link"`| N/A | TF kinematic frame ID of the vehicle |
| `can_interface` | `std::string`| `"can0"` | N/A | Destination physical socket name |
| `use_actuation_cmd` | `bool` | `true` | `true` / `false` | True: Use pedal mappings. False: Bypass to Control |
| `convert_steer_cmd` | `bool` | `true` | `true` / `false` | Enable VGR steering wheel angle conversion |

---

## 4. micro-ROS Internal Events (`EventsMicroAutowareHandle`)

> **Source Locations:**
> *   `vehicle_interface/microAutoware/src/microAutoware/microAutoware.h`
> *   `vehicle_interface/microAutoware/src/nucleo-H753ZI_microAutoware_Serial_HIL/Core/Inc/microAutoware.h` (STM32 Firmware Project Copy)

Event flags used within the FreeRTOS middleware tasks (`StartMicroAutoware` and `StartTaskControl`):

| Constant Name | Value (Binary) | Value (Hex) | Action Trigger / Target | Description |
|:---|:---|:---|:---|:---|
| `MA_TO_AUTOWARE_MODE_FLAG` | `0b0000000010` | `0x002` | Control Task $\rightarrow$ micro-ROS | Requests shift of current control execution to Autoware |
| `MA_TO_MANUAL_MODE_FLAG` | `0b0000000100` | `0x004` | Control Task $\rightarrow$ micro-ROS | Requests fallback shift to manual control (joystick) |
| `MA_TO_EMERGENCY_MODE_FLAG`| `0b0000001000` | `0x008` | Control Task $\rightarrow$ micro-ROS | Triggers fallback shift to Emergency Stop control status |
| `SYS_TO_AUTOWARE_MODE_FLAG`| `0b0000010000` | `0x010` | micro-ROS $\rightarrow$ Control Task | Acknowledges system state mode is now Autoware |
| `SYS_TO_MANUAL_MODE_FLAG` | `0b0000100000` | `0x020` | micro-ROS $\rightarrow$ Control Task | Acknowledges system state mode is now Manual |
| `SYS_TO_EMERGENCY_MODE_FLAG`| `0b0001000000`| `0x040` | micro-ROS $\rightarrow$ Control Task | Acknowledges system state mode is now Emergency |
| `VEHICLE_NEW_DATA_FLAG` | `0b0010000000` | `0x080` | Control Task $\rightarrow$ micro-ROS | Signifies new HIL/CARLA feedback parsed from UART |
| `AUTOWARE_NEW_DATA_FLAG` | `0b0100000000` | `0x100` | micro-ROS $\rightarrow$ Control Task | Signifies new control command message received |
| `MICRO_ROS_AGENT_ONLINE_FLAG`| `0b1000000000`| `0x200` | micro-ROS $\rightarrow$ Control Task | Set when handshake ping checks successfully with agent |

---

## 5. HIL Serial Communication Protocol (STM32 $\leftrightarrow$ CARLA)

> **Source Locations:**
> *   `vehicle_interface/microAutoware/src/HIL/taksControl.c` / `utils.c`
> *   `vehicle_interface/microAutoware/src/nucleo-H753ZI_microAutoware_Serial_HIL/Core/Src/taksControl.c` / `utils.c` (STM32 Firmware Project Copy)

### A. Command Message Packet Structure (STM32 $\rightarrow$ CARLA)
*Total Packet Frame: 30 Bytes*

| Byte Index | ASCII Tag Prefix | Data Payload Field | Byte Type | Description |
|:---|:---|:---|:---|:---|
| `0` | `#` (0x23) | Frame Start Tag | `char` | Marks start of transmission frame |
| `1` | `S` (0x53) | Steering Angle prefix | `char` | Identifies following float payload |
| `2..5` | N/A | `xSteeringAngle` | `float` (4 Bytes) | Target steering tire angle command [rad] |
| `6` | `W` (0x57) | Steering Velocity prefix | `char` | Identifies following float payload |
| `7..10` | N/A | `xSteeringVelocity`| `float` (4 Bytes) | Target steering angular rate [rad/s] |
| `11` | `V` (0x56) | Target Speed prefix | `char` | Identifies following float payload |
| `12..15`| N/A | `xSpeed` | `float` (4 Bytes) | Longitudinal target velocity [m/s] |
| `16` | `A` (0x41) | Target Accel prefix | `char` | Identifies following float payload |
| `17..20`| N/A | `xAcceleration` | `float` (4 Bytes) | Target acceleration command [m/s²] |
| `21` | `J` (0x4A) | Target Jerk prefix | `char` | Identifies following float payload |
| `22..25`| N/A | `xJerk` | `float` (4 Bytes) | Target jerk command [m/s³] |
| `26` | `M` (0x4D) | Control Mode prefix | `char` | Identifies following control mode byte |
| `27` | N/A | `ucControlMode` | `uint8_t` (1 Byte) | Current mode: `0 = Emergency`, `1 = Autoware`, `4 = Manual` |
| `28` | `$` (0x24) | Frame Stop Tag | `char` | Marks end of transmission frame |
| `29` | `\0` (0x00) | String Terminator | `char` | Null terminator character |

---

### B. Telemetry Message Packet Structure (CARLA $\rightarrow$ STM32)
*Total Packet Frame: 22 Bytes*

| Byte Index | ASCII Tag Prefix | Data Payload Field | Byte Type | Description |
|:---|:---|:---|:---|:---|
| `0` | `#` (0x23) | Frame Start Tag | `char` | Marks start of incoming telemetry frame |
| `1` | `A` (0x41) | Longitudinal Speed prefix| `char` | Identifies following speed float payload |
| `2..5` | N/A | `xLongSpeed` | `float` (4 Bytes) | Simulated longitudinal vehicle velocity [m/s] |
| `6` | `B` (0x42) | Lateral Speed prefix | `char` | Identifies following lateral speed float |
| `7..10` | N/A | `xLatSpeed` | `float` (4 Bytes) | Simulated lateral vehicle velocity [m/s] |
| `11` | `C` (0x43) | Yaw Rate prefix | `char` | Identifies following heading yaw rate float |
| `12..15`| N/A | `xHeadingRate` | `float` (4 Bytes) | Simulated yaw rate feedback [rad/s] |
| `16` | `D` (0x44) | Steering Angle feedback | `char` | Identifies steering wheel angle status float |
| `17..20`| N/A | `xSteeringStatus` | `float` (4 Bytes) | Simulated actual steering wheel angle [rad] |
| `21` | `$` (0x24) | Frame Stop Tag | `char` | Marks end of telemetry frame |

---

## 6. Safety Limits & Command Validation Rules

> **Source Locations:**
> *   `vehicle_interface/src/vehicle_interface_node.cpp` (C++ Node)
> *   `vehicle_interface/microAutoware/src/HIL/taksControl.c` (STM32 Firmware)
> *   `vehicle_interface/microAutoware/src/nucleo-H753ZI_microAutoware_Serial_HIL/Core/Src/taksControl.c` (STM32 Firmware Project Copy)

The interface layers enforce strict validation bounds, watchdogs, and override logic to guarantee system stability and passenger safety:

| Rule Category | Applied Value / Range | Check Description | Action Taken on Violation | Source Node / Task |
|:---|:---|:---|:---|:---|
| **Throttle Clamping** | `[0.0, max_throttle]` (Default max: `0.4`) | Clamps input target throttle within mechanical bounds | Values exceeding are capped at `max_throttle` | `vehicle_interface` (C++) |
| **Brake Clamping** | `[0.0, max_brake]` (Default max: `0.8`) | Clamps input target brake within system pressure bounds | Values exceeding are capped at `max_brake` | `vehicle_interface` (C++) |
| **Steering Clamping** | `[-max_steering_angle, max_steering_angle]` (Default max: `1.0` rad) | Clamps input target tire angle to prevent physical lock limits | Angles exceeding are capped at limits | `vehicle_interface` (C++) |
| **Steering Rate Limit** | `max_steering_rate` (Default max: `5.0` rad/s) | Limits maximum target steering rotation rate | Transmitted as target rate field in CAN command; actual rate-limiting is handled by Tier 3 ECU | `vehicle_interface` (C++) |
| **Gear Anti-Chatter** | `margin_time_for_gear_change` (Default: `2.0` sec) | Evaluates time elapsed since last gear state switch | Gear requests are ignored if time delta is less | `vehicle_interface` (C++) |
| **Command Timeout** | `command_timeout_ms` (Default: `1000` ms) | Checks time elapsed since last incoming ROS 2 command | Applies emergency brake (`emergency_brake`, default `0.7`) and zero throttle | `vehicle_interface` (C++) |
| **Manual Override** | `override_active` flag (`0x340` CAN Status bit 1) | Monitors driver manual steering/pedal override inputs | Disengages autonomy (`is_engaged_` = false) and turns off CAN enable flags | `vehicle_interface` (C++) |
| **System Fault** | `system_fault` flag (`0x340` CAN Status bit 2) | Monitors hardware faults flagged by actuator ECUs | Disengages autonomy immediately | `vehicle_interface` (C++) |
| **HIL Autoware Command Timeout** | `TIMEOUT_GET_CONTROL_ACTION` (`110` ms) | Monitors incoming time difference between micro-ROS commands | Increments data loss; triggers MANUAL mode fallback after `10` consecutive drops | `microAutoware` (STM32 task) |
| **HIL Telemetry Status Timeout** | `TIMEOUT_GET_CARLA_RX` (`100` ms) | Monitors time difference between serial CARLA feedback frames | Increments loss; triggers EMERGENCY stop mode after `10` consecutive drops | `microAutoware` (STM32 task) |
| **Joystick Debounce** | `DEBOUNCE_TICKS` (`1000` ticks) | Avoids chatter when pressing the physical JoySW button | Ignores transition signals sent within this interval | `microAutoware` (STM32 task) |

