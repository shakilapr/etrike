#pragma once
// MTR STM32G431 — Hardware & Timing Configuration
// Pinout strictly preserves mtr-stm/architecture.md §6 and physical wiring specs.

#include <cstdint>
#include "protocol/compat/can.hpp"
#include "shared_config.h"

namespace mtr {

// ── CAN Bus (Classic CAN @ 500 kbit/s) ────────────────────────────
constexpr int kCanBitrateHz = 500'000;
// Pure receive-only mode (strictly matches mtr-stm architecture §1, §6, §13).
// MTR hardware transceiver is receive-only. Attempting to transmit triggers
// bus bit errors on PA12, causing Bus-Off and tripping the 150ms relay watchdog.
constexpr bool kEnableCanBroadcast = false;
// PA11: FDCAN1_RX (AF9)
// PA12: FDCAN1_TX (AF9)

// ── Relay Control Outputs (Active-Low) ─────────────────────────────
// Active-Low: GPIO RESET = Relay ON (Energized), GPIO SET = Relay OFF (De-energized)
constexpr uint16_t kRelayRevPin      = 0x0001;  // PA0: Mode Reverse Relay
constexpr uint16_t kRelayDrivePin    = 0x0004;  // PA2: Mode Drive Relay
constexpr uint16_t kRelayIgnitionPin = 0x0010;  // PA4: Ignition Relay

// ── Software I2C Pins for MCP4725 DAC ──────────────────────────────
constexpr uint16_t kI2cSclPin        = 0x0020;  // PA5: I2C Clock (Open-drain)
constexpr uint16_t kI2cSdaPin        = 0x0080;  // PA7: I2C Data (Open-drain)

// ── Status LED ─────────────────────────────────────────────────────
constexpr uint16_t kLedPin           = 0x0040;  // PC6: Status LED (Active-Low)

// ── Throttle Voltage Calibration (Volts) ──────────────────────────
// EDIT HERE: Set desired forward and reverse throttle limits directly in Volts.
constexpr float kDacVref             = 5.0f;   // MCP4725 VCC reference voltage

constexpr float kDacFwdMinVolts      = 1.45f;  // Forward min voltage (at 50 mm/s)
constexpr float kDacFwdMaxVolts      = 1.80f;  // Forward max voltage (at 3000 mm/s)

constexpr float kDacRevMinVolts      = 1.55f;  // Reverse min voltage (at 50 mm/s)
constexpr float kDacRevMaxVolts      = 2.00f;  // Reverse max voltage (at 500 mm/s)

// Helper: compile-time conversion from Volts to 12-bit DAC code (0 .. 4095)
constexpr uint16_t volts_to_dac_code(float volts) {
    if (volts <= 0.0f) return 0;
    float code = (volts / kDacVref) * 4096.0f;
    return static_cast<uint16_t>(code + 0.5f);
}

// ── MCP4725 DAC Limits (at 5.0 V VCC reference) ───────────────────
// Absolute hardware safety clamp window:
// 0.8 V -> Code 655
// 2.4 V -> Code 1966 (Hardware absolute maximum)
constexpr uint16_t kDacMinCode       =  655;
constexpr uint16_t kDacMaxCode       = 1966;
constexpr uint16_t kDacZeroCode      =    0;  // 0.0 V output

// Forward drive throttle window (automatically computed from Volts above)
constexpr uint16_t kDacFwdMinCode    = volts_to_dac_code(kDacFwdMinVolts);
constexpr uint16_t kDacFwdMaxCode    = volts_to_dac_code(kDacFwdMaxVolts);

// Reverse drive throttle window (automatically computed from Volts above)
constexpr uint16_t kDacRevMinCode    = volts_to_dac_code(kDacRevMinVolts);
constexpr uint16_t kDacRevMaxCode    = volts_to_dac_code(kDacRevMaxVolts);

// Speed scaling
constexpr int32_t  kMaxForwardSpeedMmps = 3000; // 3.0 m/s maximum speed setpoint
constexpr int32_t  kMaxReverseSpeedMmps =  500; // 0.5 m/s maximum reverse setpoint

// ── Timing & Rates ────────────────────────────────────────────────
constexpr uint32_t kWatchdogTimeoutMs =  500;  // 500 ms allowed CAN silence before shutdown
constexpr uint32_t kFeedbackPeriodMs  =   20;  // 50 Hz for 0x206 MTR_MOTOR_FBK
constexpr uint32_t kThrottlePeriodMs  =   10;  // 100 Hz for 0x120 SYS_THROTTLE_STS
constexpr uint32_t kMainLoopPeriodMs  =    5;  // 5 ms main loop evaluation

// ── Dedicated 0x204 RT_DRIVE_CMD watchdog (issue #2) ──────────────
// The drive command is the actual propulsion authority. It is supervised
// separately from the generic "any CAN frame within 500 ms" deadman so a
// frozen/stale 0x204 stream cannot keep a last throttle command applied
// while unrelated authority traffic (0x110/0x113/0x011) keeps the generic
// watchdog alive.
constexpr uint32_t kDriveCmdTimeoutMs     = 150;  // no valid 0x204 for 150 ms while drive is expected
// Recovery (confirmed): require kDriveCmdRecoverFrames consecutive valid
// 0x204 receive events, each separated by at most kDriveCmdRecoverMaxGapMs.
constexpr uint32_t kDriveCmdRecoverFrames =   3;
constexpr uint32_t kDriveCmdRecoverMaxGapMs = 100;

}  // namespace mtr
