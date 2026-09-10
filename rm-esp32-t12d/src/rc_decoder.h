#pragma once
// RM-ESP32-T12D — Pure signal decoding & HMI state logic.
// Free of hardware peripheral dependencies; usable in firmware, native tests, and simulation.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "config.h"
#include "sbus_parser.h"

namespace rm {

enum class LinkState : uint8_t {
    Normal   = 0,  // Fresh frames received (< 150 ms)
    Degraded = 1,  // Single frame drop flag asserted by receiver
    Lost     = 2   // Signal lost (> 150 ms or receiver failsafe)
};

struct RcSnapshot {
    // ── Primary Driving Controls ─────────────────────────────────────
    float      steering_deg{0.0f};         // Steering angle: +/- 45.0 deg (CH1: Right Stick X)
    float      brake_stroke_mm{0.0f};      // Service brake stroke: 0.0 to 27.0 mm (CH2 / Park)
    float      throttle_norm{0.0f};        // Throttle demand: 0.0 to 1.0 (CH3: Left Stick Y)
    int32_t    target_speed_mmps{0};       // Target speed demand based on gear and throttle (-500..+3000 mm/s)
    can::Gear  gear{can::Gear::N};         // Transmission gear: R, N, D (CH7: SWC)
    float      velocity_norm{0.0f};        // Signed demand: -1.0 (rev) to +1.0 (fwd)

    // ── Discrete HMI Toggles ─────────────────────────────────────────
    bool          drive_enable_req{false};    // SWD: UP = Disabled, DOWN = Enable Request
    bool          park_hold_req{true};        // SWB: UP = Park Hold (15mm), MID/DOWN = Released (0mm)
    OperatingMode op_mode{OperatingMode::Bare}; // SWA: UP = BARE, MID = SYS, DOWN = RT
    float         aux_vra{0.0f};              // CH9: VRA Knob (0.0 to 1.0)
    float         aux_vrb{0.0f};              // CH10: VRB Knob (0.0 to 1.0)
    float         aux_vrc{0.0f};              // CH11: VRC Knob (0.0 to 1.0)
    float         aux_vrd{0.0f};              // CH12: VRD Knob (0.0 to 1.0)

    // ── Link & Health Status ─────────────────────────────────────────
    LinkState  link_state{LinkState::Lost};
    bool       input_plausible{true};      // True if pulses within physical electrical limits
    bool       frame_lost{false};          // Hardware SBUS single frame drop flag
    bool       failsafe{false};            // Hardware SBUS failsafe bit
    bool       signal_valid{false};        // True if link is healthy (not lost)
    uint32_t   last_update_ms{0};          // Timestamp of last update

    // ── Raw & Calibrated Diagnostics ─────────────────────────────────
    uint16_t   raw_channels[kNumSbusChannels]{0}; // 11-bit SBUS raw values
    uint32_t   pulse_us[kNumSbusChannels]{0};     // Calibrated microsecond pulses
};

// ── Pure Signal Decoder Function ────────────────────────────────────
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

    uint32_t dt = (now_ms >= last_frame_ms) ? (now_ms - last_frame_ms) : 0;
    bool link_ok = (last_frame_ms > 0) && !hw_failsafe && (dt <= kLinkLostTimeoutMs);

    // Pulse sanity check (pulses within basic electrical bounds across 12 channels)
    bool plausible = true;
    for (uint8_t i = 0; i < 12; ++i) {
        if (pulse_us[i] < kPulseAbsoluteMinUs || pulse_us[i] > kPulseAbsoluteMaxUs) {
            plausible = false;
            break;
        }
    }
    snap.input_plausible = plausible;

    snap.signal_valid = link_ok && plausible;
    if (!snap.signal_valid) {
        snap.link_state = LinkState::Lost;
    } else if (hw_frame_lost) {
        snap.link_state = LinkState::Degraded;
    } else {
        snap.link_state = LinkState::Normal;
    }

    // Process Channels if Link is Valid
    if (snap.signal_valid) {
        // CH1: Right Stick X -> Steering Rack Angle (+/- 45.0 deg) with smooth deadband ramp
        int32_t steer_offset = static_cast<int32_t>(pulse_us[kChSteering]) - static_cast<int32_t>(kPulseCenterUs);
        int32_t abs_steer = std::abs(steer_offset);
        if (abs_steer <= static_cast<int32_t>(kPulseDeadbandUs)) {
            snap.steering_deg = 0.0f;
        } else {
            float norm = static_cast<float>(abs_steer - static_cast<int32_t>(kPulseDeadbandUs)) /
                         static_cast<float>(450 - kPulseDeadbandUs);
            norm = std::clamp(norm, 0.0f, 1.0f);
            snap.steering_deg = (steer_offset > 0 ? norm : -norm) * kMaxSteerAngleDeg;
        }

        // CH2: Right Stick Y -> Service Brake Stroke (0.0 to 27.0 mm)
        // Generous deadband zone around center (1350 to 1650us) allows free steering without triggering brake.
        // Pushing beyond deadband smoothly engages progressive service brake.
        float stick_brake_mm = 0.0f;
        if (pulse_us[kChBrake] > kBrakeMaxUs) {
            stick_brake_mm = kMaxBrakeStrokeMm;
        } else if (pulse_us[kChBrake] > kBrakeStartUs) {
            // Push forward brake (1650us to 1950us)
            float b_norm = static_cast<float>(pulse_us[kChBrake] - kBrakeStartUs) /
                           static_cast<float>(kBrakeMaxUs - kBrakeStartUs);
            stick_brake_mm = std::clamp(b_norm, 0.0f, 1.0f) * kMaxBrakeStrokeMm;
        } else if (pulse_us[kChBrake] < (kPulseCenterUs - (kBrakeStartUs - kPulseCenterUs))) {
            // Pull down brake (< 1350us down to 1050us)
            uint32_t pull_threshold = kPulseCenterUs - (kBrakeStartUs - kPulseCenterUs); // 1350us
            float b_norm = static_cast<float>(pull_threshold - pulse_us[kChBrake]) /
                           static_cast<float>(pull_threshold - 1050);
            stick_brake_mm = std::clamp(b_norm, 0.0f, 1.0f) * kMaxBrakeStrokeMm;
        }

        // CH6: SWB 3-Position Switch -> Park / Brake Hold (UP = Park Hold [15mm], MID/DOWN = Released [0mm])
        snap.park_hold_req = (pulse_us[kChParkHold] <= kParkHoldMaxUs);

        // Apply Park holding stroke if requested
        snap.brake_stroke_mm = snap.park_hold_req ? std::max(stick_brake_mm, kParkBrakeStrokeMm) : stick_brake_mm;

        // CH3: Left Stick Y -> Throttle (0.0 to 1.0)
        float throttle_demand = 0.0f;
        if (pulse_us[kChThrottle] > kThrottleMinUs) {
            float t_norm = static_cast<float>(pulse_us[kChThrottle] - kThrottleMinUs) /
                           static_cast<float>(kThrottleMaxUs - kThrottleMinUs);
            throttle_demand = std::clamp(t_norm, 0.0f, 1.0f);
        }

        // Brake Over Throttle Interlock: cut throttle if brake > 5.0 mm
        if (snap.brake_stroke_mm > kBrakeThrottleCutoffMm) {
            throttle_demand = 0.0f;
        }
        snap.throttle_norm = throttle_demand;

        // CH8: SWD 2-Position Switch -> Drive Enable (UP = Safe/Disabled, DOWN = Enable Request/Armed)
        snap.drive_enable_req = (pulse_us[kChDriveEnable] >= kSwitchThresholdUs);

        // CH7: SWC 3-Position Switch -> Gear Selector (UP = Reverse, MID = Neutral, DOWN = Drive)
        if (snap.park_hold_req) {
            snap.gear = can::Gear::N;
        } else if (pulse_us[kChGear] <= kGearRevMaxUs) {
            snap.gear = can::Gear::R;
        } else if (pulse_us[kChGear] >= kGearDriveMinUs) {
            snap.gear = can::Gear::D;
        } else {
            snap.gear = can::Gear::N;
        }

        // CH5: SWA 3-Position Switch -> Target Selection (UP = BARE, MID = SYS, DOWN = RT)
        if (pulse_us[kChOperatingMode] <= kModeBareMaxUs) {
            snap.op_mode = OperatingMode::Bare;
        } else if (pulse_us[kChOperatingMode] >= kModeRtMinUs) {
            snap.op_mode = OperatingMode::Rt;
        } else {
            snap.op_mode = OperatingMode::Sys;
        }

        // CH9..CH12: Aux Proportional Knobs (0.0 to 1.0)
        float vra = static_cast<float>(static_cast<int32_t>(pulse_us[kChAuxVra]) - 1000) / 1000.0f;
        float vrb = static_cast<float>(static_cast<int32_t>(pulse_us[kChAuxVrb]) - 1000) / 1000.0f;
        float vrc = static_cast<float>(static_cast<int32_t>(pulse_us[kChAuxVrc]) - 1000) / 1000.0f;
        float vrd = static_cast<float>(static_cast<int32_t>(pulse_us[kChAuxVrd]) - 1000) / 1000.0f;
        snap.aux_vra = std::clamp(vra, 0.0f, 1.0f);
        snap.aux_vrb = std::clamp(vrb, 0.0f, 1.0f);
        snap.aux_vrc = std::clamp(vrc, 0.0f, 1.0f);
        snap.aux_vrd = std::clamp(vrd, 0.0f, 1.0f);

        // Target Speed Demand based on Gear and Throttle
        int32_t target_spd = 0;
        if (!snap.park_hold_req) {
            if (snap.gear == can::Gear::D) {
                target_spd = static_cast<int32_t>(std::round(snap.throttle_norm * static_cast<float>(kSpeedFwdMaxMmps)));
            } else if (snap.gear == can::Gear::R) {
                target_spd = -static_cast<int32_t>(std::round(snap.throttle_norm * static_cast<float>(kSpeedRevMaxMmps)));
            }
        }
        snap.target_speed_mmps = target_spd;
        snap.velocity_norm = (snap.gear == can::Gear::R) ? -snap.throttle_norm : snap.throttle_norm;
    } else {
        // Safe Failsafe Defaults on Link Loss or Fault
        snap.steering_deg      = 0.0f;
        snap.velocity_norm     = 0.0f;
        snap.target_speed_mmps = 0;
        snap.throttle_norm     = 0.0f;
        snap.drive_enable_req  = false;
        snap.park_hold_req     = true;
        snap.op_mode           = OperatingMode::Bare;
        snap.gear              = can::Gear::N;
        snap.brake_stroke_mm   = kParkBrakeStrokeMm; // 15mm holding stroke
        snap.aux_vra           = 0.0f;
        snap.aux_vrb           = 0.0f;
        snap.aux_vrc           = 0.0f;
        snap.aux_vrd           = 0.0f;
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
    RcSnapshot snap = decode_rc_pulses(pulse_us, last_frame_ms, now_ms,
                                       frame.failsafe, frame.frame_lost);
    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        snap.raw_channels[i] = frame.channels[i];
    }
    return snap;
}

}  // namespace rm
