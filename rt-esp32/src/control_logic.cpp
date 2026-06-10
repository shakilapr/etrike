// RT control composition — converts a motion command into an inter-MCU setpoint.

#include "control_logic.h"
#include <algorithm>

#ifndef __cpp_lib_clamp
namespace std {
template<typename T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
}
#endif

namespace rt {

inter_mcu::RtToSysSetpoint resolve_drive_setpoint(
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

    const int32_t motor_effort_pwm = std::clamp(
        static_cast<int32_t>(effort),
        -inter_mcu::kMotorEffortMax,
        inter_mcu::kMotorEffortMax);

    // Brake arbitration: RT floor (0 today, obstacle-emergency later),
    // Jetson can increase but never decrease below RT's safety floor.
    const int32_t rt_computed_brake = 0;  // future: obstacle stop → hard brake
    const int32_t brake_pressure_kpa = std::max(rt_computed_brake, brake_request_kpa);

    return {
        motor_effort_pwm,
        resolved.steer_angle_mdeg,
        brake_pressure_kpa,
        inter_mcu::kFlagAutoEnable | inter_mcu::kFlagEpsEnable,
    };
}

}  // namespace rt
