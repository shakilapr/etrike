#pragma once
// RM-ESP32-T12D — Receiver Module Hardware & Timing Configuration
// RadioLink T12D transmitter + RadioLink R16F V1.0 receiver over SBUS.

#include <cstdint>
#include "shared_config.h"
#include "protocol/compat/can.hpp"

namespace rm {

constexpr const char* kFirmwareVersion = "v0.8.0-clean-rm-t12d";

// ── CAN Bus (Built-in TWAI on Low-CAN) ─────────────────────────────
constexpr int kCanBitrateHz = 500'000;
constexpr int kCanTxGpio    = 5;   // CTX = GPIO 5
constexpr int kCanRxGpio    = 4;   // CRX = GPIO 4

// ── SBUS Serial Port Configuration ─────────────────────────────────
// R16F CH16 (S.BUS output) connects to MCU UART RX pin.
// 100,000 baud, 8 data bits, Even parity, 2 stop bits, Inverted signal.
constexpr int kSbusUartPort   = 1;       // UART1
constexpr int kSbusRxGpio     = 16;      // Single-wire SBUS RX pin
constexpr int kSbusTxGpio     = -1;      // Unused (-1)
constexpr int kSbusBaudRate   = 100'000;
constexpr bool kSbusInverted  = true;    // Hardware UART RX inversion

// ── SBUS Channels ──────────────────────────────────────────────────
constexpr uint8_t kNumSbusChannels = 16;  // SBUS wire protocol frame capacity

// Active Channel Indices (0-based):
constexpr uint8_t kChSteering      = 0;   // CH1: Right Stick Horizontal (Steering +/-45.0 deg)
constexpr uint8_t kChBrake         = 1;   // CH2: Right Stick Vertical (Service Brake: 0 to 27mm stroke)
constexpr uint8_t kChThrottle      = 2;   // CH3: Left Stick Vertical (Throttle: 0 to 100%)
constexpr uint8_t kChDriveEnable   = 4;   // CH5: SWA 2-Position Switch (Drive Enable: UP=OFF, DOWN=ON)
constexpr uint8_t kChParkHold      = 5;   // CH6: SWB 2-Position Switch (Park / Brake Hold: UP=OFF, DOWN=HOLD)
constexpr uint8_t kChGear          = 6;   // CH7: SWC 3-Position Switch (Gear: UP=R, MID=N, DOWN=D)
constexpr uint8_t kChAutoRequest   = 7;   // CH8: SWD 2-Position Switch (Manual / Auto Request)
constexpr uint8_t kChAuxVra        = 8;   // CH9: VRA Knob (Aux Implement Analog: 0.0 to 1.0)
constexpr uint8_t kChAuxVrb        = 9;   // CH10: VRB Knob (Aux Implement Analog: 0.0 to 1.0)

// ── SBUS Calibration ───────────────────────────────────────────────
constexpr uint16_t kSbusRawCenter   = 992;  // 11-bit SBUS raw center (~1500us)

// Pulse Limits & Deadband
constexpr uint32_t kPulseAbsoluteMinUs  =  800;
constexpr uint32_t kPulseAbsoluteMaxUs  = 2200;
constexpr uint32_t kPulseCenterUs       = 1500;
constexpr uint32_t kPulseDeadbandUs     =   30;   // Steering center deadband (+/- 30us)

// SWC 3-Position Gear Thresholds (UP = Reverse, MID = Neutral, DOWN = Drive)
constexpr uint32_t kGearRevMaxUs        = 1300;
constexpr uint32_t kGearDriveMinUs      = 1700;

// 2-Position Switch Threshold (SWA, SWB, SWD)
constexpr uint32_t kSwitchThresholdUs   = 1500;

// Throttle Limits (CH3 Left Stick Vertical: 1050us idle to 1950us full throttle)
constexpr uint32_t kThrottleMinUs       = 1050;
constexpr uint32_t kThrottleMaxUs       = 1950;

// Brake Cutoff Interlock (mm) - motor throttle cut when brake > 5.0mm
constexpr float    kBrakeThrottleCutoffMm = 5.0f;

// ── Speed Limits (mm/s) ────────────────────────────────────────────
constexpr int32_t kSpeedFwdMaxMmps      = 3000;  // 3.00 m/s Drive Max
constexpr int32_t kSpeedRevMaxMmps      =  500;  // 0.50 m/s Reverse Limit

// ── Actuator Limits ───────────────────────────────────────────────
constexpr float kMaxSteerAngleDeg       = 45.0f;  // Mechanical rack limit (+/- 45.0 deg)
constexpr int   kSbwAngleOffset         = 30000;  // Steer-by-wire offset (0° -> 30000 raw)
constexpr int16_t kMinSteerRaw          = 29550;  // -45.0 deg full left limit (29550 raw)
constexpr int16_t kMaxSteerRaw          = 30450;  // +45.0 deg full right limit (30450 raw)
constexpr float kMaxBrakeStrokeMm       = 27.0f;  // SEB Max Emergency Stroke
constexpr float kParkBrakeStrokeMm      = 15.0f;  // SEB Park / Brake Hold Holding Stroke

// ── Timing & Link Status ──────────────────────────────────────────
constexpr int kRcCaptureHz              = 100;    // Up to 100 Hz event-driven capture
constexpr int kCanTxHz                  = 100;    // 10 ms loop (aligns with RtDriveCmd 10ms cycle)
constexpr int kHeartbeatHz              = 10;     // 100 ms loop
constexpr uint32_t kLinkLostTimeoutMs   = 150;    // >150 ms without frame = Lost (safe stop)

// ── Serial Telemetry & CAN Display Filtering ───────────────────────
constexpr int   kCanLogDecimation       = 50;     // 50 * 10ms = 500ms (2 Hz periodic summary)
constexpr float kLogDeltaSteerDeg       = 1.0f;   // Immediate log if steer changes >= 1.0 deg
constexpr float kLogDeltaBrakeMm        = 0.5f;   // Immediate log if brake changes >= 0.5 mm
constexpr int32_t kLogDeltaSpeedMmps    = 50;     // Immediate log if speed changes >= 50 mm/s

}  // namespace rm
