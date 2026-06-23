#pragma once
// MTR STM32 — Motor controller configuration.
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).
// Dedicated motor actuation: throttle DAC, gear relays, ADC, TLP281 sense.
// ESTOP wired direct — cuts throttle/gear locally, no CAN dependency.

#include <cstdint>
#include "shared_config.h"

namespace mtr {

// ── CAN (low-level bus only) ──────────────────────────────────────
constexpr int kCanBitrateHz = 500'000;
// STM32 CAN pins — TBD based on STM32 variant (e.g. F103: PB8=RX, PB9=TX)

// ── Throttle — MCP4725 I2C DAC (0-5V) + ADC read ─────────────────
constexpr int      kThrottleAdcChannel  = 0;    // STM32 ADC channel, TBD
constexpr int      kThrottleI2cSda      = 0;    // STM32 I2C pin, TBD
constexpr int      kThrottleI2cScl      = 0;    // STM32 I2C pin, TBD
constexpr uint8_t  kThrottleDacI2cAddr  = 0x60; // MCP4725
constexpr unsigned kThrottleDeadZone    = 200;
constexpr int      kThrottleMaxSpeedMmps= 3000;
constexpr int      kThrottleDacMaxVal   = 4095;

// ── Gear — TLP281 optoisolator input + relay output (72V) ────────
constexpr int kGearDSense = 0;  // TBD
constexpr int kGearSSense = 0;  // TBD
constexpr int kGearRSense = 0;  // TBD
constexpr int kGearDOut   = 0;  // TBD
constexpr int kGearSOut   = 0;  // TBD
constexpr int kGearROut   = 0;  // TBD

// ── ESTOP — direct-wired from dashboard button ────────────────────
constexpr int kEstopGpio = 0;  // TBD — NC, active-low. Shared with SYS GPIO1.

// ── Timing ────────────────────────────────────────────────────────
constexpr int kControlLoopHz       = 100;
constexpr int kMotorFeedbackHz     = 50;   // 0x206 rate
constexpr int kGearCheckHz         = 50;

}  // namespace mtr
