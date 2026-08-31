#pragma once
// RT-AURIX-Lite control configuration — physics/control constants.
// Reuses shared/shared_config.h where values are identical to avoid
// duplicated safety constants drifting.

#include <cstdint>
#include "shared_config.h"  // shared:: (single source of truth)

namespace rta {

// ── Kinematics (inverse bicycle) ───────────────────────────────────
// Wheelbase, speed limits, obstacle thresholds come from shared::.

// ── Steering (steer-by-wire unit) ──────────────────────────────────
constexpr float kSteerFollowingErrMinDeg = 2.0f;    // floor threshold
constexpr float kSteerFollowingErrFactor = 0.25f;   // × dynamic_limit
constexpr int   kSteerFollowingErrMs     = 300;     // persist time
constexpr float kSteerHardLimitDeg       = 40.0f;
constexpr float kSteerMaxAngleLowSpeed   = 40.0f;
constexpr float kSteerMaxAngleHighSpeed  = 5.0f;
constexpr int   kSteerBootWaitMs         = 500;
constexpr int   kSteerSyncTimeoutMs      = 5000;    // LISTEN_SYNC timeout
constexpr int   kSteerEstopHoldMs        = 500;     // obstacle hold-then-silent
constexpr float kSteerEstopRampDegS      = 20.0f;   // ramp-to-zero rate
constexpr float kAngleClampBaseDeg       = 40.0f;   // at 2 km/h
constexpr float kAngleClampMinDeg        = 5.0f;    // at >=25 km/h
constexpr float kAngleClampRangeDeg      = 35.0f;   // base - min
constexpr float kAngleClampSpeedRange    = 23.0f;   // 25 - 2 km/h
constexpr float kSteerRateMinDegS        = 125.0f;
constexpr float kSteerRateMaxDegS        = 525.0f;
constexpr float kSteerRateRangeDegS      = 400.0f;
constexpr int   kSbwAngleOffset          = 30000;   // 0° -> raw 30000
constexpr float kSteerLimitDeg           = 40.0f;   // soft clamp for kinematics

// ── Brake (brake-by-wire unit) ────────────────────────────────────
constexpr int   kBrakeBootWaitMs  = 500;
constexpr int   kBrakeSyncTimeoutMs = 2000;
constexpr float kBrakeManualStroke = 15.0f;  // mm, lever pressed
constexpr float kBrakeMaxStroke    = 27.0f;  // mm, ESTOP full brake

// ── PID (shadow, future active) ───────────────────────────────────
constexpr float kPidKp = 1.0f;
constexpr float kPidKi = 0.1f;
constexpr float kPidKd = 0.05f;

}  // namespace rta
