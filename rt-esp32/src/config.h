#pragma once
// RT ESP32-S3 — configuration constants (architecture.md §7.9).
// Change values here, not in source files.

#include <cstdint>

namespace rt {

// ── vehicle geometry (mm) ─────────────────────────────────────────
constexpr float kWheelbaseMM     = 1500.0f;
constexpr float kTrackWidthMM    =  800.0f;
constexpr float kWheelRadiusMM   =  200.0f;

// ── steering — SYNTREE EPS-C via CAN 0x169 ───────────────────────
constexpr float kSteerHardLimitDeg      =  40.0f;   // software hard-stop
constexpr float kSteerFollowingErrDeg   =   5.0f;   // trigger ESTOP
constexpr int   kSteerFollowingErrMs    =   300;    // must persist
constexpr int   kSteerCmdRateHz         =    50;    // SYNTREE 20 ms period
constexpr int   kSteerBootWaitMs        =   500;
constexpr float kSteerMaxAngleLowSpeed  =  40.0f;   // at 2 km/h
constexpr float kSteerMaxAngleHighSpeed =   5.0f;   // at 25 km/h

// ── speed limits (mm/s) ───────────────────────────────────────────
constexpr int kMaxSpeedFwdMmps    =  3000;           // 3 m/s ≈ 10.8 km/h
constexpr int kMaxSpeedRevMmps    =   500;
constexpr int kLowSpeedThreshMmps =    50;           // freeze steering below

// ── PID — placeholder gains (tune once encoders fitted, gap #5) ──
constexpr float kPidKp = 1.0f;
constexpr float kPidKi = 0.1f;
constexpr float kPidKd = 0.05f;

// ── obstacle ──────────────────────────────────────────────────────
constexpr unsigned kObstacleStopDistMM  =   300;
constexpr unsigned kObstacleClearDistMM =  3000;

// ── timing (ms / Hz) ──────────────────────────────────────────────
constexpr int kControlLoopHz           =  100;
constexpr int kCmdStaleTimeoutMs       =  500;
constexpr int kHeartbeatIntervalMs     =  500;  // 2 Hz
constexpr int kHeartbeatTimeoutMsSys   = 200;  // SYS→RT, 2 missed at 10 Hz → brake takeover
constexpr int kHeartbeatTimeoutMsJetson= 1500;  // Jetson→RT, 3 missed

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

// ── CAN ID aliases (from shared/can/can_protocol.h) ───────────────
// Low bus — RT sends
constexpr uint32_t kIdRtDriveCmd     = 0x204;
constexpr uint32_t kIdRtBrakeCmd     = 0x205;
constexpr uint32_t kIdSyntreeEpsCmd      = 0x169;
// Low bus — RT receives
constexpr uint32_t kIdSysSafetySts   = 0x011;
constexpr uint32_t kIdSysModeCmd     = 0x110;
constexpr uint32_t kIdSysThrottleSts = 0x120;
constexpr uint32_t kIdSysDiagRpt     = 0x600;
constexpr uint32_t kIdSysHeartbeat   = 0x7FE;
constexpr uint32_t kIdSyntreeEpsStatus      = 0x201;
// High bus — RT sends
constexpr uint32_t kIdRtStateRpt     = 0x210;
constexpr uint32_t kIdRtPidRpt       = 0x220;   // reserved (future PID)
constexpr uint32_t kIdHostObstacleDist  = 0x400;
// High bus — RT receives
constexpr uint32_t kIdHostDriveCmd   = 0x300;
constexpr uint32_t kIdHostBrakeReq   = 0x301;
constexpr uint32_t kIdHostLightCmd   = 0x302;
constexpr uint32_t kIdJetsonHeartbeat= 0x7FC;
// Both buses
constexpr uint32_t kIdSafetyEstop    = 0x001;
constexpr uint32_t kIdRtHeartbeat    = 0x7FD;   // RT sends on both buses

}  // namespace rt
