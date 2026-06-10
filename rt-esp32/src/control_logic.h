#pragma once
// Testable RT control composition: physics + obstacle limit + speed PID effort.

#include <cstdint>
#include "physics_model.h"
#include "speed_pid.h"
#include "intermcu/intermcu_protocol.h"

namespace rt {

inter_mcu::RtToSysSetpoint resolve_drive_setpoint(
    PhysicsModel& physics,
    SpeedPid& pid,
    const DriveCmd& cmd,
    int32_t measured_speed_mmps,
    unsigned obstacle_mm,
    int32_t brake_request_kpa,
    float dt_s);

}  // namespace rt
