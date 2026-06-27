// Delta trike kinematics — inverse bicycle model solver.

#include "physics_model.h"
#include "config.h"
#include <cmath>
#include <algorithm>
#include "esp_log.h"

// std::clamp is available in C++17 (project standard: -std=gnu++17).
// No polyfill needed.

namespace rt {
namespace {

constexpr const char* kTag = "physics";
constexpr float kPi = 3.14159265358979323846f;

constexpr float deg2rad(float d) { return d * kPi / 180.0f; }
constexpr float rad2deg(float r) { return r * 180.0f / kPi; }

}  // anonymous

float compute_dynamic_limit(float speed_mmps) {
    // limit_deg = 40.0 − (|speed_kmh| − 2.0) × (35.0/23.0), clamped [5.0, 40.0]
    // Uses absolute speed — rollover risk is symmetric in forward/reverse.
    float speed_kmh = std::abs(speed_mmps) * 3.6f / 1000.0f;
    float limit_deg = kAngleClampBaseDeg - (speed_kmh - 2.0f) * (kAngleClampRangeDeg / kAngleClampSpeedRange);
    return std::clamp(limit_deg, kAngleClampMinDeg, kAngleClampBaseDeg);
}

float compute_following_error_threshold(float speed_mmps) {
    float dynamic_limit = compute_dynamic_limit(speed_mmps);
    return std::max(kSteerFollowingErrMinDeg, kSteerFollowingErrFactor * dynamic_limit);
}

bool PhysicsModel::resolve(const DriveCmd& cmd, ResolvedSetpoint& out) {
    float       v = cmd.speed_mmps / 1000.0f;    // m/s
    float const w = cmd.yaw_rate_mrad_s / 1000.0f; // rad/s
    float const L = shared::kWheelbaseMM / 1000.0f;      // m
    constexpr float kYawEpsilon = 0.001f;
    const float steer_limit_rad = deg2rad(kSteerLimitDeg);
    const float low_speed_mps = shared::kLowSpeedThreshMmps / 1000.0f;

    float steer = 0.0f;
    bool  ok    = false;
    bool  saturated = false;

    if (std::abs(v) > low_speed_mps) {
        const float requested_steer = std::atan2(L * w, std::abs(v));
        saturated = std::abs(requested_steer) > steer_limit_rad;
        steer = std::clamp(requested_steer, -steer_limit_rad, steer_limit_rad);
        m_steer_hold_rad = steer;
        ok = !saturated;
    } else if (std::abs(w) > kYawEpsilon) {
        // A tricycle cannot spin in place. Convert pure/near-pure yaw into
        // a deliberate minimum-radius forward arc instead of a dead command.
        const float min_radius_m = L / std::tan(steer_limit_rad);
        const float turn_speed_mps = std::abs(w) * min_radius_m;
        v = std::clamp(turn_speed_mps, low_speed_mps, shared::kMaxSpeedFwdMmps / 1000.0f);
        steer = (w > 0.0f) ? steer_limit_rad : -steer_limit_rad;
        m_steer_hold_rad = steer;
        ok = true;
    } else {
        // Decay toward straight at low speed (avoids noisy steering near standstill)
        constexpr float kSteerDecayFactor = 0.8f;
        steer = m_steer_hold_rad * kSteerDecayFactor;
    }

    // Clamp speed to configured limits
    v = std::clamp(v, -shared::kMaxSpeedRevMmps / 1000.0f, shared::kMaxSpeedFwdMmps / 1000.0f);

    out.motor_speed_mmps  = static_cast<int32_t>(v * 1000.0f);
    out.steer_angle_mdeg  = static_cast<int32_t>(rad2deg(steer) * 1000.0f);
    out.steer_valid       = ok;
    out.steer_saturated   = saturated;
    out.reversing         = (v < 0.0f) && (out.motor_speed_mmps < 0);

    ESP_LOGD(kTag, "v=%.1f ω=%.2f → steer=%.1f° speed=%d rev=%d",
             static_cast<double>(v), static_cast<double>(w),
             static_cast<double>(rad2deg(steer)), out.motor_speed_mmps, out.reversing);
    return true;
}

int32_t PhysicsModel::obstacle_limit(int32_t target_mmps, unsigned obstacle_mm) {
    if (obstacle_mm <= shared::kObstacleStopMM)  return 0;
    if (obstacle_mm >= shared::kObstacleClearMM) return target_mmps;
    float t = static_cast<float>(obstacle_mm - shared::kObstacleStopMM)
            / static_cast<float>(shared::kObstacleClearMM - shared::kObstacleStopMM);
    return static_cast<int32_t>(target_mmps * t);
}

int32_t PhysicsModel::obstacle_to_kpa(unsigned obstacle_mm) {
    if (obstacle_mm <= shared::kObstacleStopMM) return shared::kObstacleMaxKpa;
    if (obstacle_mm >= shared::kObstacleClearMM) return 0;
    float t = static_cast<float>(obstacle_mm - shared::kObstacleStopMM)
            / static_cast<float>(shared::kObstacleClearMM - shared::kObstacleStopMM);
    return static_cast<int32_t>(shared::kObstacleMaxKpa * (1.0f - t));
}

}  // namespace rt
