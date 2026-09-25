#pragma once
// dumb-esp — Dual-Bus Autonomous Bridge & Controller.
// Interfaces Jetson Autoware on High CAN (MCP2515 SPI @ 500 kbps)
// and vehicle actuators on Low CAN (TWAI @ 500 kbps).

#include <cstdint>
#include "shared_config.h"

namespace dumb {

#if CONFIG_IDF_TARGET_ESP32S3
// ── ESP32-S3 Pinout ──────────────────────────────────────────────────────
// Low CAN (TWAI): Built-in controller to physical actuators (500 kbps)
constexpr int kCanLowTxGpio     = 5;
constexpr int kCanLowRxGpio     = 4;
constexpr int kCanLowBitrateHz  = 500'000;

// High CAN (MCP2515 SPI): External MCP2515 to Jetson Autoware (500 kbps)
constexpr int kSpiSckGpio       = 15;
constexpr int kSpiMosiGpio      = 16;
constexpr int kSpiMisoGpio      = 17;
constexpr int kSpiCsGpio        = 18;
constexpr int kMcpIntGpio       = 47;
constexpr int kCanHighBitrateHz = 500'000;
constexpr int kSpiHost          = 1; // SPI2_HOST (FSPI) on S3

constexpr int kStatusLedGpio    = 48; // WS2812 onboard LED
constexpr bool kHasWs2812Led    = true;

#else
// ── Classic ESP32 (esp32dev) Pinout ──────────────────────────────────────
// Low CAN (TWAI): Built-in controller to physical actuators (500 kbps)
constexpr int kCanLowTxGpio     = 5;
constexpr int kCanLowRxGpio     = 4;
constexpr int kCanLowBitrateHz  = 500'000;

// High CAN (MCP2515 SPI): Hardware VSPI to Jetson Autoware (500 kbps)
constexpr int kSpiSckGpio       = 18;
constexpr int kSpiMosiGpio      = 23;
constexpr int kSpiMisoGpio      = 19;
constexpr int kSpiCsGpio        = 21;
constexpr int kMcpIntGpio       = 22;
constexpr int kCanHighBitrateHz = 500'000;
constexpr int kSpiHost          = 2; // SPI3_HOST (VSPI) on classic ESP32

constexpr int kStatusLedGpio    = 2;  // Onboard blue LED on classic ESP32 dev boards
constexpr bool kHasWs2812Led    = false;
#endif

// ── Control loop timing ──────────────────────────────────────────────────
constexpr int kControlHz     = 100;       // 10 ms period
constexpr int kBootHoldMs    = 500;       // delay before first TX

// ── Actuator Limits & Encoding ───────────────────────────────────────────
// Steer-by-wire angle raw encoding (raw 30000 -> 0°, scale 0.1°/count)
constexpr int     kSbwAngleOffset  = 30000;
constexpr int16_t kMinSteerRaw     = 29550; // -45.0°
constexpr int16_t kMaxSteerRaw     = 30450; // +45.0°

// Brake stroke conversion (scale=0.05 mm/count, offset=-30 mm)
// raw = (mm + 30.0) / 0.05 = (mm + 30.0) * 20
// Released (0.0 mm) = raw 600 (0x0258); Full (27.0 mm) = raw 1140 (0x0474)
constexpr float    kMaxBrakeStrokeMm   = 27.0f;   // full actuator stroke
constexpr float    kBrakeStrokeScaleF  =  0.05f;  // mm per raw count
constexpr float    kBrakeStrokeBias    = 30.0f;   // offset in mm (raw 0 = -30.0 mm)
constexpr uint16_t kMaxBrakeStrokeRaw  = 1140;    // 27.0 mm full stroke
constexpr uint16_t kBrakeStrokeZeroRaw = 600;     // 0.0 mm released position
constexpr int32_t  kMaxBrakePressureKpa = 3000;   // 30.0 bar max hydraulic pressure

} // namespace dumb

// ── Physics constants in namespace rt ─────────────────────────────────────
// Identical values to rt-esp32/src/config.h.
// physics_model.h / physics_model.cpp reference these constants directly.
namespace rt {

constexpr float kSteerLimitDeg           = 40.0f;
constexpr float kAngleClampBaseDeg       = 40.0f;
constexpr float kAngleClampMinDeg        =  5.0f;
constexpr float kAngleClampRangeDeg      = 35.0f;
constexpr float kAngleClampSpeedRange    = 23.0f;  // km/h span for clamp curve
constexpr float kSteerFollowingErrMinDeg =  2.0f;
constexpr float kSteerFollowingErrFactor =  0.25f;

} // namespace rt
