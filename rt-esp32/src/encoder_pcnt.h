#pragma once
// Quadrature encoder interface using ESP32-S3 PCNT (Pulse Counter) peripheral.
//
// COMPILE-DISABLED by default: #ifdef CONFIG_ENABLE_ENCODERS
// Encoders are NOT physically fitted to the vehicle. The PCNT peripheral
// would configure floating inputs that count noise pulses if enabled without
// hardware. Enable only after:
//   1. Physical quadrature encoder(s) installed on motor/wheels
//   2. Wiring verified: A/B channels, pull-ups, TVS protection
//   3. Quadrature phasing verified (swap A/B if direction reversed)
//   4. Speed reading validated on 0x220 RT_PID_RPT telemetry first
//
// Four encoders planned (gap #5):
//   0 = rear motor    (GPIO 1=A, 2=B)   — fitted, quadrature
//   1 = front wheel   (GPIO 10=A, 6=B)   — sensor TBD
//   2 = rear left     (GPIO 9=A, 12=B)  — sensor TBD
//   3 = rear right    (GPIO 13=A, 14=B) — sensor TBD
//
// ESP32-S3 has 8 PCNT units. Each can decode quadrature in hardware
// with zero CPU overhead. Counter wraps at INT16 boundaries.

#include <cstdint>
#include "config.h"

namespace rt {

#ifdef CONFIG_ENABLE_ENCODERS

// ── Active implementation (compiled only when encoders fitted) ───────

// Initialize all four quadrature encoders.
// Configures PCNT units, GPIOs, glitch filters, and counter limits.
// Call once during app_main init sequence (before FreeRTOS scheduler).
void encoder_init();

// Read accumulated pulse count for a specific encoder.
// index: 0=rear_motor, 1=front_wheel, 2=rear_left, 3=rear_right.
// Returns signed 16-bit count (auto-wraps at PCNT limits).
// Must be called periodically to prevent counter overflow between reads.
int16_t encoder_read_pulses(int index);

// Reset accumulated counts for all four encoders to zero.
void encoder_reset_all();

// Reset accumulated count for a single encoder.
void encoder_reset(int index);

// Calculate speed in mm/s from pulse count delta.
// Uses rear wheel circumference (200mm radius) and encoder PPR.
// dt_s: elapsed time in seconds since last reading.
// index: use 0 for rear motor (only one with known wheel geometry).
// Returns speed in mm/s. Returns 0 if dt_s <= 0.
// NOTE: mm_per_pulse depends on actual encoder PPR fitted.
//       Default assumes 1024 PPR with 4x decoding = 4096 pulses/rev.
float encoder_read_speed_mmps(int index, float dt_s);

#else  // !CONFIG_ENABLE_ENCODERS

// ── Stub implementations — compiled when encoders not fitted ─────────
// All functions return 0 / no-op. No PCNT hardware is configured.
// This prevents floating-input noise from creating phantom speed readings.

inline void encoder_init() {}
inline int16_t encoder_read_pulses(int) { return 0; }
inline void encoder_reset_all() {}
inline void encoder_reset(int) {}
inline float encoder_read_speed_mmps(int, float) { return 0.0f; }

#endif // CONFIG_ENABLE_ENCODERS

} // namespace rt
