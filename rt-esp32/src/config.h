#pragma once
// RT ESP32-S3 — configuration constants (architecture.md §7.9).
// Change values here, not in source files.
//
// CAN protocol IDs and generated message contracts are in protocol/.
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).

#include <cstdint>
#include "shared_config.h"
#include "protocol/compat/can.hpp"

namespace rt {

// RT policy values carried by RtStateRpt::estop_reason.
constexpr uint8_t kEstopReasonNone           = 0;
constexpr uint8_t kEstopReasonButton         = 1;
constexpr uint8_t kEstopReasonHeartbeat      = 2;
constexpr uint8_t kEstopReasonFollowingError = 3;
constexpr uint8_t kEstopReasonObstacle       = 4;
constexpr uint8_t kEstopReasonCanEstop       = 5;
constexpr uint8_t kEstopReasonBusOff         = 6;
constexpr uint8_t kEstopReasonInternal       = 7;
constexpr uint8_t kEstopReasonEgasMismatch   = 8;
constexpr uint8_t kEstopReasonStaleCmd       = 9;
constexpr uint8_t kEstopReasonWatchdog       = 10;

// ── steering — steer-by-wire unit via CAN 0x169 ───────────────────────
constexpr float kSteerFollowingErrMinDeg=   2.0f;   // floor threshold (was fixed 5.0)
constexpr float kSteerFollowingErrFactor=  0.25f;   // × dynamic_limit → threshold
constexpr int   kSteerFollowingErrMs    =   300;    // must persist
constexpr int   kSteerCmdRateHz         =    50;    // 0x169 contract and active TX schedule: 20 ms
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
constexpr int   kSbwAngleOffset     = 30000;    // steer-by-wire CSV offset: raw = angle_0_1deg + offset (0° → raw=30000)
constexpr float kSteerEstopRampDegS     = 20.0f;    // ESTOP ramp-to-zero rate (gap C3)
constexpr int   kSteerEstopHoldMs       = 500;      // obstacle ESTOP: hold then silent-stop (gap C3)

// ── timing (ms / Hz) ──────────────────────────────────────────────
constexpr int kControlLoopHz           =  100;
constexpr int kHeartbeatIntervalMs     = can::gen::RtHeartbeat::kCycleMs;
constexpr int kHeartbeatTimeoutMsSys   = can::gen::SysHeartbeat::kCycleMs * 2; // policy: two missed frames
constexpr int kLowCanPeerTimeoutMs     = 1500;  // TX closes when no valid low-bus peer is heard

// ── CAN — low-level (built-in TWAI) ───────────────────────────────
// Default pin map matches architecture (CTX←GPIO5, CRX←GPIO4).
// If frames never leave the node but RX works, try the swap flag
// (bench docs: "try swapping CTX/CRX wires").
#ifndef ETRIKE_RT_TWAI_SWAP_TX_RX
#define ETRIKE_RT_TWAI_SWAP_TX_RX 0
#endif
constexpr int kCanLowBitrateHz = 500'000;
#if ETRIKE_RT_TWAI_SWAP_TX_RX
constexpr int kCanLowTxGpio    =      4;
constexpr int kCanLowRxGpio    =      5;
#else
constexpr int kCanLowTxGpio    =      5;
constexpr int kCanLowRxGpio    =      4;
#endif

// ── CAN — high-level (external MCP2515 via SPI) ───────────────────
constexpr int kCanHighBitrateHz = 500'000;
constexpr int kSpiSckGpio       =      15;
constexpr int kSpiMosiGpio      =      16;
constexpr int kSpiMisoGpio      =      17;
constexpr int kSpiCsGpio        =      18;
constexpr int kMcpIntGpio       =      47; // MCP2515 INT; GPIO47 is not a strapping pin.

// ── watchdog ──────────────────────────────────────────────────────
// constexpr int kWdtToggleGpio = 21; // Temporarily disabled

// ── encoders (quadrature PCNT, sensor TBD for wheels) ──────────────
constexpr int kEncRearMotorA   =  1;  // rear motor speed feedback
constexpr int kEncRearMotorB   =  2;
constexpr int kEncFrontWheelA  = 10;  // front wheel speed/angle
constexpr int kEncFrontWheelB  =  6;
constexpr int kEncRearLeftA    =  9;  // rear left wheel differential
constexpr int kEncRearLeftB    = 12;
constexpr int kEncRearRightA   = 13;  // rear right wheel differential
constexpr int kEncRearRightB   = 14;

// ── steering alias (used by physics_model.cpp) ────────────────────
constexpr float kSteerLimitDeg = 40.0f;      // soft limit, matches kSteerHardLimitDeg

}  // namespace rt
