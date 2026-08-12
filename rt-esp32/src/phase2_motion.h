#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "physics_model.h"
#include "protocol/compat/can.hpp"
#include "shared_config.h"

namespace rt {

// Ten missed 10 ms command/report cycles. A stale direct-angle command yields
// authority back to the legacy 0x300 yaw path; it does not stop longitudinal
// motion or alter the existing Host drive watchdog.
constexpr int kDirectSteerTimeoutMs = can::gen::HostSteerCmd::kCycleMs * 10;
constexpr int kMotionFeedbackTimeoutMs = 100;

inline bool apply_fresh_direct_steering(const can::gen::HostSteerCmd& command,
                                        int64_t received_us,
                                        int64_t now_us,
                                        ResolvedSetpoint& setpoint) {
    const bool fresh = received_us >= 0 && now_us >= received_us
        && now_us - received_us <= int64_t(kDirectSteerTimeoutMs) * 1000;
    if (!fresh || !command.angle_valid) return false;

    setpoint.steer_angle_mdeg = int32_t(command.steer_angle_0_1deg) * 100;
    setpoint.steer_valid = true;
    setpoint.steer_saturated = false;
    return true;
}

inline int32_t estimate_yaw_rate_mrad_s(int32_t speed_mmps,
                                        int32_t steer_angle_0_1deg) {
    constexpr float kPi = 3.14159265358979323846f;
    const float steer_rad = float(steer_angle_0_1deg) * 0.1f * kPi / 180.0f;
    const float wheelbase_m = shared::kWheelbaseMM / 1000.0f;
    const float yaw_mrad_s = float(speed_mmps) * std::tan(steer_rad) / wheelbase_m;
    return std::clamp<int32_t>(static_cast<int32_t>(std::lround(yaw_mrad_s)),
                               -3000, 3000);
}

inline can::gen::RtMotionRpt make_motion_report(
        int64_t now_us,
        int32_t measured_speed_mmps,
        uint8_t physical_gear,
        int64_t motor_received_us,
        int32_t measured_steer_0_1deg,
        uint8_t steer_angle_status,
        int64_t steer_received_us,
        uint8_t rolling_counter) {
    const auto is_fresh = [now_us](int64_t received_us) {
        return received_us >= 0 && now_us >= received_us
            && now_us - received_us <= int64_t(kMotionFeedbackTimeoutMs) * 1000;
    };

    can::gen::RtMotionRpt report{};
    report.speed_mmps = static_cast<int16_t>(std::clamp<int32_t>(
        measured_speed_mmps, -500, 3000));
    report.gear = physical_gear <= uint8_t(can::Gear::R)
        ? physical_gear : uint8_t(can::Gear::N);
    report.speed_valid = is_fresh(motor_received_us);
    report.gear_valid = report.speed_valid && physical_gear <= uint8_t(can::Gear::R);

    const bool steering_valid = is_fresh(steer_received_us)
        && steer_angle_status == 1;
    report.yaw_rate_valid = report.speed_valid && steering_valid;
    report.yaw_rate_mrad_s = report.yaw_rate_valid
        ? estimate_yaw_rate_mrad_s(report.speed_mmps, measured_steer_0_1deg)
        : 0;
    report.rolling_counter = rolling_counter;
    return report;
}

}  // namespace rt
