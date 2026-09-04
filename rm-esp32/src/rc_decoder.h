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
    float steering_deg{0.0f};     // Steering angle: +/- 45.0 deg (Right Stick Horizontal)
    float brake_stroke_mm{0.0f};  // Brake stroke: 0.0 to 27.0 mm (Right Stick Vertical)
    float throttle_norm{0.0f};    // Proportional throttle demand: 0.0 to 1.0 (Left Stick Vertical)
    float spare_ch4{0.0f};        // Spare input: 0.0 to 1.0 (Left Stick Horizontal)
    bool  ignition{false};        // Ignition switch via SWB (true = ON)
    can::Gear gear{can::Gear::N}; // Gear selector via SWC: N, D, R
    bool  signal_valid{false};    // True if all critical channels receive fresh pulses
    uint32_t last_update_ms{0};   // Timestamp of last valid capture
};

inline RcSnapshot decode_rc_signals(const uint32_t raw_us[kNumRcChannels],
                                    const uint32_t last_edge_ms[kNumRcChannels],
                                    uint32_t now_ms) {
    RcSnapshot snap;

    // 1. Deadman signal validity check (CH0: Steer, CH1: Brake, CH2: Throttle, CH4: Ign, CH5: Gear)
    bool valid = true;
    for (uint8_t i : {0, 1, 2, 4, 5}) {
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
        // CH0 (Right Stick Horizontal): Steering (+/- 45.0 deg)
        int32_t steer_offset = static_cast<int32_t>(raw_us[0]) - static_cast<int32_t>(kPulseCenterUs);
        if (std::abs(steer_offset) <= static_cast<int32_t>(kPulseDeadbandUs)) {
            snap.steering_deg = 0.0f;
        } else {
            float norm = static_cast<float>(steer_offset) / 450.0f;
            norm = std::clamp(norm, -1.0f, 1.0f);
            snap.steering_deg = norm * kMaxSteerAngleDeg;
        }

        // CH1 (Right Stick Vertical): Brake Stroke (0.0 to 27.0 mm)
        // Spring centered at 1500us; pushing past 1520us engages brake linearly up to 1970us
        if (raw_us[1] > (kPulseCenterUs + 20)) {
            float norm = static_cast<float>(raw_us[1] - (kPulseCenterUs + 20)) / 450.0f;
            norm = std::clamp(norm, 0.0f, 1.0f);
            snap.brake_stroke_mm = norm * kMaxBrakeStrokeMm;
        } else {
            snap.brake_stroke_mm = 0.0f;
        }

        // CH2 (Left Stick Vertical): Throttle (0.0 to 1.0) with idle deadband
        // Fully down (idle) <= 1050us -> 0.0. Pushing up ramps smoothly to 1.0 at 1950us.
        if (raw_us[2] <= kThrottleMinUs) {
            snap.throttle_norm = 0.0f;
        } else {
            float t_norm = static_cast<float>(raw_us[2] - kThrottleMinUs) /
                           static_cast<float>(kThrottleMaxUs - kThrottleMinUs);
            snap.throttle_norm = std::clamp(t_norm, 0.0f, 1.0f);
        }

        // CH3 (Left Stick Horizontal): Spare Pass-Through (0.0 to 1.0)
        float spare_norm = static_cast<float>(raw_us[3] - kPulseMinValidUs) / 1000.0f;
        snap.spare_ch4 = std::clamp(spare_norm, 0.0f, 1.0f);

        // CH4 (SWB 2-position toggle): Ignition OFF/ON
        snap.ignition = (raw_us[4] >= kIgnitionThresholdUs);

        // CH5 (SWC 3-position toggle): Gear Selector R / N / D
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
        snap.throttle_norm = 0.0f;
        snap.spare_ch4 = 0.0f;
    }

    return snap;
}

}  // namespace rm
