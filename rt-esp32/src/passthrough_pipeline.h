#pragma once

#include <cstdint>
#include "protocol/compat/can.hpp"
#include "bypass_modes.h"
#include "rt_snapshots.h"

namespace rt {

// Rule 11: Pure passthrough logic mapping high-level commands directly
// to actuator motion snapshots without clamps, interlocks, or state gating.
inline void compute_passthrough_motion(
    const HostDriveSnapshot& host,
    int64_t now_us,
    etrike::HighLevelLossPolicy loss_policy,
    uint32_t loss_timeout_ms,
    int16_t direct_steer_angle,
    bool direct_steer_valid,
    int32_t brake_request_kpa,
    MotionOutputSnapshot& out
) {
    const int64_t loss_timeout_us = int64_t(loss_timeout_ms) * 1000;
    const bool host_loss = (host.timestamp_us <= 0) || (now_us - host.timestamp_us > loss_timeout_us);

    out = {};
    if (host_loss && loss_policy == etrike::HighLevelLossPolicy::ZeroOutput) {
        // Rule 10: ZeroOutput policy zeroes all motion outputs on loss
        out.motor_speed_mmps = 0;
        out.motor_gear = uint8_t(can::Gear::N);
        out.steer_angle_0_1deg = 0;
        out.brake_kpa = 0;
    } else {
        // Passthrough: speed is completely unclamped (vehicle limits ignored)
        out.motor_speed_mmps = host.speed_mmps;
        if (host.drive_cmd_valid) {
            out.motor_gear = host.gear_override;
            if (out.motor_gear == uint8_t(can::Gear::N)) {
                out.motor_speed_mmps = 0;
            }
        } else if (host.speed_mmps > 0) {
            out.motor_gear = uint8_t(can::Gear::D);
        } else if (host.speed_mmps < 0) {
            out.motor_gear = uint8_t(can::Gear::R);
        } else {
            out.motor_gear = uint8_t(can::Gear::N);
        }

        // Steer angle: completely unclamped (dynamic speed-angle curves ignored)
        if (direct_steer_valid) {
            out.steer_angle_0_1deg = direct_steer_angle;
        } else if (host.direct_steer_valid) {
            out.steer_angle_0_1deg = static_cast<int16_t>(host.direct_steer_0_1deg);
        } else {
            out.steer_angle_0_1deg = static_cast<int16_t>(host.yaw_rate_mrad_s / 10);
        }
        out.brake_kpa = brake_request_kpa;
    }

    out.current_mode = uint8_t(can::Mode::Auto);
    out.estop_reason = 0;
    out.safety_state = 0;
    out.seb_emergency_takeover = false;
    out.steer_command_enable = true;
    out.reversing = (out.motor_speed_mmps < 0);
}

} // namespace rt
