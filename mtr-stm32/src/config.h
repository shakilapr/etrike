#pragma once
// MTR STM32 — Motor controller configuration.
// CAN protocol definitions come from the canonical root protocol.
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).
// Dedicated motor actuation: throttle DAC, gear MOSFETs, ADC, TLP281 sense.
// ESTOP wired direct — cuts throttle/gear locally, no CAN dependency.
//
// Pin encoding: (port_number * 16 + pin_number)
//   0-15  = PORTA (PA0-PA15)
//   16-31 = PORTB (PB0-PB15)
//   32-47 = PORTC (PC0-PC15)
// Helper macros GPIO_PORT(p) and GPIO_PIN(p) are defined in the HAL support
// header; implementation files use these to map to STM32 HAL calls.

#include <cstdint>
#include "shared_config.h"
#ifndef TESTING
#include "protocol/generated/cpp/etrike_protocol.hpp"

namespace mtr {

namespace messages = etrike::protocol::generated;

enum class Mode : uint8_t {
    Manual = messages::RtStateRpt::kModeManual,
    Auto = messages::RtStateRpt::kModeAuto,
    Estop = messages::RtStateRpt::kModeEstop,
};

enum class Gear : uint8_t {
    N = messages::RtDriveCmd::kGearN,
    D = messages::RtDriveCmd::kGearD,
    S = messages::RtDriveCmd::kGearS,
    R = messages::RtDriveCmd::kGearR,
};

#else
// Stubs for native testing where the host GCC is too old for C++17 inline variables
namespace mtr {
enum class Mode : uint8_t {
    Manual = 0,
    Auto = 1,
    Estop = 2,
};
enum class Gear : uint8_t {
    N = 0,
    D = 1,
    S = 2,
    R = 3,
};
#endif

// ── Throttle — MCP4725 I2C DAC (0-5V) + ADC read ─────────────────
constexpr int      kThrottleI2cSda      = 7;    // PA7 (0*16+7) — SW I2C SDA
constexpr int      kThrottleI2cScl      = 5;    // PA5 (0*16+5) — SW I2C SCL
constexpr uint8_t  kThrottleDacI2cAddr  = 0x61; // MCP4725, A0 tied to VCC
constexpr unsigned kThrottleDeadZone    = 200;   // raw ADC counts
constexpr int      kThrottleMaxSpeedMmps= 3000;
constexpr int      kThrottleDacMaxVal   = 4095;  // 12-bit DAC

// ── Gear — Logic Decoder (74HC139 or similar) ───────────────────────
constexpr int kGearDecA    = 0;   // PA0 — Decoder input A
constexpr int kGearDecB    = 1;   // PA1 — Decoder input B
constexpr int kGearDecEn   = 16;  // PB0 — Decoder enable (active-low)

// ── ESTOP — direct-wired from dashboard button ────────────────────
// NC (normally-closed), active-low, pull-up.
// When button is pressed, GPIO goes LOW.
// Shared with SYS GPIO1 (separate MCU, different physical pin).

// ── Timing ────────────────────────────────────────────────────────
constexpr int kControlLoopHz       = 100;   // 10 ms — main motor control
constexpr int kSafetyCheckHz       = 20;    // ESTOP GPIO + staleness
constexpr int kCanTxLoopHz         = 100;   // base rate for CAN TX task

// ── Timeouts ──────────────────────────────────────────────────────
#ifndef TESTING
constexpr int kCmdStaleTimeoutMs   = messages::RtDriveCmd::kCycleMs * 5;
#else
constexpr int kCmdStaleTimeoutMs   = 10 * 5;
#endif

constexpr int kStartupGracePeriodMs = 3000; // mask checks at boot

// ── Gear safety ───────────────────────────────────────────────────
constexpr int kGearSwitchMaxSpeedMmps = 50;  // max speed for safe gear change (mm/s)

// ── Fault flags (bit positions in 0x206 fault_flags byte) ─────────
// Gap #15: Canonical definitions in shared/shared_config.h (shared::kMtrFault*).
// Local aliases retained for MTR code compatibility.
constexpr uint8_t kFaultEstopActive   = shared::kMtrFaultEstopActive;
constexpr uint8_t kFaultCmdTimeout    = shared::kMtrFaultCmdTimeout;

}  // namespace mtr
