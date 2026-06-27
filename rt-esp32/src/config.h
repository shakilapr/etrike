#pragma once
// RT ESP32-S3 — configuration constants (architecture.md §7.9).
// Change values here, not in source files.
//
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).

#include <cstdint>
#include "shared_config.h"

namespace rt {

// ── steering — SYNTREE EPS-C via CAN 0x169 ───────────────────────
constexpr float kSteerFollowingErrMinDeg=   2.0f;   // floor threshold (was fixed 5.0)
constexpr float kSteerFollowingErrFactor=  0.25f;   // × dynamic_limit → threshold
constexpr int   kSteerFollowingErrMs    =   300;    // must persist
constexpr int   kSteerCmdRateHz         =   100;    // SYNTREE 10 ms period (was 50Hz — 20ms margin too tight with ±5ms TX jitter)
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
constexpr int   kSteerSyncTimeoutMs     = 5000;     // LISTEN_SYNC timeout → FAULT (gap C1)
constexpr int   kSyntreeAngleOffset     = 30000;    // SYNTREE CSV offset: raw = angle_0_1deg + offset (0° → raw=30000)
constexpr float kSteerEstopRampDegS     = 20.0f;    // ESTOP ramp-to-zero rate (gap C3)
constexpr int   kSteerEstopHoldMs       = 500;      // obstacle ESTOP: hold then silent-stop (gap C3)

// ── timing (ms / Hz) ──────────────────────────────────────────────
constexpr int kControlLoopHz           =  100;
constexpr int kHeartbeatIntervalMs     =  500;  // RT sends 0x7FD at 2 Hz
constexpr int kHeartbeatTimeoutMsSys   = 200;  // monitors SYS 0x7FE at 10 Hz, 2 missed frames = 200ms → brake takeover

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

// ── steering alias (used by physics_model.cpp) ────────────────────
constexpr float kSteerLimitDeg = 40.0f;      // soft limit, matches kSteerHardLimitDeg

}  // namespace rt
