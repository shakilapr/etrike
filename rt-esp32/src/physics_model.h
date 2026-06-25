#pragma once
// Delta trike kinematics — inverse bicycle model.
// DriveCmd → PhysicsModel.resolve() → ResolvedSetpoint

#include <cstdint>
#include "config.h"

namespace rt {

// ── Internal data types (architecture.md §7.5) ─────────────────────

struct DriveCmd {
    int32_t speed_mmps      = 0;   // [-500, 3000]
    int32_t yaw_rate_mrad_s = 0;   // [-3000, 3000]
};

struct ResolvedSetpoint {
    int32_t motor_speed_mmps  = 0;
    int32_t steer_angle_mdeg  = 0;   // +right, ±45000
    bool    steer_valid       = false;
    bool    steer_saturated   = false;
    bool    reversing         = false;
    uint8_t cmd_gear          = 0;   // CAN gear override (0=none, 1=D, 2=S, 3=R)
};

// ── Kinematics model ───────────────────────────────────────────────

class PhysicsModel {
public:
    // Resolve a motion command into a vehicle setpoint.
    // Returns true on success. Steer angle is internally clamped.
    bool resolve(const DriveCmd& cmd, ResolvedSetpoint& out);

    // Obstacle speed limiter: linearly scales speed from 0 at stop-dist
    // to full speed at clear-dist.
    static int32_t obstacle_limit(int32_t target_mmps, unsigned obstacle_mm);

private:
    float m_steer_hold_rad = 0.0f;   // last valid steer for decay
};

// Dynamic angle clamp: limit_deg = 40.0 − (speed_kmh − 2.0) × (35.0/23.0), clamped [5.0, 40.0].
float compute_dynamic_limit(float speed_mmps);

// Following error threshold: max(2.0, 0.25 × dynamic_limit_deg). Speed-scaled (was fixed 5.0°).
float compute_following_error_threshold(float speed_mmps);
}  // namespace rt
