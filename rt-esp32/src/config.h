#pragma once
// RT ESP32-S3 — configuration constants (architecture.md §7.9).
// Change values here, not in source files.
//
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).

#include <cstdint>
#include "shared_config.h"

namespace rt {

// ── vehicle geometry (mm) ─────────────────────────────────────────
constexpr float kTrackWidthMM    =  800.0f;
constexpr float kWheelRadiusMM   =  200.0f;

// ── steering — SYNTREE EPS-C via CAN 0x169 ───────────────────────
constexpr float kSteerHardLimitDeg      =  40.0f;   // software hard-stop
constexpr float kSteerFollowingErrMinDeg=   2.0f;   // floor threshold (was fixed 5.0)
constexpr float kSteerFollowingErrFactor=  0.25f;   // × dynamic_limit → threshold
constexpr int   kSteerFollowingErrMs    =   300;    // must persist
constexpr int   kSteerCmdRateHz         =    50;    // SYNTREE 20 ms period
constexpr int   kSteerBootWaitMs        =   500;
// Dynamic angle clamp: limit_deg = 40.0 − (speed_kmh − 2.0) × (35.0/23.0), clamped [5.0, 40.0]
constexpr float kAngleClampBaseDeg      =  40.0f;   // max at 2 km/h
constexpr float kAngleClampMinDeg       =   5.0f;   // min at ≥25 km/h
constexpr float kAngleClampRangeDeg     =  35.0f;   // base − min
constexpr float kAngleClampSpeedRange   =  23.0f;   // 25 − 2 km/h
// Steering slew rate: rate_deg_s = 125 + (speed_kmh − 2) × (400/23), clamped [125, 525]
constexpr float kSteerRateMinDegS       = 125.0f;   // at low speed
constexpr float kSteerRateMaxDegS       = 525.0f;   // at high speed
constexpr float kSteerRateRangeDegS     = 400.0f;   // max − min

// ── PID — placeholder gains (tune once encoders fitted, gap #5) ──
constexpr float kPidKp = 1.0f;
constexpr float kPidKi = 0.1f;
constexpr float kPidKd = 0.05f;

// ── timing (ms / Hz) ──────────────────────────────────────────────
constexpr int kControlLoopHz           =  100;
constexpr int kHeartbeatIntervalMs     =  500;  // 2 Hz
constexpr int kHeartbeatTimeoutMsSys   = 200;  // SYS→RT, 2 missed at 10 Hz → brake takeover

// ── CAN — low-level (built-in TWAI) ───────────────────────────────
constexpr int kCanLowBitrateHz = 500'000;
constexpr int kCanLowTxGpio    =      5;
constexpr int kCanLowRxGpio    =      4;

// ── CAN — high-level (external MCP2515 via SPI) ───────────────────
constexpr int kCanHighBitrateHz = 500'000;
constexpr int kSpiSckGpio       =      36;
constexpr int kSpiMosiGpio      =      37;
constexpr int kSpiMisoGpio      =      38;
constexpr int kSpiCsGpio        =      39;
constexpr int kMcpIntGpio       =      40;

// ── watchdog ──────────────────────────────────────────────────────
constexpr int kWdtToggleGpio = 21;

// ── encoders (quadrature, PCNT) ───────────────────────────────────
constexpr int kEncRearMotorA  =  1;
constexpr int kEncRearMotorB  =  2;
constexpr int kEncFrontWheelA =  3;       // sensor TBD
constexpr int kEncFrontWheelB =  6;       // sensor TBD
constexpr int kEncRearLeftA   =  9;       // sensor TBD
constexpr int kEncRearLeftB   = 12;       // sensor TBD
constexpr int kEncRearRightA  = 13;       // sensor TBD
constexpr int kEncRearRightB  = 14;       // sensor TBD

// ── sensors ───────────────────────────────────────────────────────
constexpr int kObstacleTrigGpio =  7;
constexpr int kObstacleEchoGpio =  8;
constexpr int kImuSdaGpio       = 10;      // IMU (optional)
constexpr int kImuSclGpio       = 11;

constexpr int kInterMcuTxGpio   = 17;       // REMOVE in Phase R4
constexpr int kInterMcuRxGpio   = 18;       // REMOVE in Phase R4
constexpr int kInterMcuBaud     = 2'000'000; // REMOVE in Phase R4

// ── steering alias (used by physics_model.cpp) ────────────────────
constexpr float kSteerLimitDeg = 40.0f;      // soft limit, matches kSteerHardLimitDeg

}  // namespace rt
