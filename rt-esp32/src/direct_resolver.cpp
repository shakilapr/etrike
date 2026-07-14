// Direct passthrough kinematics resolver — stateless command mapping.
// Active when ETRIKE_RT_KINEMATICS_RESOLVER=1.
//
// Does NOT use the bicycle inverse-kinematics model.  Commands are scaled
// and clamped directly to actuator limits.

#include "direct_resolver.h"
#include "config.h"
#include <algorithm>
#include <cstdlib>

namespace rt {

namespace {
// Linear scale: max yaw rate (3000 mrad/s) maps to max steer (45000 mdeg).
// 45000 mdeg / 3000 mrad/s = 15 mdeg / (mrad/s)
constexpr int32_t kYawToSteerScale = 15;

// Steering hard limit in millidegrees (±45°).
constexpr int32_t kSteerLimitMdeg = 45000;
}  // anonymous

bool DirectResolver::resolve(const DriveCmd& cmd, ResolvedSetpoint& out) {
    // ── Speed: clamp to configured vehicle limits ───────────────────
    const int32_t max_fwd = static_cast<int32_t>(shared::kMaxSpeedFwdMmps);
    const int32_t max_rev = static_cast<int32_t>(shared::kMaxSpeedRevMmps);
    out.motor_speed_mmps = std::clamp(cmd.speed_mmps, -max_rev, max_fwd);

    // ── Steering: linear scale yaw_rate → steer angle ──────────────
    // Clamp output to ±kSteerLimitMdeg.
    const int32_t raw_steer = cmd.yaw_rate_mrad_s * kYawToSteerScale;
    const bool saturated = (raw_steer > kSteerLimitMdeg || raw_steer < -kSteerLimitMdeg);
    out.steer_angle_mdeg = std::clamp(raw_steer, -kSteerLimitMdeg, kSteerLimitMdeg);
    out.steer_valid      = true;
    out.steer_saturated  = saturated;

    // ── Reversing flag ──────────────────────────────────────────────
    out.reversing = (out.motor_speed_mmps < 0);

    // cmd_gear is propagated by the caller (main.cpp) — not set here.
    out.cmd_gear = 0;

    return true;
}

}  // namespace rt
