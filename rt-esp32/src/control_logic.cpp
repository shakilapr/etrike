// Control composition — converts a motion command into a motor/steering/brake setpoint.
// TODO Phase 5: full rewrite for unified single-ESP32 queue-based pipeline.

#include "control_logic.h"
#include <algorithm>

namespace {
constexpr int32_t kMotorEffortMax = 8191;    // 13-bit PWM max
constexpr uint8_t kFlagAutoEnable = 0x01;
constexpr uint8_t kFlagEpsEnable  = 0x02;
}

namespace rt {

Setpoint resolve_drive_setpoint(
    PhysicsModel& physics,
    SpeedPid& pid,
    const DriveCmd& cmd,
    int32_t measured_speed_mmps,
    unsigned obstacle_mm,
    int32_t brake_request_kpa,
    float dt_s) {
    ResolvedSetpoint resolved;
    physics.resolve(cmd, resolved);

    const int32_t target_speed_mmps =
        PhysicsModel::obstacle_limit(resolved.motor_speed_mmps, obstacle_mm);

    const float effort = pid.update(
        static_cast<float>(target_speed_mmps),
        static_cast<float>(measured_speed_mmps),
        dt_s);

    const int32_t motor_effort_pwm = std::max(
        std::min(static_cast<int32_t>(effort), kMotorEffortMax),
        -kMotorEffortMax);

    const int32_t rt_computed_brake = 0;
    const int32_t brake_pressure_kpa = std::max(rt_computed_brake, brake_request_kpa);

    return {
        resolved.steer_angle_mdeg,
        motor_effort_pwm,
        brake_pressure_kpa,
        kFlagAutoEnable | kFlagEpsEnable,
    };
}

}  // namespace rt
