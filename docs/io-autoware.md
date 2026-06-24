# Autoware.Auto Vehicle Interface I/O Documentation

The **Vehicle Interface** acts as the crucial bridge between the high-level autonomous driving stack (Autoware) and the vehicle's low-level Drive-by-Wire (DBW) system. It is responsible for translating high-level commands from Autoware into vehicle-specific signals and converting raw vehicle feedback into standard messages expected by Autoware.

This document details the standard input and output topics involved in this interface, primarily relying on `autoware_auto_msgs` and `autoware_vehicle_msgs`.

---

## 1. Outputs from Autoware to Low-Level (Commands)

These are the commands published by Autoware's planning and control stack. The low-level vehicle interface (or vehicle adapter) subscribes to these topics to actuate the vehicle.

| Topic Name | Message Type | Description |
| :--- | :--- | :--- |
| `/control/command/control_cmd` | `autoware_auto_control_msgs/AckermannControlCommand` | **Primary Motion Command (High-Level):** Consists of two sub-messages:<br>1. `AckermannLateralCommand`: Contains `steering_tire_angle` and `steering_tire_rotation_rate`.<br>2. `LongitudinalCommand`: Contains `speed`, `acceleration`, and `jerk`. |
| `/control/command/actuation_cmd` | `tier4_vehicle_msgs/ActuationCommandStamped` | **Actuation Command (Low-Level):** Direct input for vehicle hardware actuators. Contains `accel_cmd` (throttle pedal), `brake_cmd` (brake pedal), and `steer_cmd` (steering torque or angle). |
| `/control/command/gear_cmd` | `autoware_auto_vehicle_msgs/GearCommand` | **Gear Shift Request:** Command to shift gears (e.g., Park, Reverse, Neutral, Drive). |
| `/control/command/turn_indicators_cmd` | `autoware_auto_vehicle_msgs/TurnIndicatorsCommand` | **Turn Indicators:** Command to activate the left, right, or no turn indicators. |
| `/control/command/hazard_lights_cmd` | `autoware_auto_vehicle_msgs/HazardLightsCommand` | **Hazard Lights:** Command to activate or deactivate hazard flashing lights. |
| `/control/control_mode_request` | `autoware_auto_vehicle_msgs/ControlModeCommand` | **Mode Switch:** Request to transition the vehicle between Autonomous mode and Manual (human) driving mode. |
| `/control/command/emergency_cmd` | `tier4_vehicle_msgs/VehicleEmergencyStamped` | **Emergency Action:** Request to trigger an emergency stop or safe state immediately due to a detected critical failure. |

> [!TIP]
> **Adapting Commands:** Because vehicles require specific electrical signals (like throttle/brake pedal percentages or raw CAN messages), an intermediary **vehicle command adapter** (such as `raw_vehicle_cmd_converter`) is typically implemented to translate the generalized `AckermannControlCommand` into low-level `ActuationCommandStamped` messages. The DBW interface then converts these actuation commands into CAN/hardware signals.

---

## 2. Inputs to Autoware from Low-Level (Feedback & Status)

These topics are published by the vehicle's low-level interface to provide Autoware with the current physical state, ensuring that commands are being executed correctly and allowing closed-loop control.

| Topic Name | Message Type | Description |
| :--- | :--- | :--- |
| `/vehicle/status/kinematic_state` | `autoware_auto_vehicle_msgs/VehicleKinematicState` | **Odometry & Motion:** Reports the current vehicle pose (position and orientation) and its twist (linear and angular velocities) relative to the vehicle frame. |
| `/vehicle/status/steering_status` | `autoware_auto_vehicle_msgs/SteeringReport` | **Steering Feedback:** The current actual angle of the steering tires. |
| `/vehicle/status/velocity_status` | `autoware_auto_vehicle_msgs/VelocityReport` | **Velocity Feedback:** The current measured longitudinal velocity of the vehicle. |
| `/vehicle/status/control_mode` | `autoware_auto_vehicle_msgs/ControlModeReport` | **Current Control Mode:** Confirms whether the vehicle is currently operating in Autonomous, Manual, or a transitional mode. |
| `/vehicle/status/gear_status` | `autoware_auto_vehicle_msgs/GearReport` | **Current Gear:** Confirms the gear that the vehicle's transmission is currently engaged in. |
| `/vehicle/status/turn_indicators_status` | `autoware_auto_vehicle_msgs/TurnIndicatorsReport` | **Turn Indicators State:** Confirms the active state of the turn indicators. |
| `/vehicle/status/hazard_lights_status` | `autoware_auto_vehicle_msgs/HazardLightsReport` | **Hazard Lights State:** Confirms whether the hazard lights are currently active. |

> [!IMPORTANT]
> **Safety and Fallbacks:** The low-level interface is expected to monitor the connection to Autoware. If messages on `/control/command/control_cmd` time out, the low-level system should automatically trigger a safe stop and transition the vehicle back to manual mode.
