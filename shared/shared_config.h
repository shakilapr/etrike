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
constexpr int kHeartbeatTimeoutMsHost = 1500;
constexpr int kStartupGracePeriodMs = 3000;

// Brake
constexpr float kBrakeStrokeScale = 0.05f;
constexpr float kBrakeStrokeOffset = -30.0f;
constexpr int kSebMaxPressureRaw = 100;
constexpr int kMaxBrakeKpa = 5000;        // SEB physical limit (5 MPa = 100 raw)
constexpr int kObstacleMaxKpa = 5000;     // max from obstacle distance formula (5 MPa = SEB limit)
constexpr int kAssistStopKpa = 2000;

// MTR fault flags (0x206 MTR_MOTOR_FBK byte 3, Gap #15)
// Bit definitions shared between MTR STM32 and SYS ESP32 for ESTOP acknowledgment.
constexpr uint8_t kMtrFaultEstopActive  = 0x01;  // ESTOP confirmed active (Gap #15)
constexpr uint8_t kMtrFaultCmdTimeout   = 0x02;  // 0x204 command stale >200ms
constexpr uint8_t kMtrFaultAdcFault     = 0x04;  // Throttle ADC fault
constexpr uint8_t kMtrFaultGearConflict = 0x08;  // Multiple gear lines HIGH

} // namespace shared
