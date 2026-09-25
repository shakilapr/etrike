#pragma once
// Shared Onboard RGB Status LED Visual Grammar Engine
// Implements the deterministic timing & color composition defined in:
// docs/hardware/rgb-status-led-visual-language.md
//
// Pure C++17 (no platform headers) so the state evaluator and waveform
// generator can be unit-tested on host and driven at 50 Hz on ESP32-S3.

#include <cstdint>

namespace shared::led {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

// ── 1. The 7 Domain Colors (Where is the issue?) ─────────────────────────
enum class DomainColor : uint8_t {
    Off    = 0,
    Red    = 1,  // Safety / ESTOP
    Orange = 2,  // Physical CAN / SPI interface
    Cyan   = 3,  // Actuators (MTR, SEB, SES)
    Yellow = 4,  // Peer / network controllers (Host, RT, SYS)
    Purple = 5,  // MANUAL mode
    Green  = 6,  // AUTO mode
    White  = 7,  // Boot / system initialization
    Blue   = 8,  // Reserved for upstream/authority overlay pip
};

// ── 2. Base Cadences (State urgency) ─────────────────────────────────────
enum class BaseCadence : uint8_t {
    Off         = 0,
    Solid       = 1,  // Continuous (critical latched or active motion)
    RemoteEstop = 2,  // 600 ms RED -> 200 ms YELLOW step toggle
    FastBlink   = 3,  // 150 ms ON / 150 ms OFF (~3.3 Hz hardware/bus fault)
    Breathe     = 4,  // 1800 ms smooth cycle (900 ms fade up, 900 ms fade down)
};

// ── 3. Temporal Overlay Pips (Temporally replaces base color) ────────────
enum class OverlayPip : uint8_t {
    None          = 0,
    RedFault      = 1,  // 200 ms RED   (1000 ms gap): Fault / rejected / unsafe
    AmberModified = 2,  // 200 ms AMBER (1000 ms gap): Modified / blocked / conflict
    BlueAuthority = 3,  // 200 ms BLUE  (1000 ms gap): Upstream source / authority missing
    WhiteActivity = 4,  // 120 ms WHITE (880 ms gap) : Active command executing
};

struct VisualPattern {
    DomainColor base    = DomainColor::Off;
    BaseCadence cadence = BaseCadence::Off;
    OverlayPip  pip     = OverlayPip::None;

    constexpr bool operator==(const VisualPattern& o) const {
        return base == o.base && cadence == o.cadence && pip == o.pip;
    }
    constexpr bool operator!=(const VisualPattern& o) const {
        return !(*this == o);
    }
};

// ── Timing Constants ─────────────────────────────────────────────────────
constexpr uint32_t kRemoteEstopCycleMs = 800;   // 600 ms Red + 200 ms Yellow
constexpr uint32_t kRemoteEstopRedMs   = 600;
constexpr uint32_t kFastBlinkCycleMs   = 300;   // 150 ms ON + 150 ms OFF
constexpr uint32_t kFastBlinkOnMs      = 150;
constexpr uint32_t kBreatheCycleMs     = 1800;  // 900 ms up + 900 ms down
constexpr uint32_t kBreatheHalfMs      = 900;

constexpr uint32_t kPipPeriodMs        = 1200;  // 1000 ms base + 200 ms pip
constexpr uint32_t kDiagPipDurationMs  = 200;
constexpr uint32_t kTickPeriodMs       = 1000;  // 880 ms base + 120 ms white tick
constexpr uint32_t kWhiteTickDurationMs= 120;

// Calibrated pre-scale RGB coordinates (avoids adjacent-hue bleed on WS2812)
constexpr Rgb palette_lookup(DomainColor c) {
    switch (c) {
        case DomainColor::Red:    return {255,   0,   0};
        case DomainColor::Orange: return {255,  35,   0};
        case DomainColor::Cyan:   return {  0, 180, 255};
        case DomainColor::Yellow: return {255, 130,   0};
        case DomainColor::Purple: return {140,   0, 255};
        case DomainColor::Green:  return {  0, 255,   0};
        case DomainColor::White:  return {255, 255, 255};
        case DomainColor::Blue:   return {  0,   0, 255};
        case DomainColor::Off:
        default:                  return {  0,   0,   0};
    }
}

constexpr Rgb scale_rgb(Rgb c, uint8_t scale) {
    return {
        static_cast<uint8_t>((uint16_t(c.r) * scale) / 255),
        static_cast<uint8_t>((uint16_t(c.g) * scale) / 255),
        static_cast<uint8_t>((uint16_t(c.b) * scale) / 255),
    };
}

// Deterministic waveform renderer: maps (VisualPattern, now_ms) -> final WS2812 RGB
// max_brightness defaults to 64 (~25% peak) to prevent bench glare.
constexpr Rgb render(const VisualPattern& pat, uint32_t now_ms, uint8_t max_brightness = 64) {
    if (pat.base == DomainColor::Off || pat.cadence == BaseCadence::Off) {
        return {0, 0, 0};
    }

    // 1. Check Temporal Overlay Pip (replaces base color at end of cycle)
    if (pat.pip != OverlayPip::None) {
        if (pat.pip == OverlayPip::WhiteActivity) {
            const uint32_t phase = now_ms % kTickPeriodMs;
            if (phase >= (kTickPeriodMs - kWhiteTickDurationMs)) {
                return scale_rgb(palette_lookup(DomainColor::White), max_brightness);
            }
        } else {
            const uint32_t phase = now_ms % kPipPeriodMs;
            if (phase >= (kPipPeriodMs - kDiagPipDurationMs)) {
                switch (pat.pip) {
                    case OverlayPip::RedFault:
                        return scale_rgb(palette_lookup(DomainColor::Red), max_brightness);
                    case OverlayPip::AmberModified:
                        return scale_rgb(palette_lookup(DomainColor::Orange), max_brightness);
                    case OverlayPip::BlueAuthority:
                        return scale_rgb(palette_lookup(DomainColor::Blue), max_brightness);
                    default:
                        break;
                }
            }
        }
    }

    // 2. Render Base Cadence
    switch (pat.cadence) {
        case BaseCadence::Solid:
            return scale_rgb(palette_lookup(pat.base), max_brightness);

        case BaseCadence::RemoteEstop: {
            const uint32_t phase = now_ms % kRemoteEstopCycleMs;
            const DomainColor c = (phase < kRemoteEstopRedMs) ? DomainColor::Red : DomainColor::Yellow;
            return scale_rgb(palette_lookup(c), max_brightness);
        }

        case BaseCadence::FastBlink: {
            const uint32_t phase = now_ms % kFastBlinkCycleMs;
            if (phase < kFastBlinkOnMs) {
                return scale_rgb(palette_lookup(pat.base), max_brightness);
            }
            return {0, 0, 0};
        }

        case BaseCadence::Breathe: {
            const uint32_t phase = now_ms % kBreatheCycleMs;
            // Triangle wave 0..255 over 1800 ms (900 ms up, 900 ms down)
            const uint32_t lin = (phase < kBreatheHalfMs)
                ? (phase * 255) / kBreatheHalfMs
                : ((kBreatheCycleMs - phase) * 255) / kBreatheHalfMs;
            // Quadratic gamma with 12% minimum floor so the base hue remains identifiable at trough
            const uint32_t gamma = (lin * lin) / 255;
            const uint32_t clamped = 30 + (gamma * 225) / 255;
            const uint8_t  scale = static_cast<uint8_t>((clamped * max_brightness) / 255);
            return scale_rgb(palette_lookup(pat.base), scale);
        }

        case BaseCadence::Off:
        default:
            return {0, 0, 0};
    }
}

} // namespace shared::led
