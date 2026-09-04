#pragma once
// RM-ESP32 — Pure signal decoding logic (free of hardware peripheral dependencies).
// Usable in both firmware and native unit testing.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "config.h"

namespace rm {

    struct RcSnapshot {
    float steering_deg{0.0f};     // Steering angle: +/- 40.0 deg
    float brake_stroke_mm{0.0f};  // Brake stroke: 0.0 to 27.0 mm
    float speed_trim{0.0f};       // Speed trim / limiter from VRA dial (0.0 to 1.0)
    float aux_pass{0.0f};         // Aux pass-through from VRB dial (0.0 to 1.0)
    bool  ignition{false};        // Ignition switch via SWB (true = ON)
    can::Gear gear{can::Gear::N}; // Gear selector via SWC: N, D, R
    bool  signal_valid{false};    // True if all critical channels receive fresh pulses
    uint32_t last_update_ms{0};   // Timestamp of last valid capture
};

inline RcSnapshot decode_rc_signals(const uint32_t raw_us[kNumRcChannels],
                                    const uint32_t last_edge_ms[kNumRcChannels],
                                    uint32_t now_ms) {
    RcSnapshot snap;

    // 1. Deadman signal validity check (CH0, CH1, CH4, CH5 are safety critical)
    bool valid = true;
    for (uint8_t i : {0, 1, 4, 5}) {
        if (now_ms < last_edge_ms[i] || (now_ms - last_edge_ms[i]) > kSignalLossTimeoutMs) {
            valid = false;
            break;
        }
        if (raw_us[i] < kPulseMinValidUs || raw_us[i] > kPulseMaxValidUs) {
            valid = false;
            break;
        }
    }

    snap.signal_valid = valid;
    snap.last_update_ms = now_ms;

    if (valid) {
        // CH0: Steering (+/- 450 deg)
        int32_t steer_offset = static_cast<int32_t>(raw_us[0]) - static_cast<int32_t>(kPulseCenterUs);
        if (std::abs(steer_offset) <= static_cast<int32_t>(kPulseDeadbandUs)) {
            snap.steering_deg = 0.0f;
        } else {
            float norm = static_cast<float>(steer_offset) / 450.0f;
            norm = std::clamp(norm, -1.0f, 1.0f);
            snap.steering_deg = norm * kMaxSteerAngleDeg;
        }

        // CH1: Brake Stroke (0.0 to 27.0 mm)
        // Pulled from 1520us to 2000us
        if (raw_us[1] > (kPulseCenterUs + 20)) {
            float norm = static_cast<float>(raw_us[1] - (kPulseCenterUs + 20)) / 450.0f;
            norm = std::clamp(norm, 0.0f, 1.0f);
            snap.brake_stroke_mm = norm * kMaxBrakeStrokeMm;
        } else {
            snap.brake_stroke_mm = 0.0f;
        }

        // CH2: Speed Limiter / Throttle (0.0 to 1.0) with idle deadband
        if (raw_us[2] <= kThrottleMinUs) {
            snap.speed_trim = 0.0f;
        } else {
            float trim_norm = static_cast<float>(raw_us[2] - kThrottleMinUs) /
                              static_cast<float>(kThrottleMaxUs - kThrottleMinUs);
            snap.speed_trim = std::clamp(trim_norm, 0.0f, 1.0f);
        }

        // CH3: Aux Pass-Through (0.0 to 1.0)
        float aux_norm = static_cast<float>(raw_us[3] - kPulseMinValidUs) / 1000.0f;
        snap.aux_pass = std::clamp(aux_norm, 0.0f, 1.0f);

        // CH4: Ignition (SWB 2-position toggle)
        snap.ignition = (raw_us[4] >= kIgnitionThresholdUs);

        // CH5: Gear Selector (SWC 3-position toggle)
        // UP = Reverse, MID = Park/Neutral, DOWN = Drive
        if (raw_us[5] <= kGearRevMaxUs) {
            snap.gear = can::Gear::R;
        } else if (raw_us[5] >= kGearDriveMinUs) {
            snap.gear = can::Gear::D;
        } else {
            snap.gear = can::Gear::N;
        }
    } else {
        // Safe fail-safe defaults upon signal loss
        snap.steering_deg = 0.0f;
        snap.brake_stroke_mm = kMaxBrakeStrokeMm; // Maximum emergency brake stroke
        snap.ignition = false;
        snap.gear = can::Gear::N;
        snap.speed_trim = 0.0f;
    }

    return snap;
}

}  // namespace rm
