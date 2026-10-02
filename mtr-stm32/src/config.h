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

// ── Bench TX diagnostic probe ─────────────────────────────────────
// When true, the firmware sends one 0x206 frame per second and reports the
// FDCAN transmit outcome on the PC6 status LED — the only output channel
// available while the TX path itself is broken (a failing frame can never
// carry its own error report onto the bus):
//   Solid ON   — frame transmitted, TEC unchanged, no LEC error (TX proven)
//   Slow 1 Hz  — LEC=3 ACK error (frame reached the bus, nobody acknowledged)
//   Fast 5 Hz  — bus-level error (LEC 1/2/4/5/6: stuff/form/bit/CRC —
//                transceiver TX stage dead, silent, or miswired)
//   Dark       — Bus-Off (judge >=3 s after power-on; LED starts solid)
// Probe builds reserve the PC6 LED for the probe (relay-transition toggling
// is compiled out) and keep the 0x631 diagnostic drain disabled. The probe
// transmits at 1 Hz, violating the 0x206 20 ms contract — keep RT offline
// (bench topology 2) or expect RT-side MTR feedback staleness flags.
// Must be false in vehicle builds; bench use only.
constexpr bool kTxProbe = false;

}  // namespace mtr
