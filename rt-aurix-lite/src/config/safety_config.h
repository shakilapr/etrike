#pragma once
// RT-AURIX-Lite safety configuration — ESTOP reasons, EGAS thresholds,
// fault bits. Reuses shared/shared_config.h where identical.

#include <cstdint>
#include "shared_config.h"  // shared:: (single source of truth)

namespace rta {

// ── ESTOP reason codes (RTA_STATE_RPT estop_reason) ───────────────
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

// ── EGAS Level 2 (architecture.md §7) ─────────────────────────────
constexpr int kEgasMismatchMmps = 500;   // |setpoint - feedback| > this
constexpr int kEgasMismatchMs   = 500;   // for this long -> ESTOP

// ── MTR fault flags (0x206 byte 3, shared with MTR/SYS) ───────────
// Reused directly from shared::kMtrFault*.

// ── Steering following-error threshold ─────────────────────────────
// threshold_deg = max(kSteerFollowingErrMinDeg, kSteerFollowingErrFactor
//                     * dynamic_limit_deg); persist kSteerFollowingErrMs.
constexpr float kSteerFollowErrMinDeg = 2.0f;

// ── ESTOP rate limiting (architecture.md §2.3) ────────────────────
// shared::kEstopBroadcastMinIntervalUs = 250 ms per ECU.

}  // namespace rta
