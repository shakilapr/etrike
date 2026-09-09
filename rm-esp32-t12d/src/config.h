#pragma once
// RM-ESP32-T12D — Receiver Module Hardware & Timing Configuration
// RadioLink T12D transmitter + RadioLink R16F V1.0 receiver over SBUS.

#include <cstdint>
#include "shared_config.h"
#include "protocol/compat/can.hpp"

namespace rm {

constexpr const char* kFirmwareVersion = "v0.8.0-alpha-rm-t12d";

// ── CAN Bus (Built-in TWAI on Low-CAN) ─────────────────────────────
constexpr int kCanBitrateHz = 500'000;
constexpr int kCanTxGpio    = 21;
constexpr int kCanRxGpio    = 22;

// ── SBUS Serial Port Configuration ─────────────────────────────────
// R16F CH16 (S.BUS output) connects to MCU UART RX pin.
// Standard SBUS: 100,000 baud, 8 data bits, Even parity, 2 stop bits, Inverted signal.
constexpr int kSbusUartPort   = 1;       // UART1
constexpr int kSbusRxGpio     = 16;      // Single-wire SBUS RX pin
constexpr int kSbusTxGpio     = -1;      // Unused (-1)
constexpr int kSbusBaudRate   = 100'000;
constexpr bool kSbusInverted  = true;    // Hardware UART RX inversion

// ── SBUS & T12D Channels ───────────────────────────────────────────
constexpr uint8_t kNumSbusChannels = 16;  // SBUS wire protocol frame capacity
constexpr uint8_t kNumT12dChannels = 12;  // T12D hardware transmitter generated channels

// Channel Indices (0-based):
constexpr uint8_t kChSteering = 0;  // CH1: Right Stick Horizontal (Steering +/-45.0 deg)
constexpr uint8_t kChBrake    = 1;  // CH2: Right Stick Vertical (Brake Stroke 0..27mm)
constexpr uint8_t kChThrottle = 2;  // CH3: Left Stick Vertical (Throttle 0..100%)
constexpr uint8_t kChYawSpare = 3;  // CH4: Left Stick Horizontal (Spare / Yaw)
constexpr uint8_t kChIgnition = 4;  // CH5: SWB 2-Position Switch (Ignition OFF/ON)
constexpr uint8_t kChGear     = 5;  // CH6: SWC 3-Position Switch (Gear R/N/D)
constexpr uint8_t kChSwitchA  = 6;  // CH7: SWA 2-Position Switch (Autonomy Override)
constexpr uint8_t kChSwitchD  = 7;  // CH8: SWD 2-Position Switch (Aux / Fast ESTOP)
constexpr uint8_t kChDialVra  = 8;  // CH9: VRA Knob (Speed Governor 0..100%)
constexpr uint8_t kChDialVrb  = 9;  // CH10: VRB Knob (Aux Trim)
constexpr uint8_t kChAux11    = 10; // CH11: Aux Channel 11
constexpr uint8_t kChAux12    = 11; // CH12: Aux Channel 12

// ── RadioLink T12D / SBUS Calibration & Conversion ────────────────
// 11-bit SBUS raw values range ~172 (1000us) to ~1811 (2000us) with center ~992 (1500us).
constexpr uint16_t kSbusRawMin      =  172;
constexpr uint16_t kSbusRawCenter   =  992;
constexpr uint16_t kSbusRawMax      = 1811;

// Calibrated Microsecond Equivalent (for seamless compatibility & tests)
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
constexpr int kRcCaptureHz              = 50;     // 20 ms loop
constexpr int kCanTxHz                  = 50;     // 20 ms loop
constexpr int kHeartbeatHz              = 10;     // 100 ms loop
constexpr int kSignalLossTimeoutMs      = 100;    // Deadman signal loss threshold

// ── Serial Telemetry & CAN Display Filtering ───────────────────────
// Prevents UART buffer flooding at 50 Hz while providing instant change feedback
constexpr int   kCanLogDecimation       = 25;     // 25 * 20ms = 500ms (2 Hz periodic summary)
constexpr float kLogDeltaSteerDeg       = 1.0f;   // Immediate log if steer changes >= 1.0 deg
constexpr float kLogDeltaBrakeMm        = 0.5f;   // Immediate log if brake changes >= 0.5 mm
constexpr int32_t kLogDeltaSpeedMmps    = 50;     // Immediate log if speed changes >= 50 mm/s

}  // namespace rm
