#pragma once
// Delta trike kinematic model — inverse bicycle solver.
// δ = atan2(L · ω, |v|)     R = L / tan(δ)

#include <cstdint>

namespace rt {

struct DriveCmd {
    int32_t speed_mmps      = 0;   // linear.x  [mm/s]
    int32_t yaw_rate_mrad_s = 0;   // angular.z [millirad/s]
};

struct ResolvedSetpoint {
    int32_t motor_speed_mmps  = 0;   // target for rear motor  [mm/s]
    int32_t steer_angle_mdeg  = 0;   // front steer angle      [millideg]
    bool    steer_valid       = false;
    bool    steer_saturated   = false;
    bool    reversing         = false;
};

class PhysicsModel {
public:
    PhysicsModel() = default;

    // Resolve a motion command into actuator setpoints.
    // Returns true (always succeeds for valid inputs).
    bool resolve(const DriveCmd& cmd, ResolvedSetpoint& out);

    // Obstacle-based speed limiting: linear interpolation between
    // stop distance (→ 0 speed) and clear distance (→ full speed).
    static int32_t obstacle_limit(int32_t target_mmps, unsigned obstacle_mm);

private:
    float m_steer_hold_rad = 0.0f;  // last valid steering angle (decays at low speed)
};

}  // namespace rt
