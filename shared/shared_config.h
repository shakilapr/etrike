#pragma once
// Shared configuration constants — single source of truth for vehicle/safety
// parameters that are identical across all ECU projects.
// Include via each project's config.h.
#include <cstdint>

namespace shared {

// Vehicle geometry
constexpr float kWheelbaseMM = 1500.0f;

// Obstacle
constexpr unsigned kObstacleStopMM = 300;
constexpr unsigned kObstacleClearMM = 3000;

// Speed limits
constexpr int kMaxSpeedFwdMmps = 3000;
constexpr int kMaxSpeedRevMmps = 500;
constexpr int kLowSpeedThreshMmps = 50;

// Safety timeouts
constexpr int kCmdStaleTimeoutMs = 500;
constexpr int kHeartbeatTimeoutMsJetson = 1500;
constexpr int kStartupGracePeriodMs = 3000;

// Brake
constexpr float kBrakeStrokeScale = 0.05f;
constexpr float kBrakeStrokeOffset = -30.0f;
constexpr int kSebMaxPressureRaw = 100;
constexpr int kMaxBrakeKpa = 20000;
constexpr int kAssistStopKpa = 2000;

} // namespace shared
