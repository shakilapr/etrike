#pragma once
// Testable RT control composition: physics + obstacle limit + speed PID effort.

#include <cstdint>
#include "physics_model.h"
#include "speed_pid.h"
// TODO Phase 3/5: replace inter_mcu::RtToSysSetpoint with internal Setpoint struct
// (was: #include "intermcu/intermcu_protocol.h" — removed, inter-MCU link eliminated)

namespace rt {

// Placeholder — will be replaced by unified Setpoint struct in Phase 5
struct Setpoint {
    int32_t steer_angle_mdeg   = 0;
    int32_t motor_effort_pwm   = 0;
    int32_t brake_pressure_kpa = 0;
    uint8_t flags              = 0;
};

Setpoint resolve_drive_setpoint(
    PhysicsModel& physics,
    SpeedPid& pid,
    const DriveCmd& cmd,
    int32_t measured_speed_mmps,
    unsigned obstacle_mm,
    int32_t brake_request_kpa,
    float dt_s);

}  // namespace rt
