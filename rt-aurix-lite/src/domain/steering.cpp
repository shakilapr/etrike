// Steering controller — pure domain logic (see steering.h).

#include "domain/steering.h"

#include <algorithm>
#include <cmath>

#include "domain/kinematics.h"  // compute_dynamic_limit

namespace rta {

namespace {
constexpr std::int64_t kUsPerMs = 1000;

constexpr std::int64_t kBootWaitUs =
    static_cast<std::int64_t>(kSteerBootWaitMs) * kUsPerMs;
constexpr std::int64_t kSyncTimeoutUs =
    static_cast<std::int64_t>(kSteerSyncTimeoutMs) * kUsPerMs;
constexpr std::int64_t kEstopHoldUs =
    static_cast<std::int64_t>(kSteerEstopHoldMs) * kUsPerMs;
constexpr std::int64_t kFollowErrUs =
    static_cast<std::int64_t>(kSteerFollowingErrMs) * kUsPerMs;
}  // namespace

void SteeringControl::build_command(SteeringCommand& out) {
    out.valid = true;
    out.angle_0_1deg = m_active_angle_0_1deg;
    out.rolling_counter = m_roll;
    m_roll = (m_roll + 1) & 0x0Fu;

    // Dynamic slew rate: 125°/s at low speed, 525°/s at high speed.
    float speed_kmh = std::abs(static_cast<float>(m_speed_mmps)) * 3.6f / 1000.0f;
    float rate_deg_s = kSteerRateMinDegS
        + (speed_kmh - 2.0f) * (kSteerRateRangeDegS / kAngleClampSpeedRange);
    out.speed_raw = static_cast<std::uint16_t>(
        std::clamp(rate_deg_s, kSteerRateMinDegS, kSteerRateMaxDegS));
    out.vehicle_speed_raw = static_cast<std::uint8_t>(std::clamp(speed_kmh, 0.0f, 255.0f));
}

bool SteeringControl::tick(const SteeringFeedback& fb, TimeUs now_us, SteeringCommand& out) {
    // BOOT_WAIT: 500 ms power-on delay.
    if (m_state == SteerState::BOOT_WAIT) {
        if (++m_timer >= (kSteerBootWaitMs * kSteerCmdRateHz) / 1000) {
            m_state = SteerState::LISTEN_SYNC;
            m_timer = 0;
            m_sync_start_us = now_us;
        }
        out.valid = false;
        return false;
    }

    if (m_state == SteerState::LISTEN_SYNC) {
        // Timeout: 5 s without valid aligned feedback -> FAULT.
        if (now_us - m_sync_start_us > kSyncTimeoutUs) {
            m_state = SteerState::FAULT;
            out.valid = false;
            return false;
        }
        // Wait for valid, aligned, plausible feedback.
        if (!fb.valid) { out.valid = false; return false; }
        if (!fb.angle_aligned) { out.valid = false; return false; }
        // Plausibility: at power-on wheels should be near center (>30° => FAULT).
        if (std::abs(static_cast<std::int32_t>(fb.angle_0_1deg)) > 300) {
            m_state = SteerState::FAULT;
            out.valid = false;
            return false;
        }
        // Synchronized — capture current angle.
        m_active_angle_0_1deg = fb.angle_0_1deg;
        m_state = SteerState::ACTIVE;
        build_command(out);
        return true;
    }

    if (m_state == SteerState::ACTIVE) {
        build_command(out);
        return true;
    }

    if (m_state == SteerState::ESTOP_RAMP_TO_ZERO) {
        // Ramp toward 0° at 20°/s (200 0.1°/s); at 50 Hz => 4 0.1° per tick.
        constexpr std::int16_t kRampStep =
            static_cast<std::int16_t>(kSteerEstopRampDegS * 10.0f / 50.0f);
        if (m_active_angle_0_1deg > kRampStep) {
            m_active_angle_0_1deg -= kRampStep;
        } else if (m_active_angle_0_1deg < -kRampStep) {
            m_active_angle_0_1deg += kRampStep;
        } else {
            m_active_angle_0_1deg = 0;
            if (m_estop_exit_pending) {
                m_state = SteerState::ACTIVE;
                m_estop_exit_pending = false;
            }
            // Ramp complete — hold at 0°, continue transmitting.
        }

        // Following-error check during centering ramp (linkage jam detection).
        if (fb.valid) {
            std::int32_t err = std::abs(static_cast<std::int32_t>(m_active_angle_0_1deg)
                                        - static_cast<std::int32_t>(fb.angle_0_1deg));
            if (err > 50) {  // 5° = 50 in 0.1° units
                if (m_estop_follow_err_start_us == 0) {
                    m_estop_follow_err_start_us = now_us;
                } else if (now_us - m_estop_follow_err_start_us > 1000 * kUsPerMs) {
                    m_state = SteerState::FAULT;
                    out.valid = false;
                    return false;
                }
            } else {
                m_estop_follow_err_start_us = 0;
            }
        }

        build_command(out);
        return true;
    }

    if (m_state == SteerState::ESTOP_HOLD_THEN_SILENT) {
        if (now_us - m_estop_hold_start_us < kEstopHoldUs) {
            m_active_angle_0_1deg = m_estop_hold_angle;
            build_command(out);
            return true;
        }
        if (m_estop_exit_pending) {
            m_state = SteerState::ACTIVE;
            m_estop_exit_pending = false;
        } else {
            m_state = SteerState::FAULT;
        }
        out.valid = false;
        return false;
    }

    // FAULT — stop transmitting.
    out.valid = false;
    return false;
}

void SteeringControl::start_estop(bool obstacle_triggered) noexcept {
    m_estop_exit_pending = false;  // new ESTOP overrides any pending exit
    if (m_state != SteerState::ACTIVE) return;  // first trigger wins
    if (obstacle_triggered) {
        // Clamp hold angle to dynamic limit for current speed.
        float max_deg = compute_dynamic_limit(static_cast<float>(std::abs(m_speed_mmps)));
        std::int16_t max_raw = static_cast<std::int16_t>(max_deg * 10.0f);
        m_state = SteerState::ESTOP_HOLD_THEN_SILENT;
        m_estop_hold_angle = std::clamp(m_active_angle_0_1deg,
                                        static_cast<std::int16_t>(-max_raw), max_raw);
        m_estop_hold_start_us = 0;
    } else {
        m_state = SteerState::ESTOP_RAMP_TO_ZERO;
        m_estop_follow_err_start_us = 0;
    }
}

}  // namespace rta
