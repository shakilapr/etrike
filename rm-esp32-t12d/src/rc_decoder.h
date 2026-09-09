#pragma once
// RM-ESP32-T12D — Pure signal decoding logic (free of hardware peripheral dependencies).
// Usable in firmware, native unit testing, and simulation.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "config.h"
#include "sbus_parser.h"

namespace rm {

struct RcSnapshot {
    float steering_deg{0.0f};     // Steering angle: +/- 45.0 deg (CH1: Right Stick Horizontal)
    float brake_stroke_mm{0.0f};  // Brake stroke: 0.0 to 27.0 mm (CH2: Right Stick Vertical)
    float throttle_norm{0.0f};    // Proportional throttle demand: 0.0 to 1.0 (CH3: Left Stick Vertical)
    float yaw_spare{0.0f};        // Spare input: 0.0 to 1.0 (CH4: Left Stick Horizontal)
    bool  ignition{false};        // Ignition switch via SWB (CH5: true = ON)
    can::Gear gear{can::Gear::N}; // Gear selector via SWC (CH6: N, D, R)
    bool  switch_a{false};        // SWA toggle (CH7)
    bool  switch_d{false};        // SWD toggle (CH8)
    float dial_vra{0.0f};         // VRA speed governor / trim: 0.0 to 1.0 (CH9)
    float dial_vrb{0.0f};         // VRB aux trim: 0.0 to 1.0 (CH10)
    uint16_t raw_channels[kNumSbusChannels]{0}; // Raw 11-bit SBUS channels
    uint32_t pulse_us[kNumSbusChannels]{0};     // Calibrated equivalent microsecond pulse
    bool  frame_lost{false};      // Single frame loss flag
    bool  failsafe{false};        // Hardware receiver failsafe active
    bool  signal_valid{false};    // True if healthy pulses received without failsafe/timeout
    uint32_t last_update_ms{0};   // Timestamp of last valid capture
};

// Pure decoder from calibrated microsecond pulse array and edge times
inline RcSnapshot decode_rc_pulses(const uint32_t pulse_us[kNumSbusChannels],
                                   uint32_t last_frame_ms,
                                   uint32_t now_ms,
                                   bool hw_failsafe = false,
                                   bool hw_frame_lost = false) {
    RcSnapshot snap;
    snap.last_update_ms = now_ms;
    snap.failsafe = hw_failsafe;
    snap.frame_lost = hw_frame_lost;

    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        snap.pulse_us[i] = pulse_us[i];
    }

    // 1. Fail-Safe Deadman check:
    // - Receiver hardware failsafe bit asserted
    // - Signal timeout > 100 ms
    // - Out of range pulse duration on critical channels (0: Steer, 1: Brake, 2: Throttle, 4: Ign, 5: Gear)
    bool valid = true;
    if (hw_failsafe) {
        valid = false;
    } else if ((now_ms - last_frame_ms) > kSignalLossTimeoutMs) {
        valid = false;
    } else {
        for (uint8_t i : {kChSteering, kChBrake, kChThrottle, kChIgnition, kChGear}) {
            if (pulse_us[i] < kPulseMinValidUs || pulse_us[i] > kPulseMaxValidUs) {
                valid = false;
                break;
            }
        }
    }

    snap.signal_valid = valid;

    if (valid) {
        // CH0 / CH1: Right Stick Horizontal: Steering (+/- 45.0 deg)
        int32_t steer_offset = static_cast<int32_t>(pulse_us[kChSteering]) - static_cast<int32_t>(kPulseCenterUs);
        if (std::abs(steer_offset) <= static_cast<int32_t>(kPulseDeadbandUs)) {
            snap.steering_deg = 0.0f;
        } else {
            float norm = static_cast<float>(steer_offset) / 450.0f;
            norm = std::clamp(norm, -1.0f, 1.0f);
            snap.steering_deg = norm * kMaxSteerAngleDeg;
        }

        // CH1 / CH2: Right Stick Vertical: Brake Stroke (0.0 to 27.0 mm)
        // Spring centered at 1500us; pushing past 1520us engages brake linearly up to 1970us
        if (pulse_us[kChBrake] > (kPulseCenterUs + 20)) {
            float norm = static_cast<float>(pulse_us[kChBrake] - (kPulseCenterUs + 20)) / 450.0f;
            norm = std::clamp(norm, 0.0f, 1.0f);
            snap.brake_stroke_mm = norm * kMaxBrakeStrokeMm;
        } else {
            snap.brake_stroke_mm = 0.0f;
        }

        // CH2 / CH3: Left Stick Vertical: Throttle (0.0 to 1.0) with idle deadband
        // Fully down (idle) <= 1050us -> 0.0. Pushing up ramps smoothly to 1.0 at 1950us.
        if (pulse_us[kChThrottle] <= kThrottleMinUs) {
            snap.throttle_norm = 0.0f;
        } else {
            float t_norm = static_cast<float>(pulse_us[kChThrottle] - kThrottleMinUs) /
                           static_cast<float>(kThrottleMaxUs - kThrottleMinUs);
            snap.throttle_norm = std::clamp(t_norm, 0.0f, 1.0f);
        }

        // CH3 / CH4: Left Stick Horizontal: Spare Pass-Through (0.0 to 1.0)
        float yaw_norm = static_cast<float>(pulse_us[kChYawSpare] - 1000) / 1000.0f;
        snap.yaw_spare = std::clamp(yaw_norm, 0.0f, 1.0f);

        // CH4 / CH5: SWB 2-position toggle: Ignition OFF / ON
        snap.ignition = (pulse_us[kChIgnition] >= kIgnitionThresholdUs);

        // CH5 / CH6: SWC 3-position toggle: Gear Selector R / N / D
        // UP = Reverse, MID = Park/Neutral, DOWN = Drive
        if (pulse_us[kChGear] <= kGearRevMaxUs) {
            snap.gear = can::Gear::R;
        } else if (pulse_us[kChGear] >= kGearDriveMinUs) {
            snap.gear = can::Gear::D;
        } else {
            snap.gear = can::Gear::N;
        }

        // CH6 / CH7: SWA 2-position toggle
        snap.switch_a = (pulse_us[kChSwitchA] >= kPulseCenterUs);

        // CH7 / CH8: SWD 2-position toggle
        snap.switch_d = (pulse_us[kChSwitchD] >= kPulseCenterUs);

        // CH8 / CH9: VRA Dial (0.0 to 1.0)
        float vra_norm = static_cast<float>(pulse_us[kChDialVra] - 1000) / 1000.0f;
        snap.dial_vra = std::clamp(vra_norm, 0.0f, 1.0f);

        // CH9 / CH10: VRB Dial (0.0 to 1.0)
        float vrb_norm = static_cast<float>(pulse_us[kChDialVrb] - 1000) / 1000.0f;
        snap.dial_vrb = std::clamp(vrb_norm, 0.0f, 1.0f);
    } else {
        // Safe fail-safe defaults upon signal loss or failsafe bit
        snap.steering_deg    = 0.0f;
        snap.brake_stroke_mm = kMaxBrakeStrokeMm; // Maximum emergency brake stroke
        snap.ignition        = false;
        snap.gear            = can::Gear::N;
        snap.throttle_norm   = 0.0f;
        snap.yaw_spare       = 0.0f;
        snap.switch_a        = false;
        snap.switch_d        = false;
        snap.dial_vra        = 0.0f;
        snap.dial_vrb        = 0.0f;
    }

    return snap;
}

// Overload decoding directly from an SbusFrame
inline RcSnapshot decode_sbus_frame(const SbusFrame& frame,
                                    uint32_t last_frame_ms,
                                    uint32_t now_ms) {
    uint32_t pulse_us[kNumSbusChannels];
    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        pulse_us[i] = sbus_to_pulse_us(frame.channels[i]);
    }
    RcSnapshot snap = decode_rc_pulses(pulse_us, last_frame_ms, now_ms, frame.failsafe, frame.frame_lost);
    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        snap.raw_channels[i] = frame.channels[i];
    }
    return snap;
}

}  // namespace rm
