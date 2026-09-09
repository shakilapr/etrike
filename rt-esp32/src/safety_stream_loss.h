#pragma once
// SYS_SAFETY_STS (0x011) authority acquisition + freshness fail-safe (issue #10).
//
// RT's drive authority is granted by the SYS 0x011 stream. Booting must never
// grant authority: a SYS that has never published a valid 0x011 is UNACQUIRED,
// not "clear". This is modelled as an explicit state machine:
//
//   UNACQUIRED (boot) --N consecutive valid frames--> ACQUIRED
//   ACQUIRED   --0x011 lost > kSysSafetyStsTimeoutUs--> LOST  (estop latch)
//
// Safety invariants (issue #10):
//   * While UNACQUIRED no propulsion/steering authority is granted (boot must
//     not be treated as "all clear").
//   * A stream that NEVER arrives within kSysSafetyAcquireTimeoutUs is a
//     SYS-absent fail-safe: motion is inhibited and a fault is reported, but a
//     global ESTOP is NOT latched — a dead SYS could never send the two-frame
//     0x011 clear, so latching would brick the vehicle.
//   * Once ACQUIRED, loss of the stream > kSysSafetyStsTimeoutUs keeps/sets the
//     E-stop latch (fail-safe) — never a silent clear.
//
// The class is pure C++ so it can be unit-tested without the control loop /
// FreeRTOS. t_control calls update() once per cycle.

#include <cstdint>

namespace rt {

constexpr int64_t kSysSafetyStsTimeoutUs     = 700000;  // ACQUIRED then lost > 700 ms
constexpr int64_t kSysSafetyAcquireTimeoutUs = 2000000; // never-received -> SYS-absent
constexpr int     kSysSafetyAcquireFrames     = 2;      // consecutive valid frames to acquire

enum class SafetyStreamState : uint8_t {
    UNACQUIRED = 0,
    ACQUIRED   = 1,
    LOST       = 2,
};

struct SafetyStreamStatus {
    SafetyStreamState state = SafetyStreamState::UNACQUIRED;
    bool estop_latch_required = false;  // ACQUIRED -> LOST: keep/set estop (fail-safe)
    bool motion_authorized     = false; // true only when ACQUIRED and stream fresh
    bool sys_absent_fault      = false; // UNACQUIRED past deadline: inhibit + fault (no estop)
};

// BUG-01: Multi-stream authority readiness bitmask.
constexpr uint8_t READY_BIT_SAFETY = 1u << 0;  // 0x011 valid & fresh (SafetyStreamSupervisor)
constexpr uint8_t READY_BIT_MODE   = 1u << 1;  // 0x110 valid & fresh (StreamValidity advancing)
constexpr uint8_t READY_BIT_HOST   = 1u << 2;  // 0x300 fresh (Host drive command received)

constexpr uint8_t kSysAuthorityRequired = READY_BIT_SAFETY | READY_BIT_MODE;
constexpr uint8_t kMotionRequired        = READY_BIT_SAFETY | READY_BIT_MODE | READY_BIT_HOST;

inline bool is_sys_authority_ready(uint8_t mask) {
    return (mask & kSysAuthorityRequired) == kSysAuthorityRequired;
}

inline bool is_motion_ready(uint8_t mask) {
    return (mask & kMotionRequired) == kMotionRequired;
}

class SafetyStreamSupervisor {
public:
    // now_us: current time. last_valid_frame_us: timestamp of the last *valid*
    // 0x011 frame (0 / -1 when never received). The caller only feeds timestamps
    // of CRC+rolling-counter-valid frames.
    SafetyStreamStatus update(int64_t now_us, int64_t last_valid_frame_us) {
        SafetyStreamStatus s;
        const bool new_frame = (last_valid_frame_us > last_rx_us_);
        if (new_frame) last_rx_us_ = last_valid_frame_us;

        switch (state_) {
        case SafetyStreamState::UNACQUIRED:
            if (new_frame) {
                // A frame gap larger than the stream cycle resets partial
                // acquisition: a few stray frames must not grant authority.
                if (acquire_count_ > 0
                    && (now_us - last_rx_us_) > int64_t(600000)) {
                    acquire_count_ = 0;
                }
                ++acquire_count_;
                if (acquire_count_ >= rt::kSysSafetyAcquireFrames)
                    state_ = SafetyStreamState::ACQUIRED;
            }
            break;

        case SafetyStreamState::ACQUIRED:
            if (!new_frame
                && (now_us - last_rx_us_) > rt::kSysSafetyStsTimeoutUs) {
                state_ = SafetyStreamState::LOST;
            }
            break;

        case SafetyStreamState::LOST:
            // Stream resumed. The ESTOP latch itself is only cleared by SYS's
            // two-frame 0x011.estop_active==0 sequence (handled upstream); here we
            // simply re-confirm authority once frames flow again.
            if (new_frame) state_ = SafetyStreamState::ACQUIRED;
            break;
        }

        const int64_t since_rx = (last_rx_us_ < 0) ? int64_t(0) : (now_us - last_rx_us_);
        s.state = state_;
        s.estop_latch_required = (state_ == SafetyStreamState::LOST);
        s.motion_authorized = (state_ == SafetyStreamState::ACQUIRED
                               && since_rx <= rt::kSysSafetyStsTimeoutUs);
        s.sys_absent_fault = (state_ == SafetyStreamState::UNACQUIRED
                              && last_rx_us_ < 0
                              && now_us - boot_us_ > rt::kSysSafetyAcquireTimeoutUs);
        return s;
    }

    // (Re)initialise. `now_us` anchors the acquisition deadline.
    void reset(int64_t now_us) {
        boot_us_ = now_us;
        state_ = SafetyStreamState::UNACQUIRED;
        last_rx_us_ = -1;
        acquire_count_ = 0;
    }

    SafetyStreamState state() const { return state_; }

private:
    int64_t boot_us_ = 0;
    int64_t last_rx_us_ = -1;  // -1 = never received a valid frame
    int     acquire_count_ = 0;
    SafetyStreamState state_ = SafetyStreamState::UNACQUIRED;
};

// Legacy freshness predicate (kept for existing harness references): 0x011 lost
// after an established stream. last_rx_us <= 0 means "never received" (startup)
// — NOT a loss. Superseded by SafetyStreamSupervisor, which additionally models
// acquisition (issue #10).
constexpr int64_t kSysSafetyStsTimeoutUsLegacy = 700000;  // 0x011 lost > 700 ms

inline bool sys_safety_sts_lost(int64_t last_rx_us, int64_t now_us) {
    return last_rx_us > 0 && (now_us - last_rx_us) > kSysSafetyStsTimeoutUsLegacy;
}

}  // namespace rt
