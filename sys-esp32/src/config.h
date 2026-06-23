#pragma once
// SYS ESP32-S3 — safety & actuator configuration (architecture.md §8.9).
// Change values here, not in source files.

#include <cstdint>

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
constexpr int kStartupGracePeriodMs = 3000;   // mask at boot
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
constexpr float kBrakeStrokeScale  =  0.05f;
constexpr float kBrakeStrokeOffset = -30.0f;

// ── CAN ID aliases (from shared/can/can_protocol.h) ───────────────
// SYS sends
constexpr uint32_t kIdSysSafetySts   = 0x011;
constexpr uint32_t kIdSysDcdcCmd     = 0x012;
constexpr uint32_t kIdSysModeCmd     = 0x110;
constexpr uint32_t kIdSysThrottleSts = 0x120;
constexpr uint32_t kIdSysDiagRpt     = 0x600;
constexpr uint32_t kIdSyntreeSebCmd      = 0x7B9;
constexpr uint32_t kIdSysHeartbeat   = 0x7FE;
// SYS receives
constexpr uint32_t kIdRtDriveCmd     = 0x204;
constexpr uint32_t kIdRtBrakeCmd     = 0x205;
constexpr uint32_t kIdHostLightCmd   = 0x302;   // forwarded by RT
constexpr uint32_t kIdSyntreeSebStatus      = 0x721;
constexpr uint32_t kIdRtHeartbeat    = 0x7FD;
// Both
constexpr uint32_t kIdSafetyEstop    = 0x001;

// ── obstacle constants (mirrored from RT, for speed_limiter.cpp) ────
constexpr unsigned kObstacleStopDistMM  = 300;
constexpr unsigned kObstacleClearDistMM = 3000;

}  // namespace sys
