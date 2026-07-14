#pragma once
// Direct passthrough kinematics resolver.
// Used when ETRIKE_RT_KINEMATICS_RESOLVER=1.
//
// Maps host commands directly to actuator setpoints without running
// the bicycle inverse-kinematics model.  This is intentionally stateless:
// there is no steering-hold decay and no internal model state.
//
// Input contract:
//   DriveCmd::speed_mmps       → motor_speed_mmps (clamped to vehicle limits)
//   DriveCmd::yaw_rate_mrad_s  → steer_angle_mdeg (scaled linearly to ±45000 mdeg)
//
// Scaling: yaw_rate_mrad_s is linearly mapped from ±3000 mrad/s → ±45000 mdeg.
//   Scale factor = 45000 / 3000 = 15 mdeg per mrad/s
//
// docs/rt-sys-feature-configuration-and-test-plan.md §"Kinematics resolver strategy"

#include "physics_model.h"  // DriveCmd, ResolvedSetpoint
#include "shared_config.h"

namespace rt {

class DirectResolver {
public:
    // Resolve a motion command into a vehicle setpoint by direct mapping.
    // Returns true (always succeeds — no model singularities).
    bool resolve(const DriveCmd& cmd, ResolvedSetpoint& out);
};

}  // namespace rt
