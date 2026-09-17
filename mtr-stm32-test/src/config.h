#pragma once
// MTR STM32G431 — Hardware Test Configuration
// Pinout strictly identical to mtr-stm32 hardware specifications.

#include <cstdint>

namespace mtr_test {

// ── Relay Control Outputs (Active-Low) ─────────────────────────────
// Logic: GPIO RESET = Relay ON (Energized), GPIO SET = Relay OFF (De-energized)
constexpr uint16_t kRelayRevPin      = 0x0001;  // PA0: Mode Reverse Relay
constexpr uint16_t kRelayDrivePin    = 0x0004;  // PA2: Mode Drive Relay
constexpr uint16_t kRelayIgnitionPin = 0x0010;  // PA4: Ignition Relay

// ── Status LED (Active-Low) ─────────────────────────────────────────
// Logic: GPIO RESET = LED ON, GPIO SET = LED OFF
constexpr uint16_t kLedPin           = 0x0040;  // PC6: Status LED

// ── Software I2C Pins for MCP4725 DAC ──────────────────────────────
constexpr uint16_t kI2cSclPin        = 0x0020;  // PA5: I2C Clock (Open-drain)
constexpr uint16_t kI2cSdaPin        = 0x0080;  // PA7: I2C Data (Open-drain)

// MCP4725 full 12-bit rail-to-rail: 0 to 4095 (0.0 V to 5.0 V)
#ifndef DAC_MAX_CODE
constexpr uint16_t kDacMaxCode       = 4095;    // 0.0 V to 5.0 V full rail
#else
constexpr uint16_t kDacMaxCode       = DAC_MAX_CODE;
#endif

constexpr uint16_t kDacZeroCode      = 0;       // 0.0 V

// Sweep profile: 0 to max in 15 seconds, max to 0 in 15 seconds (30s period)
constexpr uint32_t kSweepHalfPeriodMs = 15'000; // 15 seconds up / 15 seconds down
constexpr uint32_t kSweepTotalPeriodMs = 2 * kSweepHalfPeriodMs; // 30 seconds
constexpr uint32_t kDacUpdateIntervalMs = 20;   // 50 Hz DAC modulation rate

// ── Relay Sequencing Timings ───────────────────────────────────────
// Sequentially pulses: Ignition -> Drive -> Reverse in order
constexpr uint32_t kRelayOnTimeMs    = 2'000;   // 2.0 s ON per relay
constexpr uint32_t kRelayOffTimeMs   = 1'000;   // 1.0 s gap between relays

}  // namespace mtr_test
