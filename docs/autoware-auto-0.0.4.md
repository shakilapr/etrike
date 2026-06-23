# Autoware.Auto Integration — E-Trike v0.0.4-alpha

**Supersedes:** All previous 0.0.4 documents
**Base document:** `docs/autoware-auto-communication-architecture.md`
**Audit basis:** 12 agents across 4 rounds
**Date:** 2026-06-23

---

## Optimal Architecture: Minimum Change, Maximum Compatibility

The current E-Trike CAN protocol works. RT does kinematics. Safety is verified. The only thing missing for Autoware.Auto compatibility is the ROS 2 topic interface on the Jetson.

**What changes:** Jetson gets a new ROS node (`autoware_vehicle_bridge`). The CAN bus stays exactly as it is.

```
AckermannControlCommand (ROS)
        │
        ▼
┌─────────────────────────┐
│ autoware_vehicle_bridge │  ← NEW (Jetson)
│  ROS → CAN: 0x300       │
│  CAN → ROS: reports     │
└───────────┬─────────────┘
            │ 0x300 {speed, yaw, gear}  ← UNCHANGED
    High CAN│ 500 kbit/s
            ▼
┌─────── RT ESP32-S3 ─────┐  ← UNCHANGED
│  kinematics, gateway,    │
│  steering, safety        │
└───────────┬─────────────┘
    Low CAN │ 500 kbit/s
            ▼
    EPS-C + SEB + SYS + MTR  ← UNCHANGED
```

Everything below the red line stays identical to today. The only new code is the Jetson ROS node.

---

## 1. ROS 2 Topic Interface (Jetson Node)

### 1.1 Subscriptions (Inputs)

| Topic | Message Type | Maps To | Notes |
|:---|:---|:---|:---|
| `~/input/control_cmd` | `autoware_auto_control_msgs/msg/AckermannControlCommand` | High CAN `0x300` | `longitudinal.speed` → speed_mmps. `lateral.steering_tire_angle` → yaw via inverse tricycle kinematics. `longitudinal.acceleration` → brake kPa via deceleration model. |
| `~/input/gear_cmd` | `autoware_auto_vehicle_msgs/msg/GearCommand` | `0x300` gear field | Override auto-derived gear |
| `~/input/turn_indicators_cmd` | `autoware_auto_vehicle_msgs/msg/TurnIndicatorsCommand` | `0x302` turn bits | Forwarded by RT to SYS |
| `~/input/hazard_lights_cmd` | `autoware_auto_vehicle_msgs/msg/HazardLightsCommand` | `0x302` both turn bits | |
| `~/input/engage` | `autoware_auto_vehicle_msgs/msg/Engage` | Internal state | `false` → stop publishing commands |

### 1.2 Publications (Outputs)

| Topic | Message Type | Source CAN | Notes |
|:---|:---|:---|:---|
| `~/output/velocity_status` | `autoware_auto_vehicle_msgs/msg/VelocityReport` | `0x120` (speed) | `longitudinal_velocity` = mmps/1000. `heading_rate` = 0 (no sensor). `lateral_velocity` = 0. |
| `~/output/steering_status` | `autoware_auto_vehicle_msgs/msg/SteeringReport` | *(via `0x201` SES_STATUS on low bus — RT forwards as `0x210` steering validity)* | `steering_tire_angle` from RT state report or derived from `0x300` echo |
| `~/output/gear_status` | `autoware_auto_vehicle_msgs/msg/GearReport` | `0x210` (mode+steer+reversing) | RT derives gear from speed direction |
| `~/output/control_mode` | `autoware_auto_vehicle_msgs/msg/ControlModeReport` | `0x210` mode field + `0x011` estop | Manual→MANUAL(4), Auto+engaged→AUTONOMOUS(1), Estop→DISENGAGED(5) |

### 1.3 What Gets Fixed in the Original Document

| Original | Corrected | Reason |
|:---|:---|:---|
| `autoware_control_msgs` | `autoware_auto_control_msgs` | Missing `.auto` prefix |
| `autoware_vehicle_msgs` | `autoware_auto_vehicle_msgs` | Missing `.auto` prefix |
| `Control` message | `AckermannControlCommand` | Current canonical type |
| `tier4_vehicle_msgs/*` | *(removed)* | Not Autoware.Auto. Replaced with `autoware_auto_*_msgs` equivalents or removed. |
| `ActuationCommandStamped` | *(removed)* | Nav2 pattern. Control → CAN directly. |
| `VehicleEmergencyStamped` | `Engage` message | Original type doesn't exist in Autoware.Auto |
| Gear `10=REVERSE, 20=PARK` | `REVERSE=20, PARK=22, LOW=23` | Match `autoware_auto_vehicle_msgs/GearCommand` constants |
| `~/input/from_can_bus` / `~/output/to_can_bus` | *(removed)* | Raw CAN pass-through is a security concern |
| `longitudinal.speed` | *(keep — IS correct)* | Field is `speed` in `LongitudinalCommand.msg` |
| `1=Autonomous, 4=Manual` | *(keep — matches Autoware constants)* | `ControlModeReport`: AUTONOMOUS=1, MANUAL=4 |

---

## 2. CAN Protocol (Unchanged from Current Architecture)

The high-bus CAN protocol stays exactly as defined in `architecture.md` §2.2 and `can-dictionary.md` §2. No CAN IDs change. No encoding changes. No new frames.

### 2.1 Commands (Jetson → RT) — UNCHANGED

| CAN ID | Name | Content | Rate |
|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | DLC=0 (event) | Event |
| `0x300` | HOST_DRIVE_CMD | `{i32 speed_mmps, i24 yaw_rate_mrad_s, u8 gear}` | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQ | `{i32 brake_pressure_kpa}` | On demand |
| `0x302` | HOST_LIGHT_CMD | `{u8 bits: LT[0] RT[1] BRK[2] HEAD[3]}` | On change |
| `0x7FC` | JETSON_HEARTBEAT | `{u8 alive_ctr}` | 2 Hz |

### 2.2 Feedback (RT → Jetson) — UNCHANGED

| CAN ID | Name | Content | Rate |
|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | DLC=0 (forwarded from low bus) | Event |
| `0x011` | SYS_SAFETY_STS | `{u8 estop, u8 hb_ok}` (forwarded from SYS) | 5 Hz |
| `0x120` | SYS_THROTTLE_STS | `{i16 speed_mmps}` (forwarded from MTR) | 100 Hz |
| `0x210` | RT_STATE_RPT | `{u8 mode, u8 steer_valid, u8 reversing}` | 10 Hz |
| `0x400` | HOST_OBSTACLE_DIST | `{u32 distance_mm}` | 10 Hz |
| `0x600` | SYS_DIAG_RPT | `{u8 mode, u8 brake, u8 hb, u8 estop, u16 heap, u8 tec, u8 rec}` | 1 Hz |
| `0x7FD` | RT_HEARTBEAT | `{u8 alive_ctr}` | 2 Hz |

### 2.3 Why No Changes

- **CAN IDs:** The current IDs work. Changing them requires coordinated RT + Jetson firmware updates with zero functional benefit.
- **Encoding:** Physical units (mm/s, kPa) are directly readable on `candump`. Ratio encoding adds conversion steps without improving anything.
- **Split frames:** The current combined `0x300` is atomic per planning cycle. Splitting into steer/brake/throttle CAN IDs creates correlation problems that don't exist today.
- **Autoware.Auto doesn't standardize CAN.** The ROS topics are the interface contract. The CAN bus is an internal implementation detail.

---

## 3. RT ESP32-S3 (Unchanged)

The RT firmware stays as described in `architecture.md` §7. No changes to:
- CAN gateway forwarding rules (§2.3)
- Tricycle kinematics (§7.6)
- Steering LBS state machine + dynamic clamp + following error
- Brake max-select arbitration
- Obstacle speed limit
- Command staleness watchdog (500ms)
- 8 FreeRTOS tasks + priorities (§7.7)
- Any CAN TX/RX behavior

**One addition (optional, not required for Autoware.Auto):** Add `0x721` SEB_STATUS to RT's low-bus RX list for SEB error monitoring. This is a pre-existing gap, not an upgrade requirement.

---

## 4. Safety (Unchanged)

All safety mechanisms in `architecture.md` §§6-8 remain identical:
- 8 ESTOP trigger paths (physical button, CAN 0x001, heartbeat loss, following error, external watchdog, 0x204 staleness)
- EGAS 3-level motor safety (MTR L1, SYS L2, hardware ESTOP L3)
- Mode-gated dual control (Option D)
- Heartbeat/liveness matrix (unchanged rates and timeouts)
- SYS heartbeat: 10 Hz / 200ms (the post-gap-#12 rate — can-dictionary.md to be updated)

---

## 5. Jetson Node Implementation

### 5.1 Package

```
jetson/src/autoware_vehicle_bridge/
  ├── include/autoware_vehicle_bridge/
  │   └── vehicle_bridge_node.hpp
  ├── src/
  │   ├── vehicle_bridge_node.cpp    # Lifecycle node
  │   ├── ros_to_can.cpp             # AckermannControlCommand → 0x300/0x301/0x302
  │   └── can_to_ros.cpp             # 0x120/0x011/0x210/0x600 → ROS messages
  ├── config/
  │   └── etrike.param.yaml          # Trike-specific parameters
  └── CMakeLists.txt
```

### 5.2 Encoding (ROS → CAN)

```cpp
// AckermannControlCommand → 0x300 HOST_DRIVE_CMD
void encode_drive_cmd(const AckermannControlCommand& cmd, can::Frame& fr) {
    fr.id = 0x300;
    fr.dlc = 8;
    
    // Speed: m/s → mm/s
    int32_t speed_mmps = (int32_t)(cmd.longitudinal.speed * 1000.0f);
    speed_mmps = std::clamp(speed_mmps, -500, 3000);
    
    // Steering angle → yaw rate via inverse tricycle kinematics
    // δ = arctan(L·ω/|v|) → ω = |v|·tan(δ)/L  where L=1.5m
    float v_ms = std::abs(cmd.longitudinal.speed);
    float delta_rad = cmd.lateral.steering_tire_angle;
    int32_t yaw_mrad_s = 0;
    if (v_ms > 0.05f) {
        float omega_rad_s = v_ms * std::tan(delta_rad) / 1.5f;
        yaw_mrad_s = (int32_t)(omega_rad_s * 1000.0f);
    }
    yaw_mrad_s = std::clamp(yaw_mrad_s, -3000, 3000);
    
    // Gear: auto-derived from speed direction (CAN override via GearCommand)
    uint8_t gear = (speed_mmps > 50) ? 1 : (speed_mmps < -50) ? 3 : 0;  // D=1, R=3, N=0
    
    // Big-endian encode
    fr.put_i32(0, speed_mmps);
    fr.data[4] = (yaw_mrad_s >> 16) & 0xFF;
    fr.data[5] = (yaw_mrad_s >> 8) & 0xFF;
    fr.data[6] = yaw_mrad_s & 0xFF;
    fr.data[7] = gear;
}

// Brake: longitudinal.acceleration (negative) → 0x301 HOST_BRAKE_REQ
void encode_brake_req(const AckermannControlCommand& cmd, can::Frame& fr) {
    if (!cmd.longitudinal.is_defined_acceleration) return;
    float decel = std::max(0.0f, -cmd.longitudinal.acceleration);
    int32_t kpa = (int32_t)(decel / 5.0f * 5000.0f);  // 5 m/s² → 5000 kPa (max)
    kpa = std::clamp(kpa, 0, 5000);
    
    fr.id = 0x301;
    fr.dlc = 4;
    fr.put_i32(0, kpa);
}
```

### 5.3 Decoding (CAN → ROS)

```cpp
// 0x120 SYS_THROTTLE_STS → VelocityReport
void decode_velocity(const can::Frame& fr, VelocityReport& msg) {
    int16_t speed_mmps = fr.get_i16(0);
    msg.longitudinal_velocity = speed_mmps / 1000.0f;  // mm/s → m/s
    msg.lateral_velocity = 0.0f;   // No sensor
    msg.heading_rate = 0.0f;        // No sensor (TBD: IMU or inverse kinematics)
}

// 0x210 RT_STATE_RPT → ControlModeReport + GearReport
void decode_state(const can::Frame& fr, ControlModeReport& mode_msg, GearReport& gear_msg) {
    uint8_t mode = fr.data[0];
    bool reversing = fr.data[2];
    
    // Map trike mode → Autoware constants
    switch (mode) {
        case 0: mode_msg.mode = ControlModeReport::MANUAL; break;      // 4
        case 1: mode_msg.mode = ControlModeReport::AUTONOMOUS; break;   // 1
        case 2: mode_msg.mode = ControlModeReport::DISENGAGED; break;   // 5
    }
    
    uint8_t gear = reversing ? 3 : 1;  // R=3, D=1 (simplified — full mapping TBD)
    gear_msg.report = gear;
}
```

### 5.4 Parameters (etrike.param.yaml)

```yaml
/**:
  ros__parameters:
    wheel_base: 1.5                # m (tricycle)
    max_speed_forward: 3.0         # m/s
    max_speed_reverse: 0.5         # m/s
    max_steering_angle: 0.698      # rad (40°)
    max_brake_pressure_kpa: 5000   # 5 MPa
    loop_rate: 100.0               # Hz
    command_timeout_ms: 500
    can_interface: "can0"
    rt_heartbeat_timeout_ms: 1500
```

**Removed from original document's §3** (not applicable to trike):
VGR coefficients, wheel_radius, margin_time_for_gear_change, use_actuation_cmd, convert_steer_cmd, max_throttle (pedal ratio), max_brake (pedal ratio), emergency_brake (pedal ratio)

---

## 6. Document Updates to `autoware-auto-communication-architecture.md`

### 6.1 §1 ROS 2 Topics: Fix compliance errors
- `.auto` prefix on all package names
- `Control` → `AckermannControlCommand`
- Remove `ActuationCommandStamped`, `VehicleEmergencyStamped`, `ActuationStatusStamped` (tier4)
- Remove raw CAN pass-through topics
- Add `Engage` subscription
- Keep correct field names (`longitudinal.speed`, `1=Autonomous/4=Manual`)

### 6.2 §2 CAN Protocol: Replace with actual trike CAN IDs
- Use `architecture.md` §2.2 / `can-dictionary.md` §2 as source of truth
- Commands: `0x300` drive, `0x301` brake, `0x302` lights, `0x001` ESTOP
- Feedback: `0x011`, `0x120`, `0x210`, `0x400`, `0x600`
- Heartbeats: `0x7FC`, `0x7FD`
- Physical units throughout (no ratio encoding)
- Remove the original's `0x200`–`0x206`, `0x300`–`0x340` CAN IDs (reference design for different vehicle)

### 6.3 §3 Parameters: Set trike values
- wheel_base: 1.5m, max_steering_angle: 0.698rad (40°), etc.
- Remove passenger-car params (VGR, wheel_radius, margin_time)

### 6.4 §4 micro-ROS: Extract
- Move to separate reference document or remove
- Not applicable to E-Trike (ESP32-S3 + raw CAN)

### 6.5 §5 HIL Serial: Extract
- Move to `docs/hil-simulation-protocol.md`
- Not production hardware

### 6.6 §6 Safety Limits: Adapt
- Keep safety concept, update references to E-Trike architecture
- Reference `architecture.md` §§7.10, 8.10 for actual enforcement

---

## 7. Pre-Existing Issues (Separate from This Upgrade)

These issues exist in the current system regardless of Autoware.Auto integration. They should be tracked separately:

| # | Issue | Severity |
|:---|:---|:---|
| B1 | Steering angle offset (EPS-C CSV offset=-3000 vs architecture offset=0) | CRITICAL |
| B2 | SEB comm-fault behavior (hold vs release on CAN loss) | CRITICAL |
| H1 | Dynamic angle clamp interpolation formula undefined | HIGH |
| H2 | SYS heartbeat rate: 10Hz in architecture §8.6/§8.9 but 2Hz in can-dictionary | HIGH |
| H3 | 0x721 not in RT RX list (needed for SEB error monitoring) | HIGH |
| H4 | 0x400 direction contradictory (Jetson→RT vs RT→Jetson) | MEDIUM |
| M1 | Following error threshold should be speed-scaled, not fixed 5° | MEDIUM |
| M2 | Obstacle→kPa formula undefined | MEDIUM |
| M3 | 0x206 MTR_MOTOR_FBK missing from can-dictionary | MEDIUM |
| M4 | VCU_Veh_Spd_Value source (commanded vs measured speed) undocumented | MEDIUM |
| M5 | 0x120 sender: MTR vs SYS documentation conflict | LOW |

**These are not caused by the Autoware.Auto upgrade. They exist today.** They should be fixed independently.

---

## 8. What This Approach Avoids

| Avoided Complexity | Why |
|:---|:---|
| CAN ID changes | Current IDs work. Changing them requires coordinated RT+Jetson firmware updates. |
| Ratio encoding | Two conversion steps (physical→ratio→CAN→ratio→physical) that cancel out. |
| Split command frames | PendingSetpoint assembly, 3 queues, per-field staleness, cross-cycle correlation. |
| Dual-protocol transition | Gated deployment, RX for both old and new, phased ID enable/disable. |
| `0x300` direction reversal | Old: Jetson→RT command. New: RT→Jetson feedback. Same ID, same bus, opposite direction. |
| Extra feedback CAN IDs | `0x301` BRAKE_FBK and `0x302` THROTTLE_FBK not needed for any ROS message. |

---

## 9. Implementation

### Phase 1: Document Update
- [ ] Apply §6 changes to `autoware-auto-communication-architecture.md`
- [ ] Fix can-dictionary.md: `0x7FE`→10Hz, add `0x206`, fix `0x120` sender

### Phase 2: Jetson Node
- [ ] Create `jetson/src/autoware_vehicle_bridge/` package
- [ ] Lifecycle node (configure/activate/deactivate/cleanup)
- [ ] ROS→CAN encoding (AckermannControlCommand → 0x300/0x301/0x302)
- [ ] CAN→ROS decoding (0x120/0x011/0x210 → reports)
- [ ] Heartbeat (send `0x7FC`, monitor `0x7FD`)

### Phase 3: Integration Test
- [ ] End-to-end: AckermannControlCommand → 0x300 → RT → actuator → feedback → ROS report
- [ ] Engage=false → stop commands
- [ ] CAN 0x001 ESTOP path from Jetson
- [ ] 4-hour endurance soak

---

## 10. CAN ID Quick Reference

### High Bus (Jetson ↔ RT) — UNCHANGED

| ID | Name | Direction | Content |
|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | Bidir (bridged) | DLC=0 |
| `0x011` | SYS_SAFETY_STS | ← (fwd) | `{u8 estop, u8 hb_ok}` |
| `0x120` | SYS_THROTTLE_STS | ← (fwd) | `{i16 speed_mmps}` |
| `0x210` | RT_STATE_RPT | ← | `{u8 mode, u8 steer_valid, u8 reversing}` |
| `0x300` | HOST_DRIVE_CMD | → | `{i32 speed_mmps, i24 yaw_mrad_s, u8 gear}` |
| `0x301` | HOST_BRAKE_REQ | → | `{i32 brake_pressure_kpa}` |
| `0x302` | HOST_LIGHT_CMD | → | `{u8 bits}` |
| `0x400` | HOST_OBSTACLE_DIST | → | `{u32 mm}` |
| `0x600` | SYS_DIAG_RPT | ← (fwd) | `{8 bytes}` |
| `0x7FC` | JETSON_HEARTBEAT | → | `{u8 ctr}` |
| `0x7FD` | RT_HEARTBEAT | ← | `{u8 ctr}` |

### Low Bus (RT/SYS/Actuators) — UNCHANGED

| ID | Name | Sender | Locked? |
|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | Any | — |
| `0x011` | SYS_SAFETY_STS | SYS | — |
| `0x110` | SYS_MODE_CMD | SYS | — |
| `0x120` | SYS_THROTTLE_STS | MTR | — |
| `0x169` | VCU_SES_REQ | RT | SYNTREE |
| `0x201` | SES_STATUS | EPS-C | SYNTREE |
| `0x204` | RT_DRIVE_CMD | RT | — |
| `0x205` | RT_BRAKE_CMD | RT | — |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | — |
| `0x721` | SEB_STATUS | SEB | SYNTREE |
| `0x7B9` | VCU_SEB_REQ | RT/SYS (mode-gated) | SYNTREE |
| `0x7FD` | RT_HEARTBEAT | RT | — |
| `0x7FE` | SYS_HEARTBEAT | SYS | — |

---

*Version: 0.0.4-alpha. Optimal architecture — minimum change for maximum Autoware.Auto compatibility.*
