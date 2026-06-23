#pragma once
// SYS ESP32-S3 — safety & actuator configuration (architecture.md §8.9).
// Change values here, not in source files.
//
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).

#include <cstdint>
#include "shared_config.h"

namespace sys {

// ── CAN — low-level only (built-in TWAI) ──────────────────────────
constexpr int kCanBitrateHz = 500'000;
constexpr int kCanTxGpio    =       5;
constexpr int kCanRxGpio    =       4;

// ── safety inputs ─────────────────────────────────────────────────
constexpr int kEstopGpio      =  1;   // big red mushroom, NC, active-low, pull-up
constexpr int kBrakeLeverGpio =  2;   // active-low, pull-up
constexpr int kStartBtnGpio   = 32;   // green momentary, exits ESTOP→MANUAL
constexpr int kModeBtnGpio    = 11;   // momentary, toggles MANUAL↔AUTO

// ── throttle — MCP4725 I2C DAC (0–5V) + ADC read ─────────────────
constexpr int      kThrottleAdcChannel  =  5;   // ADC1_CH5 → GPIO10
constexpr int      kThrottleI2cSda      = 15;
constexpr int      kThrottleI2cScl      = 16;
constexpr uint8_t  kThrottleDacI2cAddr  = 0x60; // MCP4725
constexpr unsigned kThrottleDeadZone    =  200;
constexpr int      kThrottleMaxSpeedMmps= 3000;
constexpr int      kThrottleDacMaxVal   = 4095; // 12-bit, VCC=5V → 0–5V

// ── gear — TLP281 optoisolator input + relay output (72V) ────────
constexpr int kGearDSense = 12, kGearSSense = 13, kGearRSense = 14;
constexpr int kGearDOut   = 33, kGearSOut   = 34, kGearROut   = 35;

// ── signal lights — handlebar switch inputs ──────────────────────
constexpr int kSwitchLeftTurn  =  3;
constexpr int kSwitchRightTurn =  6;
constexpr int kSwitchHeadlight =  7;

// ── signal lights — relay lamp outputs ────────────────────────────
constexpr int kLightLeftTurn  = 18;
constexpr int kLightRightTurn = 19;
constexpr int kLightBrake     = 21;
constexpr int kLightHead      = 22;

// ── mode indicator bulbs + 12V relay ─────────────────────────────
constexpr int kBulbAuto       = 25;
constexpr int kBulbManual     = 26;
constexpr int kPower12vRelay  = 27;

// ── watchdog ──────────────────────────────────────────────────────
constexpr int kWdtToggleGpio = 23;

// ── timing (ms / Hz) ─────────────────────────────────────────────
constexpr int kControlLoopHz        =  100;
constexpr int kHeartbeatIntervalMs  =  100;   // 10 Hz SYS heartbeat (fast path for brake loss detection, gap #12)
constexpr int kHeartbeatTimeoutMs   =  200;   // RT heartbeat loss (2 missed frames at 10 Hz). FTTI: 1.4m at 25 km/h.
constexpr int kSafetyCheckHz        =   20;
constexpr int kGearCheckHz          =   50;
constexpr int kDebounceMs           =  500;   // push button debounce

// ── turn signal blink ─────────────────────────────────────────────
constexpr int kTurnBlinkOnMs  = 500;
constexpr int kTurnBlinkOffMs = 500;

// ── brake — SYNTREE SEB via CAN 0x7B9 ────────────────────────────
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

// ── mode button long-press ESTOP exit (gap #11) ─────────────────────
constexpr int kEstopLongPressMs       = 3000;  // held 3s → MANUAL

}  // namespace sys
