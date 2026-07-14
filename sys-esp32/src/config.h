#pragma once
// SYS ESP32-S3 — safety & actuator configuration (architecture.md §8.9).
// Change values here, not in source files.
//
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).

#include <cstdint>
#include "shared_config.h"
#include "can/generated/can_messages.h"


namespace sys {

// ── CAN — low-level only (built-in TWAI) ──────────────────────────
constexpr int kCanBitrateHz = 500'000;
constexpr int kCanTxGpio    =       5;
constexpr int kCanRxGpio    =       4;

// ── safety inputs ─────────────────────────────────────────────────
// NC contact must connect this pin to 3.3 V when healthy; an external 10 kOhm
// pull-down makes an open wire or pressed button read LOW (ESTOP active).
constexpr int kEstopGpio      =  1;   // big red mushroom, NC, active-low, pull-down
constexpr int kBrakeLeverGpio =  2;   // active-low, pull-up
constexpr int kStartBtnGpio   = 41;   // green momentary — ESTOP exit to MANUAL
constexpr int kModeBtnGpio    = 11;   // momentary, toggles MANUAL↔AUTO
constexpr int kIgnitionGpio   =  8;   // reserved; production firmware does not drive it

// ── signal lights — handlebar switch inputs ──────────────────────
constexpr int kSwitchLeftTurn  =  9;  // moved from GPIO3 (ESP32-S3 JTAG strapping pin)
constexpr int kSwitchRightTurn =  6;
constexpr int kSwitchHeadlight =  7;

// ── signal lights — relay lamp outputs ────────────────────────────
constexpr int kLightLeftTurn  = 18;
constexpr int kLightRightTurn = 19;
constexpr int kLightBrake     = 21;
constexpr int kLightHead      = 10;

// ── mode indicator bulbs + 12V relay ─────────────────────────────
constexpr int kBulbAuto       = 48;
constexpr int kBulbManual     = 39;
constexpr int kBulbReady      = 17;  // green — system ready (AUTO/MANUAL, RT alive, no faults)
constexpr int kBulbEstop      = 20;  // red — dedicated ESTOP indicator
constexpr int kPower12vRelay  = 40;

// ── watchdog ──────────────────────────────────────────────────────
// constexpr int kWdtToggleGpio = 47; // Temporarily disabled


// ── timing (ms / Hz) ─────────────────────────────────────────────
constexpr int kControlLoopHz        =  100;
constexpr int kHeartbeatIntervalMs  = can::gen::SysHeartbeat::kCycleMs;
constexpr int kHeartbeatTimeoutMsRt = can::gen::RtHeartbeat::kCycleMs * 2; // policy: two missed frames
constexpr int kSetpointStaleMs      = can::gen::RtDriveCmd::kCycleMs * 20; // policy: twenty missed frames
constexpr int kSafetyCheckHz        =   20;
constexpr int kGearCheckHz          =   50;
constexpr int kDebounceMs           =  500;   // push button debounce

// ── brake — brake-by-wire unit via CAN 0x7B9 ────────────────────────────
constexpr int   kBrakeCmdRateHz    =   50;     // 20 ms period
constexpr int   kBrakeBootWaitMs   =  500;
constexpr float kBrakeManualStroke = 15.0f;    // mm, lever pressed
constexpr float kBrakeMaxStroke    = 27.0f;    // mm, ESTOP

// ── EGAS L2 motor monitoring (architecture §6.1) ─────────────────────
constexpr int kEgasSpeedThresholdMmps = 500;   // abs(cmd - actual) > 500 mm/s
constexpr int kEgasFaultDurationMs    = 500;   // persist 500ms → ESTOP

// ── brake following error (architecture §8.10) ──────────────────────
constexpr int   kBrakeFollowingErrRaw = 60;    // 3mm in raw units (3 / 0.05)
constexpr int   kBrakeFollowingErrMs  = 100;   // persist 100ms → log error

// ── SEB status staleness (architecture §8.10) ───────────────────────
constexpr int kSebStatusTimeoutMs     = 100;   // no 0x721 for 100ms → log warning
constexpr int kSebRollingTimeoutMs    = 100;   // 0x721 rolling counter may repeat at 100Hz; must advance within 100ms

// ── mode button long-press ESTOP exit (gap #11) ─────────────────────
constexpr int kEstopLongPressMs       = 3000;  // held 3s → MANUAL

// ── MTR ESTOP ACK (gap #15) ──────────────────────────────────────────
constexpr int kMtrEstopAckTimeoutMs   =  100;  // ESTOP_ACTIVE bit in 0x206 within 100ms

// ── 0x206 staleness (gap #15) ────────────────────────────────────────
constexpr int kMtrFbkStaleMs          =  200;  // MTR comms lost if no 0x206 for 200ms

// ── 0x001 ESTOP rate limiting (gap #14) ──────────────────────────────
constexpr int kEstopRateLimitWindowMs =  500;  // rolling window
constexpr int kEstopRateLimitMax      =    2;  // max 0x001 frames per window

}  // namespace sys
