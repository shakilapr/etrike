#pragma once
// dumb-esp — single-bus bare controller & fake e-trike simulator.
// Hardware pin map, timing, actuator constants, and physics constants
// re-exported in the rt:: namespace so physics_model.cpp compiles unmodified.

#include <cstdint>
#include "shared_config.h"

// ── CAN bus (built-in TWAI, single bus, identical wiring to RT/RM) ─────────
namespace dumb {

constexpr int kCanTxGpio     =     5;
constexpr int kCanRxGpio     =     4;
constexpr int kCanBitrateHz  = 500'000;

// Control loop
constexpr int kControlHz     = 100;       // 10 ms period
constexpr int kBootHoldMs    = 500;       // delay before first TX

// Steer-by-wire angle raw encoding  (raw 30000 → 0°, scale 0.1°/count)
constexpr int     kSbwAngleOffset  = 30000;
constexpr int16_t kMinSteerRaw     = 29550; // −45°
constexpr int16_t kMaxSteerRaw     = 30450; // +45°

// Brake stroke conversion (scale=0.05 mm/count, offset=-30 mm)
// raw = (mm + 30.0) / 0.05 = (mm + 30.0) * 20
// Released (0.0 mm) = raw 600 (0x0258); Full (27.0 mm) = raw 1140 (0x0474)
constexpr float    kMaxBrakeStrokeMm   = 27.0f;   // full actuator stroke
constexpr float    kBrakeStrokeScaleF  =  0.05f;  // mm per raw count
constexpr float    kBrakeStrokeBias    = 30.0f;   // offset in mm (raw 0 = -30.0 mm)
constexpr uint16_t kMaxBrakeStrokeRaw  = 1140;    // 27.0 mm full stroke
constexpr uint16_t kBrakeStrokeZeroRaw = 600;     // 0.0 mm released position

} // namespace dumb

// ── Physics constants in namespace rt ─────────────────────────────────────
// Identical values to rt-esp32/src/config.h.
// physics_model.h / physics_model.cpp are copied verbatim from rt-esp32 and
// reference these constants directly.
namespace rt {

constexpr float kSteerLimitDeg           = 40.0f;
constexpr float kAngleClampBaseDeg       = 40.0f;
constexpr float kAngleClampMinDeg        =  5.0f;
constexpr float kAngleClampRangeDeg      = 35.0f;
constexpr float kAngleClampSpeedRange    = 23.0f;  // km/h span for clamp curve
constexpr float kSteerFollowingErrMinDeg =  2.0f;
constexpr float kSteerFollowingErrFactor =  0.25f;

} // namespace rt
