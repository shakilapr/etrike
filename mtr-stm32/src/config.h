#pragma once
// MTR STM32 — Motor controller configuration.
// Dedicated motor actuation: throttle DAC, gear relays, ADC, TLP281 sense.
// ESTOP wired direct — cuts throttle/gear locally, no CAN dependency.

#include <cstdint>

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

// ── CAN ID aliases ────────────────────────────────────────────────
// MTR receives
constexpr uint32_t kIdRtDriveCmd     = 0x204;  // RT→MTR, 100 Hz
constexpr uint32_t kIdSysModeCmd     = 0x110;  // SYS→MTR, on change
constexpr uint32_t kIdSafetyEstop    = 0x001;
constexpr uint32_t kIdRtHeartbeat    = 0x7FD;
// MTR sends
constexpr uint32_t kIdSysThrottleSts = 0x120;  // MTR→RT/SYS, 100 Hz
constexpr uint32_t kIdMotorFbk       = 0x206;  // MTR→SYS/RT, 50 Hz

}  // namespace mtr
