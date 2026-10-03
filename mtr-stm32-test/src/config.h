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

// ── Test Mode Selection ────────────────────────────────────────────
// Compile-time test mode, selected by the build environment:
//   TEST_MODE 0 (HW)     — relay sequencing + DAC sine sweep (default env:vehicle)
//   TEST_MODE 1 (CAN_TX) — CAN transmit path test          (env:can_tx)
//   TEST_MODE 2 (CAN_RX) — CAN receive path test           (env:can_rx)
#ifndef TEST_MODE
#define TEST_MODE 0
#endif
constexpr int kTestMode = TEST_MODE;

// ── CAN Test Configuration (production-identical) ──────────────────
constexpr uint32_t kCanBitrateHz = 500'000;    // Classic CAN, Low bus
// Nominal bit timing — identical to mtr-stm32 production (16 MHz PCLK1
// kernel): prescaler 2, 1 + 13 + 2 = 16 Tq, SJW 2 -> 500 kbit/s at the
// 87.5% sample point (CiA 301 standard). ESP32 peers resolve to 85.0%/SJW 1.
constexpr uint32_t kCanPrescaler = 2;
constexpr uint32_t kCanSJW       = 2;
constexpr uint32_t kCanSeg1      = 13;
constexpr uint32_t kCanSeg2      = 2;

// Transmitted test frame. 0x206 matches the production telemetry ID; on the
// bench (RT offline, SYS absent) no peer reacts to a 1 Hz stream.
#ifndef CAN_TEST_FRAME_ID
#define CAN_TEST_FRAME_ID 0x206u
#endif
constexpr uint32_t kTxTestFrameId  = CAN_TEST_FRAME_ID;
constexpr uint8_t  kTxTestFrameDlc = 4;

// ── CAN Clock Source Experiment ────────────────────────────────────
// 0 = HSI (PCLK1, 16 MHz internal RC) — current production setup
// 1 = HSE (external 8 MHz crystal)    — ST-recommended for real buses
// Both yield identical wire timing (500 kbit/s, 16 Tq, 87.5 % sample
// point); only the clock accuracy differs (HSI ±1 % vs crystal ppm).
// Build the *_hse environments to compare on the bench (see TESTING.md).
#ifndef CAN_USE_HSE
#define CAN_USE_HSE 0
#endif
constexpr bool kCanUseHse = CAN_USE_HSE;

// ── CAN Test Timings ───────────────────────────────────────────────
constexpr uint32_t kTxTestPeriodMs     = 1'000;  // one test frame per second
constexpr uint32_t kTxTestSampleMs     = 10;     // verdict sampled after send completes
constexpr uint32_t kRxActivityWindowMs = 500;    // "recent traffic" window (LED solid)
constexpr uint32_t kBusOffRecoveryDebounceMs = 150;  // same debounce as production

}  // namespace mtr_test
