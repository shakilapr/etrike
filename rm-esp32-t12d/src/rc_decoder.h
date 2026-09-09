#pragma once
// RM-ESP32-T12D — Pure signal decoding & HMI state logic.
// Free of hardware peripheral dependencies; usable in firmware, native tests, and simulation.
// Compliant with NHTSA, SAE, FHWA, UN R121, and ISO 13850 standards.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "config.h"
#include "sbus_parser.h"

namespace rm {

enum class LinkState : uint8_t {
    Normal   = 0,  // Fresh frames (< 50 ms)
    Degraded = 1,  // Stale frames (50..100 ms)
    Lost     = 2   // Signal lost (> 100 ms or failsafe)
};

enum class DriveEnvelope : uint8_t {
    Precision = 0, // 0.75 m/s (2.7 km/h) Docking / Precision
    Normal    = 1, // 1.80 m/s (6.5 km/h) Standard Driving
    Fast      = 2  // 3.00 m/s (10.8 km/h) Fast Transport (Conditional)
};

enum class ArmState : uint8_t {
    InitWaitDisable = 0, // Boot / reconnect: must observe SWA UP before arming
    Disarmed        = 1, // SWA is UP, ready to arm on deliberate DOWN edge
    Armed           = 2  // SWA is DOWN, drive enabled
};

struct RcSnapshot {
    // ── Primary Automotive Driving Controls ─────────────────────────
    float   steering_deg{0.0f};         // Steering angle: +/- 45.0 deg (CH1: Right Stick X)
    float   velocity_norm{0.0f};        // Signed demand: -1.0 to +1.0 (CH2: Right Stick Y, Spring Centered)
    int32_t target_speed_mmps{0};       // Target speed demand based on envelope and reversal state
    float   implement_x{0.0f};          // Implement / Aux X: 0.0 to 1.0 (CH3: Left Stick X)
    float   implement_y{0.0f};          // Implement / Aux Y: 0.0 to 1.0 (CH4: Left Stick Y)

    // ── Discrete HMI Toggles ─────────────────────────────────────────
    bool          drive_enable_req{false}; // SWA: true = DOWN (Enable Request), false = UP (Disabled)
    bool          park_hold_req{true};     // SWB: true = UP (Park / Brake Hold), false = DOWN (Drive)
    DriveEnvelope drive_envelope{DriveEnvelope::Precision}; // SWC: Precision / Normal / Fast
    bool          auto_mode_req{false};    // SWD: true = DOWN (Auto Request), false = UP (Manual)
    float         aux_vra{0.0f};           // CH9: VRA Knob (Auxiliary / Reserved)
    float         aux_vrb{0.0f};           // CH10: VRB Knob (Auxiliary / Reserved)

    // ── Link & Plausibility Status ───────────────────────────────────
    LinkState link_state{LinkState::Lost};
    bool      input_plausible{false};   // True if active pulses sit in valid clusters
    bool      reversal_locked{false};   // True if reverse torque is locked awaiting speed_safe && dwell
    bool      frame_lost{false};        // Hardware SBUS single frame drop flag
    bool      failsafe{false};          // Hardware SBUS failsafe bit
    bool      signal_valid{false};      // True if link is healthy (not lost)
    uint32_t  last_update_ms{0};        // Timestamp of last valid frame

    // ── Raw & Calibrated Diagnostics ─────────────────────────────────
    uint16_t raw_channels[kNumSbusChannels]{0}; // 11-bit SBUS raw values
    uint32_t pulse_us[kNumSbusChannels]{0};     // Calibrated microsecond pulses

    // ── Backward-Compatibility Aliases ───────────────────────────────
    float     brake_stroke_mm{0.0f};
    float     throttle_norm{0.0f};
    bool      ignition{false};
    can::Gear gear{can::Gear::N};
    bool      switch_a{false};
    bool      switch_d{false};
    float     dial_vra{0.0f};
    float     dial_vrb{0.0f};
};

// ── Direction Reversal Tracker (speed_safe && dwell_complete) ───────
struct ReversalTracker {
    bool     was_forward{false};
    uint32_t neutral_dwell_start_ms{0};
    bool     dwell_active{false};

    int32_t process_velocity(float vel_norm,
                             int32_t forward_max_mmps,
                             int32_t reverse_max_mmps,
                             int32_t measured_speed_mmps,
                             uint32_t now_ms,
                             bool* out_locked = nullptr) {
        if (vel_norm > 0.0f) {
            // Actively commanding forward drive
            was_forward = true;
            dwell_active = false;
            if (out_locked) *out_locked = false;
            return static_cast<int32_t>(std::round(vel_norm * static_cast<float>(forward_max_mmps)));
        } else if (vel_norm < 0.0f) {
            // Operator requesting reverse drive
            if (was_forward) {
                // Must strictly verify measured speed is stationary AND dwell timer elapsed
                if (std::abs(measured_speed_mmps) <= kZeroSpeedThresholdMmps) {
                    if (!dwell_active) {
                        dwell_active = true;
                        neutral_dwell_start_ms = now_ms;
                    }
                    if ((now_ms - neutral_dwell_start_ms) >= kNeutralDwellRequiredMs) {
                        // Both conditions satisfied: reverse authorized!
                        was_forward = false;
                        dwell_active = false;
                        if (out_locked) *out_locked = false;
                        return static_cast<int32_t>(std::round(vel_norm * static_cast<float>(reverse_max_mmps)));
                    }
                } else {
                    // Vehicle is still moving: reset dwell timer
                    dwell_active = false;
                }
                // Reverse locked out: demand zero torque, allow controlled stop
                if (out_locked) *out_locked = true;
                return 0;
            } else {
                // Already in reverse / safe to reverse
                if (out_locked) *out_locked = false;
                return static_cast<int32_t>(std::round(vel_norm * static_cast<float>(reverse_max_mmps)));
            }
        } else {
            // Stick in neutral
            if (was_forward) {
                if (std::abs(measured_speed_mmps) <= kZeroSpeedThresholdMmps) {
                    if (!dwell_active) {
                        dwell_active = true;
                        neutral_dwell_start_ms = now_ms;
                    }
                    if ((now_ms - neutral_dwell_start_ms) >= kNeutralDwellRequiredMs) {
                        was_forward = false;
                        dwell_active = false;
                    }
                } else {
                    dwell_active = false;
                }
            }
            if (out_locked) *out_locked = false;
            return 0;
        }
    }
};

// ── Edge-Qualified Arming Sequence Tracker (SWA) ────────────────────
struct ArmingTracker {
    ArmState state{ArmState::InitWaitDisable};
    uint32_t healthy_start_ms{0};
    bool     was_healthy{false};

    void update(bool swa_down, bool stick_neutral, bool link_healthy, uint32_t now_ms) {
        if (!link_healthy) {
            state = ArmState::InitWaitDisable;
            was_healthy = false;
            healthy_start_ms = 0;
            return;
        }
        if (!was_healthy) {
            was_healthy = true;
            healthy_start_ms = now_ms;
        }
        bool link_mature = (now_ms - healthy_start_ms) >= kArmingMinHealthyMs;

        switch (state) {
            case ArmState::InitWaitDisable:
                // SWA must first be observed in the DISABLED position (UP)
                if (!swa_down && link_mature) {
                    state = ArmState::Disarmed;
                }
                break;
            case ArmState::Disarmed:
                // Deliberate rising edge DOWN with neutral stick arms drive
                if (swa_down) {
                    if (stick_neutral && link_mature) {
                        state = ArmState::Armed;
                    } else {
                        // Attempted arming without neutral stick: trip back to wait disable
                        state = ArmState::InitWaitDisable;
                    }
                }
                break;
            case ArmState::Armed:
                // Immediate, unconditional disarm on SWA UP
                if (!swa_down) {
                    state = ArmState::Disarmed;
                }
                break;
        }
    }

    bool is_armed() const { return state == ArmState::Armed; }
};

// ── Pure Signal Decoder Function ────────────────────────────────────
inline RcSnapshot decode_rc_pulses(const uint32_t pulse_us[kNumSbusChannels],
                                   uint32_t last_frame_ms,
                                   uint32_t now_ms,
                                   bool hw_failsafe = false,
                                   bool hw_frame_lost = false,
                                   int32_t measured_speed_mmps = 0,
                                   ReversalTracker* rev_tracker = nullptr) {
    RcSnapshot snap;
    snap.last_update_ms = now_ms;
    snap.failsafe = hw_failsafe;
    snap.frame_lost = hw_frame_lost;

    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        snap.pulse_us[i] = pulse_us[i];
    }

    // 1. Graduated Link State Evaluation:
    uint32_t dt = (now_ms >= last_frame_ms) ? (now_ms - last_frame_ms) : 0;
    if (hw_failsafe) {
        snap.link_state = LinkState::Lost;
        snap.signal_valid = false;
    } else if (dt <= kLinkDegradedTimeoutMs) {
        snap.link_state = LinkState::Normal;
        snap.signal_valid = true;
    } else if (dt <= kLinkLostTimeoutMs) {
        snap.link_state = LinkState::Degraded;
        snap.signal_valid = true;
    } else {
        snap.link_state = LinkState::Lost;
        snap.signal_valid = false;
    }

    // 2. Channel Plausibility Check across active T12D channels (0..11)
    bool plausible = true;
    for (uint8_t i = 0; i < kNumT12dChannels; ++i) {
        if (pulse_us[i] < kPulseAbsoluteMinUs || pulse_us[i] > kPulseAbsoluteMaxUs) {
            plausible = false;
            break;
        }
    }
    // Switches must sit in one of the three legitimate clusters
    for (uint8_t i : {kChDriveEnable, kChParkHold, kChDriveEnvelope, kChAutoRequest}) {
        uint32_t p = pulse_us[i];
        bool in_low  = (p <= kClusterLowMaxUs);
        bool in_mid  = (p >= kClusterMidMinUs && p <= kClusterMidMaxUs);
        bool in_high = (p >= kClusterHighMinUs);
        if (!in_low && !in_mid && !in_high) {
            plausible = false;
            break;
        }
    }
    snap.input_plausible = plausible;

    if (!plausible) {
        snap.signal_valid = false;
        snap.link_state = LinkState::Lost;
    }

    // 3. Process Channels if Link is Valid
    if (snap.signal_valid) {
        // CH1: Right Stick X -> Steering Rack Angle (+/- 45.0 deg)
        int32_t steer_offset = static_cast<int32_t>(pulse_us[kChSteering]) - static_cast<int32_t>(kPulseCenterUs);
        if (std::abs(steer_offset) <= static_cast<int32_t>(kPulseDeadbandUs)) {
            snap.steering_deg = 0.0f;
        } else {
            float norm = static_cast<float>(steer_offset) / 450.0f;
            norm = std::clamp(norm, -1.0f, 1.0f);
            snap.steering_deg = norm * kMaxSteerAngleDeg;
        }

        // CH2: Right Stick Y -> Signed Velocity (-1.0 to +1.0)
        int32_t vel_offset = static_cast<int32_t>(pulse_us[kChVelocity]) - static_cast<int32_t>(kPulseCenterUs);
        if (std::abs(vel_offset) <= static_cast<int32_t>(kPulseDeadbandUs)) {
            snap.velocity_norm = 0.0f;
        } else {
            float norm = static_cast<float>(vel_offset) / 450.0f;
            snap.velocity_norm = std::clamp(norm, -1.0f, 1.0f);
        }

        // CH3 & CH4: Left Stick X & Y -> Implement / Aux Controls (0.0 to 1.0)
        float imp_x = static_cast<float>(static_cast<int32_t>(pulse_us[kChImplementX]) - 1000) / 1000.0f;
        float imp_y = static_cast<float>(static_cast<int32_t>(pulse_us[kChImplementY]) - 1000) / 1000.0f;
        snap.implement_x = std::clamp(imp_x, 0.0f, 1.0f);
        snap.implement_y = std::clamp(imp_y, 0.0f, 1.0f);

        // CH5: SWA -> Drive Enable Request (UP = Disabled, DOWN = Enable Request)
        snap.drive_enable_req = (pulse_us[kChDriveEnable] >= kSwitchThresholdUs);

        // CH6: SWB -> Park / Brake Hold (UP = Park/Hold, DOWN = Drive)
        snap.park_hold_req = (pulse_us[kChParkHold] < kSwitchThresholdUs);

        // CH7: SWC -> Drive Envelope (UP = Precision, MID = Normal, DOWN = Fast)
        if (pulse_us[kChDriveEnvelope] <= kModePrecisionMaxUs) {
            snap.drive_envelope = DriveEnvelope::Precision;
        } else if (pulse_us[kChDriveEnvelope] >= kModeFastMinUs) {
            snap.drive_envelope = DriveEnvelope::Fast;
        } else {
            snap.drive_envelope = DriveEnvelope::Normal;
        }

        // CH8: SWD -> Manual / Auto Request (UP = Manual, DOWN = Auto Request)
        snap.auto_mode_req = (pulse_us[kChAutoRequest] >= kSwitchThresholdUs);

        // CH9 & CH10: VRA & VRB Knobs (Auxiliary / Reserved: 0.0 to 1.0)
        float vra = static_cast<float>(static_cast<int32_t>(pulse_us[kChAuxVra]) - 1000) / 1000.0f;
        float vrb = static_cast<float>(static_cast<int32_t>(pulse_us[kChAuxVrb]) - 1000) / 1000.0f;
        snap.aux_vra = std::clamp(vra, 0.0f, 1.0f);
        snap.aux_vrb = std::clamp(vrb, 0.0f, 1.0f);

        // Determine Speed Envelope Limits
        int32_t fwd_limit = kSpeedNormalMaxMmps;
        switch (snap.drive_envelope) {
            case DriveEnvelope::Precision: fwd_limit = kSpeedPrecisionMaxMmps; break;
            case DriveEnvelope::Normal:    fwd_limit = kSpeedNormalMaxMmps;    break;
            case DriveEnvelope::Fast:      fwd_limit = kSpeedFastMaxMmps;      break;
        }
        int32_t rev_limit = kSpeedRevMaxMmps;

        // Process Reversal Protection
        if (rev_tracker) {
            snap.target_speed_mmps = rev_tracker->process_velocity(
                snap.velocity_norm, fwd_limit, rev_limit, measured_speed_mmps, now_ms, &snap.reversal_locked);
        } else {
            if (snap.velocity_norm >= 0.0f) {
                snap.target_speed_mmps = static_cast<int32_t>(std::round(snap.velocity_norm * static_cast<float>(fwd_limit)));
            } else {
                snap.target_speed_mmps = static_cast<int32_t>(std::round(snap.velocity_norm * static_cast<float>(rev_limit)));
            }
        }

        // Populate Backward-Compatibility Aliases
        snap.ignition      = snap.drive_enable_req;
        snap.switch_a      = snap.drive_enable_req;
        snap.switch_d      = snap.auto_mode_req;
        snap.dial_vra      = snap.aux_vra;
        snap.dial_vrb      = snap.aux_vrb;
        snap.throttle_norm = (snap.target_speed_mmps > 0) ? (static_cast<float>(snap.target_speed_mmps) / static_cast<float>(fwd_limit)) : 0.0f;
        snap.brake_stroke_mm = snap.park_hold_req ? kParkBrakeStrokeMm : 0.0f;
        if (snap.park_hold_req || snap.target_speed_mmps == 0) {
            snap.gear = can::Gear::N;
        } else if (snap.target_speed_mmps > 0) {
            snap.gear = can::Gear::D;
        } else {
            snap.gear = can::Gear::R;
        }
    } else {
        // Safe Failsafe Defaults on Link Loss or Fault
        snap.steering_deg      = 0.0f;
        snap.velocity_norm     = 0.0f;
        snap.target_speed_mmps = 0;
        snap.implement_x       = 0.0f;
        snap.implement_y       = 0.0f;
        snap.drive_enable_req  = false;
        snap.park_hold_req     = true; // Safe default: Park / Brake Hold active
        snap.drive_envelope    = DriveEnvelope::Precision;
        snap.auto_mode_req     = false;
        snap.aux_vra           = 0.0f;
        snap.aux_vrb           = 0.0f;
        snap.reversal_locked   = false;

        // Legacy compatibility safe clamps
        snap.brake_stroke_mm   = kMaxBrakeStrokeMm; // Emergency safe clamp
        snap.ignition          = false;
        snap.gear              = can::Gear::N;
        snap.throttle_norm     = 0.0f;
        snap.switch_a          = false;
        snap.switch_d          = false;
        snap.dial_vra          = 0.0f;
        snap.dial_vrb          = 0.0f;
    }

    return snap;
}

// Overload decoding directly from an SbusFrame
inline RcSnapshot decode_sbus_frame(const SbusFrame& frame,
                                    uint32_t last_frame_ms,
                                    uint32_t now_ms,
                                    int32_t measured_speed_mmps = 0,
                                    ReversalTracker* rev_tracker = nullptr) {
    uint32_t pulse_us[kNumSbusChannels];
    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        pulse_us[i] = sbus_to_pulse_us(frame.channels[i]);
    }
    RcSnapshot snap = decode_rc_pulses(pulse_us, last_frame_ms, now_ms,
                                       frame.failsafe, frame.frame_lost,
                                       measured_speed_mmps, rev_tracker);
    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        snap.raw_channels[i] = frame.channels[i];
    }
    return snap;
}

}  // namespace rm
