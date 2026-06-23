#pragma once
// MTR STM32 — Motor controller configuration.
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
// Vehicle-wide constants are in shared/shared_config.h (namespace shared).
// Dedicated motor actuation: throttle DAC, gear relays, ADC, TLP281 sense.
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

namespace mtr {

// ── CAN (low-level bus only, STM32 bxCAN1) ────────────────────────
constexpr int kCanBitrateHz = 500'000;
constexpr int kCanRxGpio    = 24;  // PB8 (16+8) — CAN1_RX
constexpr int kCanTxGpio    = 25;  // PB9 (16+9) — CAN1_TX

// ── Throttle — MCP4725 I2C DAC (0-5V) + ADC read ─────────────────
constexpr int      kThrottleAdcChannel  = 0;     // ADC1_IN0  (PA0)
constexpr int      kThrottleI2cSda      = 23;    // PB7 (16+7) — I2C1_SDA
constexpr int      kThrottleI2cScl      = 22;    // PB6 (16+6) — I2C1_SCL
constexpr uint8_t  kThrottleDacI2cAddr  = 0x60;  // MCP4725 (A0=GND)
constexpr unsigned kThrottleDeadZone    = 200;   // raw ADC counts
constexpr int      kThrottleMaxSpeedMmps= 3000;
constexpr int      kThrottleDacMaxVal   = 4095;  // 12-bit DAC

// ── Gear — TLP281 optoisolator input + relay output (72V) ────────
// TLP281 inputs: active-low (72V present = opto pulls GPIO LOW)
constexpr int kGearDSense = 16;  // PB0 (16+0) — TLP281 ch1
constexpr int kGearSSense = 17;  // PB1 (16+1) — TLP281 ch2
constexpr int kGearRSense = 18;  // PB2 (16+2) — TLP281 ch3
// Relay outputs: HIGH = relay energized = 72V passed through
constexpr int kGearDOut   = 3;   // PA3 — relay ch1
constexpr int kGearSOut   = 4;   // PA4 — relay ch2
constexpr int kGearROut   = 5;   // PA5 — relay ch3

// ── ESTOP — direct-wired from dashboard button ────────────────────
// NC (normally-closed), active-low, pull-up.
// When button is pressed, GPIO goes LOW.
// Shared with SYS GPIO1 (separate MCU, different physical pin).
constexpr int kEstopGpio = 1;  // PA1

// ── Timing ────────────────────────────────────────────────────────
constexpr int kControlLoopHz       = 100;   // 10 ms — main motor control
constexpr int kThrottleStsRateHz   = 100;   // 0x120 SYS_THROTTLE_STS
constexpr int kMotorFeedbackHz     = 50;    // 0x206 MTR_MOTOR_FBK
constexpr int kSafetyCheckHz       = 20;    // ESTOP GPIO + staleness
constexpr int kGearCheckHz         = 50;
constexpr int kCanTxLoopHz         = 100;   // base rate for CAN TX task

// ── Timeouts ──────────────────────────────────────────────────────
constexpr int kCmdStaleTimeoutMs   = 200;   // 0x204 staleness (2 frames at 100 Hz)
constexpr int kStartupGracePeriodMs = 3000; // mask checks at boot

// ── Fault flags (bit positions in 0x206 fault_flags byte) ─────────
constexpr uint8_t kFaultEstopActive   = 0x01;  // ESTOP confirmed active
constexpr uint8_t kFaultCmdTimeout    = 0x02;  // 0x204 stale in AUTO
constexpr uint8_t kFaultAdcFault      = 0x04;  // ADC read failure
constexpr uint8_t kFaultGearConflict  = 0x08;  // multiple gear lines active

}  // namespace mtr
