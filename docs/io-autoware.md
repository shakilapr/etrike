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

---

## 3. E-Trike Compatibility Analysis

This section cross-references the standard Autoware.Auto interface (§1–2) against the E-Trike `autoware_vehicle_bridge` implementation and low-level CAN architecture.

### 3.1 Topic Compatibility Matrix

#### Commands (Autoware → Vehicle)

| Standard Topic (§1) | Standard Type | E-Trike Topic | E-Trike Type | CAN Mapping | Status |
|:---|:---|:---|:---|:---|:---|
| `/control/command/control_cmd` | `AckermannControlCommand` | `~/input/control_cmd` | `AckermannControlCommand` ✅ | `0x300` HOST_DRIVE_CMD: `speed`→mmps, `steering_tire_angle`→yaw via inverse kinematics, `acceleration`→brake kPa | **Type match; topic namespace mismatch** |
| `/control/command/actuation_cmd` | `ActuationCommandStamped` | *(not subscribed)* | — | — | **Not implemented** — tier4 type, not core Autoware.Auto; removed per v0.0.4 audit |
| `/control/command/gear_cmd` | `GearCommand` | `~/input/gear_cmd` | `GearCommand` ✅ | `0x300` gear byte (DLC=8 byte 7): NONE→N(0), DRIVE→D(1), LOW→S(2), REVERSE→R(3) | **Type match; topic namespace mismatch** |
| `/control/command/turn_indicators_cmd` | `TurnIndicatorsCommand` | `~/input/turn_indicators_cmd` | `TurnIndicatorsCommand` ✅ | `0x302` HOST_LIGHT_CMD bit 0 (left), bit 1 (right) | **Type match; topic namespace mismatch** |
| `/control/command/hazard_lights_cmd` | `HazardLightsCommand` | `~/input/hazard_lights_cmd` | `HazardLightsCommand` ✅ | `0x302` HOST_LIGHT_CMD — both bits set (0x03) | **Type match; topic namespace mismatch** |
| `/control/control_mode_request` | `ControlModeCommand` | `~/input/control_mode` | `ControlModeCommand` ✅ | Internal `engaged_` state — gates `0x300`/`0x301` TX. Physical mode gated by SYS MODE button (`0x110`). | **Type match; topic namespace mismatch** |
| `/control/command/emergency_cmd` | `VehicleEmergencyStamped` | `~/input/emergency_cmd` | `VehicleEmergencyStamped` ✅ | `0x001` SAFETY_ESTOP DLC=0, rate-limited 500ms | **Type match; topic namespace mismatch** |
| *(not in standard)* | — | `~/input/engage` | `Engage` | Internal `engaged_` state — `false` suppresses all commands | **Extra — Autoware.Auto Engage topic, used as alternative to ControlModeCommand** |

#### Feedback (Vehicle → Autoware)

| Standard Topic (§2) | Standard Type | E-Trike Topic | E-Trike Type | CAN Source | Status |
|:---|:---|:---|:---|:---|:---|
| `/vehicle/status/velocity_status` | `VelocityReport` | `~/output/velocity_status` | `VelocityReport` ✅ | `0x120` SYS_THROTTLE_STS — `{i16 speed_mmps}` → m/s. `lateral_velocity`=0, `heading_rate`=0 (no sensor). | **Type match; topic namespace mismatch** |
| `/vehicle/status/steering_status` | `SteeringReport` | `~/output/steering_status` | `SteeringReport` ✅ | `0x310` STEER_DIAG — `{i16 angle, u8 fault, …}`. Angle: offset=-3000, 0.1°/bit → radians. **Conflated path:** `decode_steering()` stub returns 0.0; actual value via inline `publish_vehicle_reports()` 0x310 case. | **Type match; topic namespace mismatch; code path split** |
| `/vehicle/status/control_mode` | `ControlModeReport` | `~/output/control_mode` | `ControlModeReport` ✅ | `0x210` RT_STATE_RPT `{u8 mode}` + `0x011` SYS_SAFETY_STS `{u8 estop}`. Trike mode 0→MANUAL(4), 1→AUTONOMOUS(1), estop→DISENGAGED(5). | **Type match; topic namespace mismatch** |
| `/vehicle/status/gear_status` | `GearReport` | `~/output/gear_status` | `GearReport` ✅ | Primary: `0x206` MTR_MOTOR_FBK byte 2 `{u8 gear_state}` — CAN gear→Autoware enum. Fallback: `0x210` reversing flag. | **Type match; topic namespace mismatch** |
| `/vehicle/status/turn_indicators_status` | `TurnIndicatorsReport` | `~/output/turn_indicators_status` | `TurnIndicatorsReport` ✅ | `0x011` SYS_SAFETY_STS byte 2 `{u4 light_state}` — bit 0=left, bit 1=right, both=hazard. **Open-loop echo fallback** — SYS light state echoed back to host; actual relay state not independently verified. | **Type match; topic namespace mismatch** |
| `/vehicle/status/hazard_lights_status` | `HazardLightsReport` | `~/output/hazard_lights_status` | `HazardLightsReport` ✅ | `0x011` byte 2 — both bits set → ENABLE, else DISABLE. Same open-loop echo limitation as turn indicators. | **Type match; topic namespace mismatch** |
| `/vehicle/status/kinematic_state` | `VehicleKinematicState` | `~/output/kinematic_state` | `VehicleKinematicState` ✅ | Dead reckoning from `0x120` speed + `0x310` steer angle. **Drifts without absolute reference.** Full encoder+IMU odometry deferred to gap #5. | **Type match; topic namespace mismatch** |
| *(not in standard)* | — | `~/output/diagnostics` | `DiagnosticArray` | `0x600` SYS_DIAG_RPT + local state (CAN status, engage, RT/SYS heartbeats, ESTOP) | **Extra — standard ROS diagnostics** |

### 3.2 CAN Protocol Compatibility

| Aspect | Status | Detail |
|:---|:---|:---|
| Command encoding | ✅ Compatible | `0x300` speed + yaw + gear: i32 BE speed, i24 BE yaw, u8 gear. RT kinematics validated. |
| Brake encoding | ✅ Compatible | `0x301` deceleration→kPa. RT arbitrates max(obstacle, host). Option D: RT→SEB direct. |
| Light encoding | ✅ Compatible | `0x302` bitfield: L=0x01, R=0x02, hazard=0x03, brake=0x04. RT forwards transparently. |
| ESTOP encoding | ✅ Compatible | `0x001` DLC=0, rate-limited 250ms. RT forwards bidirectionally. TXB2 priority on MCP2515. |
| Heartbeat encoding | ✅ Compatible | `0x7FC` DLC=2 (alive_ctr + health byte). Host timeout 1500ms → assisted stop. |
| Velocity decoding | ✅ Compatible | `0x120` i16 mm/s → float m/s. Forwarded low→high by RT at 100 Hz. |
| Steering decoding | ✅ Fixed | `0x310` STEER_DIAG: unsigned read, factor 0.1, offset -3000. Raw 30000→0°. Verified against CSV. |
| Gear decoding | ✅ Compatible | `0x206` gear_state byte: CAN 0/1/2/3 → Autoware NONE/DRIVE/LOW/REVERSE. |
| Mode decoding | ✅ Compatible | `0x210` mode byte + `safety_state` byte. SYS authoritative via `0x110`. RT reports via `0x210`. |
| Light feedback | ⚠️ Echo only | `0x011` light_state echoes `0x302` command bits. No independent relay sensor. |
| RT heartbeat | ✅ Compatible | `0x7FD` DLC=2 (alive_ctr + health byte). Sent on BOTH buses, independent counters. |
| CAN gateway forwarding | ✅ Compatible | RT forwards `0x011`, `0x120`, `0x206`, `0x600` low→high. ESTOP uses send-to-front. |

### 3.4 Low-Level Architecture Compatibility

| System Aspect | Standard Expectation | E-Trike Implementation | Compatible? |
|:---|:---|:---|:---|
| **Command timeout** | 1000ms (configurable) | RT: 500ms staleness watchdog (§7.6). Bridge: 500ms `command_timeout_ms`. Both independent. | ✅ Exceeds standard |
| **Emergency stop** | `/control/command/emergency_cmd` → immediate safe state | `0x001` ESTOP via CAN, rate-limited 500ms. RT → ramp/hold steering, zero speed, max brake. | ✅ Compatible |
| **Mode switching** | `ControlModeCommand` AUTONOMOUS/MANUAL | Bridge `engaged_` gates TX. Physical mode gated by SYS MODE button + `0x110` CAN. | ✅ Two-layer: logical + physical |
| **Gear selection** | `GearCommand` NONE/DRIVE/LOW/REVERSE/PARK | `0x300` gear byte → RT → `0x204` → MTR relays. PARK not supported (no mechanical parking pawl). | ⚠️ PARK unsupported |
| **Steering actuation** | CAN command → steering ECU | `0x169` VCU_SES_REQ → steer-by-wire unit (Angle Mode, 50 Hz, rolling counter + checksum) | ✅ Proprietary but validated |
| **Brake actuation** | CAN command → brake ECU | `0x7B9` VCU_SEB_REQ (Pressure/Stroke, 50 Hz). RT sends directly in AUTO (Option D). SYS sends in MANUAL/ESTOP. SYS reads RT safety_state from 0x210 to avoid dual-send. | ✅ Mode-gated dual sender |
| **Throttle actuation** | Analog/CAN → motor controller | MCP4725 DAC 0–5V via MTR STM32 (open-loop). Motor I/O being migrated to MTR (SYS_OWNS_MOTOR flag). | ⚠️ Open-loop, migration in progress |
| **Safety architecture** | EGAS / ISO 26262 | EGAS 3-level: MTR L1 (STM32), SYS L2 (ESP32-S3), hardware ESTOP L3. RT+SYS per-task WDT. MCP2515 error interrupts. | ✅ ASIL-C decomposition |
| **Heartbeat/liveness** | Per-node alive counter | `0x7FD` (RT, DLC=2), `0x7FE` (SYS, 10 Hz), `0x7FC` (Host, DLC=2). Health byte on RT+Host. Frozen-counter detection. | ✅ Automotive-grade |

### 3.5 Data Flow Latency

| Path | Hops | Typical Latency | Notes |
|:---|:---|:---|:---|
| AckermannControlCommand → motor speed | 2 CAN frames | ~10 ms | Jetson→`0x300`→RT→`0x204`→MTR/MCP4725 (both at 100 Hz) |
| AckermannControlCommand → steering angle | 2 CAN frames | ~20 ms | Jetson→`0x300`→RT→kinematics→`0x169`→EPS-C (50 Hz steering loop) |
| AckermannControlCommand → brake pressure | 2 CAN frames | ~20 ms | Jetson→`0x301`→RT→`0x7B9`→SEB (Option D: RT sends directly in AUTO) |
| Speed feedback → VelocityReport | 2 CAN frames | ~10 ms | MTR→`0x120`→RT(fwd)→Jetson (100 Hz) |
| Steering feedback → SteeringReport | 2 CAN frames | ~20 ms | EPS-C→`0x201`→RT→`0x310`→Jetson (10 Hz steering diag) |
| Light command round-trip | 4 CAN frames | ~20–40 ms | Jetson→`0x302`→RT(fwd)→SYS→`0x011`→RT(fwd)→Jetson |

### 3.6 Type and Unit Conversions

| Conversion | From | To | Formula | Location |
|:---|:---|:---|:---|:---|
| Speed | `longitudinal.speed` (m/s) | `0x300` speed_mmps (i32) | `speed * 1000.0`, clamp [-500, 3000] | `CanEncoder::speed_to_mmps()` |
| Steer angle → Yaw rate | `steering_tire_angle` (rad) | `0x300` yaw_mrad_s (i24) | `abs(v) * tan(δ) / wheel_base * 1000.0`, clamp [-3000, 3000] | `CanEncoder::steering_to_yaw()` |
| Deceleration → Brake kPa | `acceleration` (m/s², negative) | `0x301` brake_pressure_kpa (i32) | `(-accel / max_deceleration) * max_brake_pressure_kpa`, clamp [0, 5000] | `CanEncoder::encode_brake()` |
| Speed feedback | `0x120` speed_mmps (i16) | `VelocityReport.longitudinal_velocity` (m/s) | `mmps / 1000.0` | `CanDecoder::decode_velocity()` |
| Steer angle feedback | `0x310` angle_raw (i16) | `SteeringReport.steering_tire_angle` (rad) | `(raw - 30000) * 0.1 * π/180` | `publish_vehicle_reports()` 0x310 case |
| Gear mapping | `GearCommand.command` | `0x300` gear byte | NONE→0, DRIVE→1, LOW→2, REVERSE→3, PARK→0 (unsupported) | `tick_control()` gear switch |
| Gear feedback | `0x206` gear_state | `GearReport.report` | CAN 0→NONE(0), 1→DRIVE(2), 2→LOW(23), 3→REVERSE(20) | `publish_vehicle_reports()` CAN_MOTOR_FBK case |
| Mode feedback | `0x210` mode byte | `ControlModeReport.mode` | Trike 0→MANUAL(4), 1→AUTONOMOUS(1), 2→DISENGAGED(5) | `CanDecoder::decode_state()` |
| kPa → SEB raw | `brake_pressure_kpa` (i32) | `0x7B9` pressure raw (u8) | `kpa * 0.02`, clamp to `kSebMaxPressureRaw` (100) | SYS §8.6 |

---

## 4. Gap Summary

### Topic namespace (GAP-1) — ✅ RESOLVED
Bridge now uses standard Autoware.Auto global topic names: `/control/command/*` (sub) and `/vehicle/status/*` (pub). No remapping needed. Two extras remain local: `~/input/engage` and `~/output/diagnostics` (not Autoware-standard topics).

### Steering angle offset (GAP-2) — ✅ RESOLVED
Offset verified correct: raw=30000→0°. CSV spec confirmed. Signed→unsigned fix applied in bridge (commit ab08472).

### Steering decode split path (GAP-3)
`CanDecoder::decode_steering()` returns `steering_tire_angle = 0.0f` (stub). Actual steering decoding happens inline in `publish_vehicle_reports()` case `0x310`. Needs code path consolidation in the bridge.

### Light feedback open-loop (GAP-4)
`TurnIndicatorsReport` and `HazardLightsReport` echoed from `0x011` light_state. No independent relay-state sensor. A failed relay would report "active" while lamp is dark.

### PARK gear unsupported (GAP-5)
`GearCommand::PARK` (22) maps to CAN gear N (0). No mechanical parking pawl. Planning must never request PARK.

### Dead-reckoning drift (GAP-6)
Pure integration of speed + steer angle. Drifts without GNSS/IMU/encoder fusion. Acceptable for short-duration planning.

### Open-loop throttle (GAP-7)
Motor speed control is open-loop (fixed voltage mapping). No PID compensation. PID closure deferred to rear encoder (gap #5).

### Brake latency (GAP-8) — ✅ RESOLVED
Option D implemented. RT sends `0x7B9` directly in AUTO mode. Path now: Jetson→`0x301`→RT→`0x7B9`→SEB (2 CAN hops, was 3).

### 0x7B9 dual-sender gap (GAP-9) — ✅ RESOLVED
RT sends `0x7B9` directly in AUTO (Option D). SYS reads RT `safety_state` from `0x210` and suppresses its own `0x7B9` only when RT is Normal. No collision.

### `0x011` light_state DLC dependency (GAP-10)
Light feedback only published when `0x011` DLC ≥ 3. SYS always sends DLC=3 (firmware verified). Bridge guards with `if (frame.len >= 3)`. Low risk.
