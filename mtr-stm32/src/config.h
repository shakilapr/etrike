#pragma once
// MTR STM32G431 — Hardware & Timing Configuration
// Pinout strictly preserves mtr-stm/architecture.md §6 and physical wiring specs.

#include <cstdint>
#include "protocol/compat/can.hpp"
#include "shared_config.h"

namespace mtr {

// ── CAN Bus (FDCAN1 Classic CAN @ 500 kbit/s) ──────────────────────
constexpr int kCanBitrateHz = 500'000;
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

// ── MCP4725 DAC Limits (at 5.0 V VCC reference) ───────────────────
// 0.8 V -> Code 655
// 2.4 V -> Code 1966 (Safety limit)
constexpr uint16_t kDacMinCode       =  655;
constexpr uint16_t kDacMaxCode       = 1966;
constexpr uint16_t kDacZeroCode      =    0;  // 0.0 V output

// Speed scaling
constexpr int32_t  kMaxForwardSpeedMmps = 3000; // 3.0 m/s maximum speed setpoint
constexpr int32_t  kMaxReverseSpeedMmps =  500; // 0.5 m/s maximum reverse setpoint

// ── Timing & Rates ────────────────────────────────────────────────
constexpr uint32_t kWatchdogTimeoutMs =  500;  // 500 ms allowed CAN silence before shutdown
constexpr uint32_t kFeedbackPeriodMs  =   20;  // 50 Hz for 0x206 MTR_MOTOR_FBK
constexpr uint32_t kThrottlePeriodMs  =   10;  // 100 Hz for 0x120 SYS_THROTTLE_STS
constexpr uint32_t kMainLoopPeriodMs  =    5;  // 5 ms main loop evaluation

}  // namespace mtr
