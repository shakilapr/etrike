// RT control composition — converts a motion command into CAN setpoints.
// Phase R4: outputs can::RtDriveCmd (0x202) + can::RtBrakeCmd (0x203)
// instead of intermcu UART frames.

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

ControlOutput resolve_drive_setpoint(
    PhysicsModel& physics,
    SpeedPid& pid,
    const DriveCmd& cmd,
    int32_t measured_speed_mmps,
    unsigned obstacle_mm,
    int32_t brake_request_kpa,
    float dt_s)
{
    // 1. Kinematics: resolve speed + steer
    ResolvedSetpoint resolved;
    physics.resolve(cmd, resolved);

    // 2. Obstacle speed limit
    const int32_t target_speed_mmps =
        PhysicsModel::obstacle_limit(resolved.motor_speed_mmps, obstacle_mm);

    // 3. PID effort (placeholder — open-loop until encoders fitted)
    const float effort = pid.update(
        static_cast<float>(target_speed_mmps),
        static_cast<float>(measured_speed_mmps),
        dt_s);
    (void)effort;  // used when encoders are fitted (gap #5)

    // 4. Gear derivation from target speed
    uint8_t gear = 0;  // N
    if (target_speed_mmps > 0) {
        gear = uint8_t(can::Gear::D);
    } else if (target_speed_mmps < 0) {
        gear = uint8_t(can::Gear::R);
    }

    // 5. Brake arbitration: RT floor + Jetson request
    // RT obstacle-emergency brake pressure (future: from obstacle_task)
    const int32_t rt_computed_brake = 0;  // TODO: obstacle → hard brake
    const int32_t brake_pressure_kpa = std::max(rt_computed_brake, brake_request_kpa);

    ControlOutput out;
    out.drive.motor_speed_mmps = target_speed_mmps;
    out.drive.gear             = gear;
    out.brake.brake_pressure_kpa = brake_pressure_kpa;

    return out;
}

}  // namespace rt
