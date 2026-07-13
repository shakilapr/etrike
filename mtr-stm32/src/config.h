#pragma once
// MTR STM32 — Motor controller configuration.
// CAN protocol IDs are in shared/can/can_protocol.h (namespace can).
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

namespace mtr {

// ── Throttle — MCP4725 I2C DAC (0-5V) + ADC read ─────────────────
constexpr int      kThrottleI2cSda      = 23;   // PB7 (1*16+7) — I2C1 SDA
constexpr int      kThrottleI2cScl      = 22;   // PB6 (1*16+6) — I2C1 SCL
constexpr uint8_t  kThrottleDacI2cAddr  = 0x61; // MCP4725, A0 tied to VCC
constexpr unsigned kThrottleDeadZone    = 200;   // raw ADC counts
constexpr int      kThrottleMaxSpeedMmps= 3000;
constexpr int      kThrottleDacMaxVal   = 4095;  // 12-bit DAC

// ── Gear — TLP281 optoisolator input + MOSFET output (72V) ────────
// TLP281 inputs: active-low (72V present = opto pulls GPIO LOW)
constexpr int kGearDSense = 16;  // PB0 (16+0) — TLP281 ch1
constexpr int kGearSSense = 17;  // PB1 (16+1) — TLP281 ch2
constexpr int kGearRSense = 18;  // PB2 (16+2) — TLP281 ch3
// MOSFET outputs: HIGH = gate driven = 72V passed through
constexpr int kGearDOut   = 3;   // PA3 —  MOSFET ch1
constexpr int kGearSOut   = 4;   // PA4 —  MOSFET ch2
constexpr int kGearROut   = 5;   // PA5 —  MOSFET ch3

// ── ESTOP — direct-wired from dashboard button ────────────────────
// NC (normally-closed), active-low, pull-up.
// When button is pressed, GPIO goes LOW.
// Shared with SYS GPIO1 (separate MCU, different physical pin).

// ── Timing ────────────────────────────────────────────────────────
constexpr int kControlLoopHz       = 100;   // 10 ms — main motor control
constexpr int kSafetyCheckHz       = 20;    // ESTOP GPIO + staleness
constexpr int kCanTxLoopHz         = 100;   // base rate for CAN TX task

// ── Timeouts ──────────────────────────────────────────────────────
constexpr int kCmdStaleTimeoutMs   = 200;   // 0x204 staleness (20 missed frames at 100 Hz → 200ms)
constexpr int kStartupGracePeriodMs = 3000; // mask checks at boot

// ── Gear safety ───────────────────────────────────────────────────
constexpr int kGearSwitchMaxSpeedMmps = 50;  // max speed for safe gear change (mm/s)

// ── Fault flags (bit positions in 0x206 fault_flags byte) ─────────
// Gap #15: Canonical definitions in shared/shared_config.h (shared::kMtrFault*).
// Local aliases retained for MTR code compatibility.
constexpr uint8_t kFaultEstopActive   = shared::kMtrFaultEstopActive;
constexpr uint8_t kFaultCmdTimeout    = shared::kMtrFaultCmdTimeout;

}  // namespace mtr
