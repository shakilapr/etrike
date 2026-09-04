#pragma once
// RM-ESP32 — Receiver Module Hardware & Timing Configuration
// Pin-to-pin wiring strictly matches architecture.md §2.

#include <cstdint>
#include "shared_config.h"
#include "protocol/compat/can.hpp"

namespace rm {

constexpr const char* kFirmwareVersion = "v0.8.0-alpha-rm";

// ── CAN Bus (Built-in TWAI on Low-CAN) ─────────────────────────────
constexpr int kCanBitrateHz = 500'000;
constexpr int kCanTxGpio    = 21;
constexpr int kCanRxGpio    = 22;

// ── RMT RC Inputs (6 PWM Channels from RC Receiver) ───────────────
constexpr int kRcSteerGpio     = 18;  // CH0 / Pin 1: Right Stick Horizontal (Steering +/-45 deg)
constexpr int kRcBrakeGpio     = 19;  // CH1 / Pin 2: Right Stick Vertical (Brake Stroke 0..27mm)
constexpr int kRcThrottleGpio  = 14;  // CH2 / Pin 3: Left Stick Vertical (Throttle 0..100%)
constexpr int kRcSpareGpio     = 32;  // CH3 / Pin 4: Left Stick Horizontal (Spare / Unused)
constexpr int kRcIgnitionGpio  = 13;  // CH4 / Pin 5: SWB 2-Position Switch (Ignition OFF/ON)
constexpr int kRcGearGpio      =  4;  // CH5 / Pin 6: SWC 3-Position Switch (Gear R/N/D)

constexpr uint8_t kNumRcChannels = 6;

// ── FlySky FS-i6 Pulse Width Calibration (Microseconds) ───────────
constexpr uint32_t kPulseMinValidUs     =  900;
constexpr uint32_t kPulseMaxValidUs     = 2100;
constexpr uint32_t kPulseCenterUs       = 1500;
constexpr uint32_t kPulseDeadbandUs     =   30;  // Center deadband (+/- 30us)

// SWC 3-Position Switch Ranges (Gear Selector)
// UP = Reverse, MID = Park/Neutral, DOWN = Drive
constexpr uint32_t kGearRevMaxUs        = 1300;
constexpr uint32_t kGearParkMinUs       = 1350;
constexpr uint32_t kGearParkMaxUs       = 1650;
constexpr uint32_t kGearDriveMinUs      = 1700;

// SWB 2-Position Switch (Ignition)
constexpr uint32_t kIgnitionThresholdUs = 1500;

// ── Actuator Limits ───────────────────────────────────────────────
constexpr float kMaxSteerAngleDeg       = 45.0f;  // Mechanical rack limit (+/- 45.0 deg)
constexpr int   kSbwAngleOffset         = 30000;  // Steer-by-wire vendor offset (0° -> 30000 raw)
constexpr int16_t kMinSteerRaw          = 29550;  // -45.0 deg full left limit (29550 raw)
constexpr int16_t kMaxSteerRaw          = 30450;  // +45.0 deg full right limit (30450 raw)
constexpr float kMaxBrakeStrokeMm       = 27.0f;  // SEB Max Emergency Stroke
constexpr float kManualBrakeStrokeMm    = 15.0f;  // SEB Manual Pull Reference

constexpr uint32_t kThrottleMinUs       = 1050;   // Throttle idle threshold (deadband)
constexpr uint32_t kThrottleMaxUs       = 1950;   // Throttle full power threshold

// ── Timing & Rates ────────────────────────────────────────────────
constexpr int kRcCaptureHz              = 50;     // 20 ms loop (matches 50Hz RC frame)
constexpr int kCanTxHz                  = 50;     // 20 ms loop
constexpr int kHeartbeatHz              = 10;     // 100 ms
constexpr int kSignalLossTimeoutMs      = 100;    // Signal deadman threshold

}  // namespace rm
