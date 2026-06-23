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

// Dynamic angle clamp (arch §7.6, fix #6): max_angle inversely proportional to speed.
// 40deg at 2 km/h, 5deg at 25 km/h. Returns max allowed angle in degrees.
float compute_dynamic_limit(float speed_mmps);
}  // namespace rt
