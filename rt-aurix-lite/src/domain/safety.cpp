// Safety supervision — pure domain logic (see safety.h).

#include "domain/safety.h"

#include <algorithm>
#include <cmath>

#include "config/timing_config.h"
#include "domain/kinematics.h"  // compute_following_error_threshold

namespace rta {

namespace {
constexpr TimeUs kUsPerMs = 1000;
constexpr TimeUs kEgasMismatchUs =
    static_cast<TimeUs>(kEgasMismatchMs) * kUsPerMs;
constexpr TimeUs kFollowErrUs =
    static_cast<TimeUs>(kSteerFollowingErrMs) * kUsPerMs;
constexpr TimeUs kHeartbeatHostUs =
    static_cast<TimeUs>(kHeartbeatTimeoutMsJetson) * kUsPerMs;
constexpr TimeUs kHeartbeatMtrUs =
    static_cast<TimeUs>(kHeartbeatTimeoutMsMtr) * kUsPerMs;
}  // namespace

SafetyResult SafetySupervisor::evaluate(TimeUs now_us, bool startup_grace,
                                        bool estop_pending, Mode mode,
                                        const MotorFeedback& motor_fb,
                                        const DriveCommand& drive_cmd,
                                        const SteeringFeedback& steer_fb,
                                        std::int16_t steer_cmd_0_1deg,
                                        bool steer_active,
                                        std::uint32_t obstacle_mm,
                                        bool host_fresh, bool mtr_fresh) {
    SafetyResult r{};

    // 1. ESTOP event / mode ESTOP — latch, zero setpoints, max brake.
    if (estop_pending || mode == Mode::Estop) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
        r.estop_reason = (estop_pending ? kEstopReasonCanEstop : kEstopReasonButton);
        r.faults |= kFaultInternal;
    }

    // Liveness — host heartbeat and MTR feedback.
    if (!host_fresh) {
        m_host_lost = true;
        // Assisted stop (not full ESTOP): zero drive + moderate brake.
        r.zero_setpoints = true;
        r.estop_reason = kEstopReasonHeartbeat;
        r.faults |= kFaultLiveness;
        if (r.brake_kpa < shared::kAssistStopKpa) r.brake_kpa = shared::kAssistStopKpa;
    } else {
        m_host_lost = false;
    }
    if (!mtr_fresh) {
        m_mtr_lost = true;
        r.zero_setpoints = true;
        r.faults |= kFaultLiveness;
        if (r.estop_reason == kEstopReasonNone) r.estop_reason = kEstopReasonHeartbeat;
    } else {
        m_mtr_lost = false;
    }

    if (startup_grace) return r;

    // 2. EGAS Level 2: compare commanded vs actual motor speed.
    if (std::abs(static_cast<std::int32_t>(motor_fb.actual_speed_mmps)
                 - static_cast<std::int32_t>(drive_cmd.motor_speed_mmps))
        > kEgasMismatchMmps) {
        // Requires persistence — tracked via freshness of the mismatch window.
        // For determinism we re-check each cycle; the app orchestrator holds
        // the accumulation window. Here we flag the fault.
        r.faults |= kFaultEgasMismatch;
        // Only escalate to ESTOP when persisted (handled by caller via
        // sustained flag), but surface the potential reason.
    }

    // 3. Steering follow-error (gated on ACTIVE and no existing zero).
    if (!r.zero_setpoints && steer_active && steer_fb.valid) {
        std::int32_t err = std::abs(static_cast<std::int32_t>(steer_cmd_0_1deg)
                                    - static_cast<std::int32_t>(steer_fb.angle_0_1deg));
        float threshold_deg = compute_following_error_threshold(
            static_cast<float>(std::abs(motor_fb.actual_speed_mmps)));
        std::int32_t threshold_0_1deg = static_cast<std::int32_t>(threshold_deg * 10.0f);
        if (err > threshold_0_1deg) {
            if (!m_follow_err_active) {
                m_follow_err_active = true;
                m_follow_err_start_us = now_us;
            } else if (now_us - m_follow_err_start_us > kFollowErrUs) {
                r.zero_setpoints = true;
                r.brake_kpa = shared::kMaxBrakeKpa;
                r.disable_steering = true;
                r.estop_reason = kEstopReasonFollowingError;
                r.faults |= kFaultSteering;
            }
        } else {
            m_follow_err_active = false;
            m_follow_err_start_us = 0;
        }
    } else {
        m_follow_err_active = false;
        m_follow_err_start_us = 0;
    }

    // 4. Obstacle-triggered ESTOP.
    if (obstacle_mm <= shared::kObstacleStopMM
        && std::abs(motor_fb.actual_speed_mmps) > shared::kLowSpeedThreshMmps) {
        r.disable_steering = true;
        r.obstacle_triggered = true;
        r.estop_reason = kEstopReasonObstacle;
        r.faults |= kFaultInternal;
    }

    return r;
}

}  // namespace rta
