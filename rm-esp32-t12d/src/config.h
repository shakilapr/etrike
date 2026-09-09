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
constexpr int kCanTxGpio    = 5;   // Matches SYS & RT nodes (CTX = GPIO 5)
constexpr int kCanRxGpio    = 4;   // Matches SYS & RT nodes (CRX = GPIO 4)

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
constexpr uint8_t kChSteering      = 0;   // CH1: Right Stick Horizontal (Steering +/-45.0 deg)
constexpr uint8_t kChVelocity      = 1;   // CH2: Right Stick Vertical (Signed Velocity, Spring Centered)
constexpr uint8_t kChImplementX    = 2;   // CH3: Left Stick Horizontal (Implement / Aux X)
constexpr uint8_t kChImplementY    = 3;   // CH4: Left Stick Vertical (Implement / Aux Y)
constexpr uint8_t kChDriveEnable   = 4;   // CH5: SWA 2-Position Switch (Drive Enable Request)
constexpr uint8_t kChParkHold      = 5;   // CH6: SWB 2-Position Switch (Park / Brake Hold)
constexpr uint8_t kChDriveEnvelope = 6;   // CH7: SWC 3-Position Switch (Precision / Normal / Fast)
constexpr uint8_t kChAutoRequest   = 7;   // CH8: SWD 2-Position Switch (Manual / Auto Request)
constexpr uint8_t kChAuxVra        = 8;   // CH9: VRA Knob (Aux / Reserved)
constexpr uint8_t kChAuxVrb        = 9;   // CH10: VRB Knob (Aux / Reserved)
constexpr uint8_t kChAux11         = 10;  // CH11: Aux Channel 11
constexpr uint8_t kChAux12         = 11;  // CH12: Aux Channel 12

// Backward-compatibility aliases for legacy references:
constexpr uint8_t kChBrake         = kChVelocity;
constexpr uint8_t kChThrottle      = kChVelocity;
constexpr uint8_t kChIgnition      = kChDriveEnable;
constexpr uint8_t kChGear          = kChDriveEnvelope;
constexpr uint8_t kChSwitchA       = kChDriveEnable;
constexpr uint8_t kChSwitchD       = kChAutoRequest;
constexpr uint8_t kChDialVra       = kChAuxVra;
constexpr uint8_t kChDialVrb       = kChAuxVrb;

// ── RadioLink T12D / SBUS Calibration & Plausibility ───────────────
// 11-bit SBUS raw values range ~172 (1000us) to ~1811 (2000us) with center ~992 (1500us).
constexpr uint16_t kSbusRawMin      =  172;
constexpr uint16_t kSbusRawCenter   =  992;
constexpr uint16_t kSbusRawMax      = 1811;

// Calibrated Microsecond Limits & Plausibility Clusters
constexpr uint32_t kPulseAbsoluteMinUs  =  850;   // Pulses below this are mechanically/electrically impossible
constexpr uint32_t kPulseAbsoluteMaxUs  = 2150;   // Pulses above this are mechanically/electrically impossible
constexpr uint32_t kPulseMinValidUs     =  850;
constexpr uint32_t kPulseMaxValidUs     = 2150;
constexpr uint32_t kPulseCenterUs       = 1500;
constexpr uint32_t kPulseDeadbandUs     =   30;   // Center deadband (+/- 30us)

// Switch Cluster Windows (Valid states must sit inside one of these clusters):
// Low cluster: ~1000us (850..1150us)
// Mid cluster: ~1500us (1350..1650us)
// High cluster: ~2000us (1850..2150us)
constexpr uint32_t kClusterLowMaxUs     = 1150;
constexpr uint32_t kClusterMidMinUs     = 1350;
constexpr uint32_t kClusterMidMaxUs     = 1650;
constexpr uint32_t kClusterHighMinUs    = 1850;

// SWC 3-Tier Drive Envelope Thresholds (Precision / Normal / Fast)
constexpr uint32_t kModePrecisionMaxUs  = 1250;
constexpr uint32_t kModeFastMinUs       = 1750;

// SWA / SWB / SWD 2-Position Threshold
constexpr uint32_t kSwitchThresholdUs   = 1500;
constexpr uint32_t kSwitchHysteresisUs  =   50;

// ── Speed Envelopes & Velocity Scaling (mm/s) ──────────────────────
constexpr int32_t kSpeedPrecisionMaxMmps =  750;  // 0.75 m/s (2.7 km/h) Docking / Precision
constexpr int32_t kSpeedNormalMaxMmps    = 1800;  // 1.80 m/s (6.5 km/h) Standard Driving
constexpr int32_t kSpeedFastMaxMmps      = 3000;  // 3.00 m/s (10.8 km/h) Fast Transport (Conditional)
constexpr int32_t kSpeedRevMaxMmps       = 1000;  // 1.00 m/s (3.6 km/h) Reverse Limit

// ── Direction Reversal & Neutral Dwell Safety ──────────────────────
// Reverse torque is strictly locked out until BOTH measured speed <= 50 mm/s AND dwell >= 200 ms
constexpr int32_t kZeroSpeedThresholdMmps =   50;  // Maximum rolling speed considered stationary
constexpr uint32_t kNeutralDwellRequiredMs=  200;  // Neutral dwell duration before reversal allowed

// ── Drive Arming Sequence ──────────────────────────────────────────
constexpr uint32_t kArmingMinHealthyMs   =  500;  // Link must be healthy >= 500ms before arming permitted

// ── Actuator Limits ───────────────────────────────────────────────
constexpr float kMaxSteerAngleDeg       = 45.0f;  // Mechanical rack limit (+/- 45.0 deg)
constexpr int   kSbwAngleOffset         = 30000;  // Steer-by-wire vendor offset (0° -> 30000 raw)
constexpr int16_t kMinSteerRaw          = 29550;  // -45.0 deg full left limit (29550 raw)
constexpr int16_t kMaxSteerRaw          = 30450;  // +45.0 deg full right limit (30450 raw)
constexpr float kMaxBrakeStrokeMm       = 27.0f;  // SEB Max Emergency Stroke
constexpr float kParkBrakeStrokeMm      = 15.0f;  // SEB Park / Brake Hold Holding Stroke
constexpr float kManualBrakeStrokeMm    = 15.0f;  // SEB Manual Pull Reference

// ── Timing & Graduated Link States ────────────────────────────────
constexpr int kRcCaptureHz              = 50;     // 20 ms loop
constexpr int kCanTxHz                  = 50;     // 20 ms loop
constexpr int kHeartbeatHz              = 10;     // 100 ms loop
constexpr uint32_t kLinkDegradedTimeoutMs = 50;   // >50 ms without frame = Degraded (stale)
constexpr uint32_t kLinkLostTimeoutMs     = 100;  // >100 ms without frame = Lost (safe stop)
constexpr int kSignalLossTimeoutMs      = 100;    // Backward-compat alias

// ── Serial Telemetry & CAN Display Filtering ───────────────────────
constexpr int   kCanLogDecimation       = 25;     // 25 * 20ms = 500ms (2 Hz periodic summary)
constexpr float kLogDeltaSteerDeg       = 1.0f;   // Immediate log if steer changes >= 1.0 deg
constexpr float kLogDeltaBrakeMm        = 0.5f;   // Immediate log if brake changes >= 0.5 mm
constexpr int32_t kLogDeltaSpeedMmps    = 50;     // Immediate log if speed changes >= 50 mm/s

}  // namespace rm
