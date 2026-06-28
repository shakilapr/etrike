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

### 3.2 Topic Namespace Gap

All E-Trike topics use `~/input/*` and `~/output/*` (node-private namespace) instead of the standard Autoware.Auto global names `/control/command/*` and `/vehicle/status/*`.

**Impact:** An unmodified Autoware.Auto planning stack publishing to `/control/command/control_cmd` will NOT reach the E-Trike bridge subscribing to `~/input/control_cmd`. A **topic remap** is required at launch:

```yaml
# In autoware_vehicle_bridge launch file
remappings:
  - from: "~/input/control_cmd"
    to: "/control/command/control_cmd"
  - from: "~/input/gear_cmd"
    to: "/control/command/gear_cmd"
  # ... etc for all 7 subscriptions and 7 publications
```

Alternatively, the bridge could be reconfigured to subscribe/publish on the standard global topics directly (changing `~/input/*` → `/control/command/*` in the C++ source).

### 3.3 CAN Protocol Compatibility

| Aspect | Status | Detail |
|:---|:---|:---|
| Command encoding | ✅ Compatible | `0x300` speed + yaw + gear matches RT kinematics expectations (big-endian, physical units) |
| Brake encoding | ✅ Compatible | `0x301` deceleration→kPa via `max_deceleration` parameter (5 m/s² → 5000 kPa) |
| Light encoding | ✅ Compatible | `0x302` bitfield: L=0x01, R=0x02, hazard=0x03, brake=0x04 |
| ESTOP encoding | ✅ Compatible | `0x001` DLC=0, rate-limited 500ms |
| Heartbeat encoding | ✅ Compatible | `0x7FC` DLC=1, `alive_ctr++` at 2 Hz |
| Velocity decoding | ✅ Compatible | `0x120` i16 mm/s → float m/s |
| Steering decoding | ⚠️ Offset risk | `0x310` STEER_DIAG: code uses `offset=-3000` (30.0°). steer-by-wire unit CSV spec may differ. Pre-existing issue B1 in v0.0.4 audit. |
| Gear decoding | ✅ Compatible | `0x206` gear_state byte: CAN 0/1/2/3 → Autoware NONE/DRIVE/LOW/REVERSE |
| Mode decoding | ✅ Compatible | `0x210` mode byte: 0→MANUAL(4), 1→AUTONOMOUS(1), 2→DISENGAGED(5) |
| Light feedback | ⚠️ Echo only | `0x011` light_state is SYS→RT→Jetson forwarded. No independent sensor confirms relay state. Open-loop echo until SYS adds dedicated light status bits. |
| RT heartbeat | ✅ Compatible | `0x7FD` alive counter, 1500ms timeout |
| CAN gateway forwarding | ✅ Compatible | RT forwards `0x011`, `0x120`, `0x206`, `0x600` low→high unchanged |

### 3.4 Low-Level Architecture Compatibility

| System Aspect | Standard Expectation | E-Trike Implementation | Compatible? |
|:---|:---|:---|:---|
| **Command timeout** | 1000ms (configurable) | RT: 500ms staleness watchdog (§7.6). Bridge: 500ms `command_timeout_ms`. Both independent. | ✅ Exceeds standard |
| **Emergency stop** | `/control/command/emergency_cmd` → immediate safe state | `0x001` ESTOP via CAN, rate-limited 500ms. RT → ramp/hold steering, zero speed, max brake. | ✅ Compatible |
| **Mode switching** | `ControlModeCommand` AUTONOMOUS/MANUAL | Bridge `engaged_` gates TX. Physical mode gated by SYS MODE button + `0x110` CAN. | ✅ Two-layer: logical + physical |
| **Gear selection** | `GearCommand` NONE/DRIVE/LOW/REVERSE/PARK | `0x300` gear byte → RT → `0x204` → MTR relays. PARK not supported (no mechanical parking pawl). | ⚠️ PARK unsupported |
| **Steering actuation** | CAN command → steering ECU | `0x169` VCU_SES_REQ → steer-by-wire unit (Angle Mode, 50 Hz, rolling counter + checksum) | ✅ Proprietary but validated |
| **Brake actuation** | CAN command → brake ECU | `0x7B9` VCU_SEB_REQ → brake-by-wire unit (Pressure/Stroke Mode, 50 Hz, rolling counter + checksum). RT→`0x205`→SYS→`0x7B9` (AUTO) or SYS→`0x7B9` (MANUAL/ESTOP). | ✅ Mode-gated dual sender |
| **Throttle actuation** | Analog/CAN → motor controller | MCP4725 DAC 0–5V via MTR STM32 (open-loop). No PID until rear encoder fitted (gap #5). | ⚠️ Open-loop only |
| **Safety architecture** | EGAS / ISO 26262 | EGAS 3-level: MTR L1 (STM32), SYS L2 (ESP32-S3), hardware ESTOP L3. TPS3850 external watchdog on both RT and SYS. | ✅ ASIL-C decomposition |
| **Heartbeat/liveness** | Per-node alive counter | `0x7FD` (RT), `0x7FE` (SYS, 10 Hz), `0x7FC` (Jetson, 2 Hz). RT: SYS timeout 200ms, Jetson timeout 1500ms. SYS: RT timeout 1000ms. | ✅ Automotive-grade |

### 3.5 Data Flow Latency

| Path | Hops | Typical Latency | Notes |
|:---|:---|:---|:---|
| AckermannControlCommand → motor speed | 2 CAN frames | ~10 ms | Jetson→`0x300`→RT→`0x204`→MTR/MCP4725 (both at 100 Hz) |
| AckermannControlCommand → steering angle | 2 CAN frames | ~20 ms | Jetson→`0x300`→RT→kinematics→`0x169`→EPS-C (50 Hz steering loop) |
| AckermannControlCommand → brake pressure | 3 CAN frames | ~30 ms | Jetson→`0x301`→RT→max-select→`0x205`→SYS→`0x7B9`→SEB (RT→SYS at 50 Hz, SYS→SEB at 50 Hz) |
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

### Topic namespace (GAP-1)
**All 7 subscriptions and 7 publications** use `~/input/*` / `~/output/*` instead of standard Autoware.Auto global topic names. Requires launch-file remapping or source-code change.

### Steering angle offset (GAP-2)
`0x310` STEER_DIAG decoding uses `offset=-3000` (30.0°). The steer-by-wire unit CSV spec must be verified. A wrong offset produces a constant steering angle bias in `SteeringReport` and dead-reckoning odometry. **Pre-existing issue B1** from v0.0.4 audit.

### Steering decode split path (GAP-3)
`CanDecoder::decode_steering()` returns `steering_tire_angle = 0.0f` (stub). Actual steering decoding happens inline in `publish_vehicle_reports()` case `0x310`. The `~/output/steering_status` publisher appears to use the stub path, while the odometry code uses the inline path. Verify which code path feeds `pub_steering_`.

### Light feedback open-loop (GAP-4)
`TurnIndicatorsReport` and `HazardLightsReport` are echoed from the same `0x011` light_state that SYS sets based on the incoming `0x302` command. No independent sensor confirms the relay actually energized. A failed relay or GPIO would report "active" while the lamp is dark.

### PARK gear unsupported (GAP-5)
`GearCommand::PARK` (22) maps to CAN gear N (0). The trike has no mechanical parking pawl. Autoware.Auto planning must be configured to never request PARK — or the bridge must translate PARK→N and log a warning.

### Dead-reckoning drift (GAP-6)
`VehicleKinematicState` uses pure integration of speed + steer angle. Drifts unboundedly without GNSS, IMU, or wheel encoder fusion. Acceptable for short-duration planning; not suitable for localization. Full odometry deferred to gap #5 (rear motor encoder).

### Open-loop throttle (GAP-7)
Motor speed control is open-loop (fixed voltage mapping). No PID compensation for hills, headwinds, or load changes. `VelocityReport` may show speed diverging from `AckermannControlCommand.longitudinal.speed` target under load. PID closure deferred to gap #5.

### Brake latency (GAP-8)
AUTO brake path: Jetson→`0x301`→RT→`0x205`→SYS→`0x7B9`→SEB is **3 CAN hops**. Architecture Option D (RT→`0x7B9` direct in AUTO) would reduce this to 2 hops but is not yet implemented (gap #12).

### 0x7B9 dual-sender gap (GAP-9)
Per architecture §6.2 Option D, RT should send `0x7B9` directly in AUTO mode (1-hop from kinematics). Currently SYS is the sole `0x7B9` sender in all modes. RT sends `0x205` to SYS, which converts to SEB protocol. **Pre-existing gap #12** — planned, not implemented.

### `0x011` light_state DLC dependency (GAP-10)
Light state feedback is only published when `0x011` DLC ≥ 3. The architecture specifies `u4 light_state` in byte 2, but SYS may not populate it in all firmware versions. The bridge code guards with `if (frame.len >= 3)` — if SYS sends DLC=2, light feedback silently stops.
