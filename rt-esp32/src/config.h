#pragma once
// RT ESP32-S3 — configuration constants.  Change values here, not in source.

namespace rt {

// ── vehicle geometry (mm) ──────────────────────────────────────
constexpr float kWheelbaseMM     = 1500.0f;   // front–rear axle
constexpr float kTrackWidthMM    =  800.0f;   // rear wheel spacing
constexpr float kWheelRadiusMM   =  200.0f;   // driven wheel

// ── steering actuator ──────────────────────────────────────────
constexpr float kSteerLimitDeg       =  45.0f;
constexpr int   kSteerServoMinUs     =   500;
constexpr int   kSteerServoMaxUs     =  2500;
constexpr int   kSteerServoCenterUs  =  1500;
constexpr float kSteerSlewRateDegS   = 180.0f;
constexpr int   kSteerPwmFreqHz      =    50;

// ── speed ──────────────────────────────────────────────────────
constexpr int kMaxSpeedFwdMmps    =  3000;     // 3 m/s ≈ 10.8 km/h
constexpr int kMaxSpeedRevMmps    =   500;     // 0.5 m/s
constexpr int kLowSpeedThreshMmps =    50;     // freeze steering below this

// ── PID ────────────────────────────────────────────────────────
constexpr float kPidKp           =  1.0f;
constexpr float kPidKi           =  0.1f;
constexpr float kPidKd           =  0.05f;
constexpr float kPidMaxIntegral  = 500.0f;

// ── obstacle ───────────────────────────────────────────────────
constexpr unsigned kObstacleStopDistMM  =   300;
constexpr unsigned kObstacleClearDistMM =  3000;

// ── timing ─────────────────────────────────────────────────────
constexpr int kControlLoopHz      =  100;
constexpr int kCmdStaleTimeoutMs  =  200;
constexpr int kHeartbeatIntervalMs=   50;

// ── CAN ────────────────────────────────────────────────────────
constexpr int kCanBitrateHz   = 500'000;
constexpr int kCanTxGpio      =      5;
constexpr int kCanRxGpio      =      4;

// ── GPIO (pre-unification — to be merged into esp32/config.h) ──
constexpr int kSteerServoGpio  =  6;
constexpr int kObstacleTrigGpio=  7;
constexpr int kObstacleEchoGpio=  8;
constexpr int kEncoderAGpio    =  1;
constexpr int kEncoderBGpio    =  2;
constexpr int kImuSdaGpio      = 10;
constexpr int kImuSclGpio      = 11;

}  // namespace rt
