#pragma once
// Testable RT control composition: physics + obstacle limit + speed PID effort.
// Produces CAN-based output (0x202 RT_DRIVE_CMD + 0x203 RT_BRAKE_CMD).
// Phase R4: migrated from intermcu UART to CAN.

#include <cstdint>
#include "physics_model.h"
#include "speed_pid.h"
#include "can/can_protocol.h"

namespace rt {

struct ControlOutput {
    can::RtDriveCmd drive;   // → 0x202 RT_DRIVE_CMD (speed + gear)
    can::RtBrakeCmd brake;   // → 0x203 RT_BRAKE_CMD (pressure kPa)
    // Steering is handled by SteeringControl (0x200 VCU_SES_REQ), not here.
};

ControlOutput resolve_drive_setpoint(
    PhysicsModel& physics,
    SpeedPid& pid,
    const DriveCmd& cmd,
    int32_t measured_speed_mmps,
    unsigned obstacle_mm,
    int32_t brake_request_kpa,
    float dt_s);

}  // namespace rt
