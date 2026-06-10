#pragma once
// SYS ESP32-S3 — safety & actuator configuration.  Change values here.

namespace sys {

// ── CAN ────────────────────────────────────────────────────────
constexpr int kCanBitrateHz      = 500'000;
constexpr int kCanTxGpio         =       5;
constexpr int kCanRxGpio         =       4;
constexpr bool kSyntreeCanOutputEnabled = false;  // enable only after checksum/DBC verification

// ── motor driver ───────────────────────────────────────────────
constexpr int kMotorPwmGpio      =       6;
constexpr int kMotorDirGpio      =       7;
constexpr int kMotorPwmFreqHz    =  20'000;
constexpr int kMotorMaxSpeedMmps =   3'000;

// ── brake ──────────────────────────────────────────────────────
constexpr int kBrakeGpio         =       8;

// ── safety inputs ──────────────────────────────────────────────
constexpr int kEstopGpio         =       1;   // active-low, internal pull-up
constexpr int kBrakeLeverGpio    =       2;

// ── throttle ADC ───────────────────────────────────────────────
constexpr int      kThrottleAdcChannel  = 5;   // ADC1_CH5 → GPIO10
constexpr unsigned kThrottleDeadZone    = 200;
constexpr int      kThrottleMaxSpeedMmps = 3000;

// ── mode switch ────────────────────────────────────────────────
constexpr int kModeSwitchGpio    =      11;

// ── timing ─────────────────────────────────────────────────────
constexpr int kControlLoopHz       = 100;
constexpr int kHeartbeatIntervalMs = 50;
constexpr int kHeartbeatTimeoutMs  = 200;
constexpr int kSafetyCheckHz       =  20;

// ── obstacle limiting ──────────────────────────────────────────
constexpr unsigned kObstacleStopDistMM  =  300;
constexpr unsigned kObstacleClearDistMM = 3000;

}  // namespace sys
