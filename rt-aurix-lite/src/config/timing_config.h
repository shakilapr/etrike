#pragma once
// RT-AURIX-Lite timing configuration — periods, timeouts, heartbeat.
// Split by concern per architecture.md §12. Values reused from
// shared/shared_config.h where semantically identical.

#include <cstdint>

namespace rta {

// ── Functional-unit periods (architecture.md §6.3) ────────────────
constexpr int kControlLoopHz   = 100;   // CPU1 control
constexpr int kSafetyRateHz    = 20;    // CPU1 safety
constexpr int kHealthRateHz    = 10;    // CPU1 watchdog/health
constexpr int kHeartbeatRateHz = 2;     // CPU0 heartbeat

// ── Mode manager (architecture.md §5) ─────────────────────────────
constexpr int kModeDebounceMs   = 500;   // button debounce
constexpr int kModeLongPressMs  = 3000;  // MODE long-press in ESTOP -> MANUAL
constexpr int kModeTickRateHz   = 10;    // mode manager call rate

// ── Heartbeat (architecture.md §8) ────────────────────────────────
constexpr int kHeartbeatId          = 0x7FD;
constexpr int kHeartbeatIntervalMs  = 500;   // 2 Hz
constexpr int kHeartbeatTimeoutMsMtr    = 200;   // implicit via 0x206
constexpr int kHeartbeatTimeoutMsJetson = 1500;  // 0x7FC
constexpr int kHostCmdStaleTimeoutMs    = 500;   // command staleness
constexpr int kStartupGracePeriodMs     = 3000;  // suppress checks at boot

// ── CAN ───────────────────────────────────────────────────────────
constexpr int kCanLowBitrateHz  = 500000;
constexpr int kCanHighBitrateHz = 500000;

}  // namespace rta
