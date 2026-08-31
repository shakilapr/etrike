#pragma once
// Inverse-bicycle kinematics for the delta trike. Pure domain logic:
// no CAN, no IPC, no logging, no clock. Behavior-preserving port of the
// RT ESP32 physics_model (rt-esp32/src/physics_model.*), adapted to the
// rta namespace and rta config. Values that are semantically identical
// come from shared/shared_config.h.

#include <cstdint>
#include "core/types.h"
#include "config/control_config.h"
#include "shared_config.h"

namespace rta {

// Kinematics result — pure values, no logging.
struct KinematicsResult {
    std::int32_t motor_speed_mmps  = 0;
    std::int32_t steer_angle_mdeg  = 0;   // +right, ±45000
    bool         steer_valid       = false;
    bool         steer_saturated   = false;
    bool         reversing         = false;
    std::uint8_t cmd_gear          = 0;   // CAN gear override (0=none)
};

class Kinematics {
public:
    // Resolve a drive demand into a vehicle setpoint.
    // Steer angle is internally clamped. Returns true on success.
    bool resolve(const DriveDemand& cmd, KinematicsResult& out);

    // Obstacle speed limiter: linearly scales speed from 0 at stop-dist
    // to full speed at clear-dist.
    static std::int32_t obstacle_limit(std::int32_t target_mmps, unsigned obstacle_mm);

    // Obstacle brake request: linearly scales brake from max at stop-dist
    // to 0 at clear-dist (kPa).
    static std::int32_t obstacle_to_kpa(unsigned obstacle_mm);

private:
    float m_steer_hold_rad = 0.0f;  // last valid steer for decay
};

// Dynamic angle clamp: limit_deg = 40.0 - (speed_kmh - 2.0) * (35.0/23.0),
// clamped [5.0, 40.0].
float compute_dynamic_limit(float speed_mmps);

// Following error threshold: max(kSteerFollowingErrMinDeg, 0.25 * dynamic_limit_deg).
float compute_following_error_threshold(float speed_mmps);

}  // namespace rta
