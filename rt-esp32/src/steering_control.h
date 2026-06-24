#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "config.h"
#include "can/can_protocol.h"
#include "physics_model.h"
namespace rt {
enum class SteerState : uint8_t {
    BOOT_WAIT,            // 500ms power-on delay — do NOT transmit
    LISTEN_SYNC,          // Waiting for 0x201 SES_STATUS (angle + alignment)
    ACTIVE,               // Normal operation — transmit 0x169 at 50 Hz
    ESTOP_RAMP_TO_ZERO,   // Non-obstacle ESTOP: ramp to 0° at 20°/s, then hold
    ESTOP_HOLD_THEN_SILENT, // Obstacle ESTOP: hold 500ms, then silent-stop
    FAULT                 // Timeout or silent-stop — stop transmitting
};
class SteeringControl {
public:
    void init() {
        m_state = SteerState::BOOT_WAIT;
        m_timer = 0;
        m_active_angle = 0;
        m_roll = 0;
        m_speed_mmps = 0;
        m_sync_start_ms = 0;
        m_estop_hold_start_ms = 0;
        m_estop_hold_angle = 0;
        m_estop_following_err_start_ms = 0;
    }
    SteerState state() const { return m_state; }

    // Call at 50 Hz. ses_angle_raw = 0.1° units from 0x201 (INT16_MIN if no data).
    // ses_angle_status = 0x201 byte0 bit0 (0=center finding, 1=aligned).
    // now_ms = monotonic millisecond counter for timeouts.
    // Returns true if `out` should be transmitted on CAN.
    bool tick(int16_t ses_angle_raw, uint8_t ses_angle_status,
              uint32_t now_ms, can::VcuSesReq& out) {
        constexpr int kBootWaitTicks = 25;  // 50 Hz × 500 ms
        switch (m_state) {
        case SteerState::BOOT_WAIT:
            if (++m_timer >= kBootWaitTicks) {
                m_state = SteerState::LISTEN_SYNC;
                m_timer = 0;
                m_sync_start_ms = now_ms;
            }
            return false;

        case SteerState::LISTEN_SYNC: {
            // Timeout check (gap C1): 5s without valid 0x201 → FAULT
            if (now_ms - m_sync_start_ms > static_cast<uint32_t>(kSteerSyncTimeoutMs)) {
                m_state = SteerState::FAULT;
                return false;
            }
            // Wait for valid angle data
            if (ses_angle_raw == INT16_MIN) return false;
            // Alignment check (gap C2): EPS-C must report angle_status == 1
            if (ses_angle_status == 0) return false;  // still center-finding
            // Synchronized — capture current angle, transition to ACTIVE
            m_active_angle = ses_angle_raw;
            m_state = SteerState::ACTIVE;
            build_command(out);
            return true;
        }

        case SteerState::ACTIVE:
            build_command(out);
            return true;

        case SteerState::ESTOP_RAMP_TO_ZERO: {
            // Ramp toward 0° at kSteerEstopRampDegS (20°/s)
            // m_active_angle is in 0.1° units. Ramp rate = 20°/s = 200 (0.1°)/s.
            // At 50 Hz tick rate: 200/50 = 4 (0.1°) per tick.
            constexpr int kRampStep = static_cast<int>(kSteerEstopRampDegS * 10.0f / 50.0f);
            if (m_active_angle > kRampStep) {
                m_active_angle -= kRampStep;
            } else if (m_active_angle < -kRampStep) {
                m_active_angle += kRampStep;
            } else {
                m_active_angle = 0;
                // Ramp complete — hold at 0°, continue transmitting
            }

            // Gap C3: following-error check during ESTOP centering ramp.
            // If mechanical jam causes >5° error for >1s, fall back to silent-stop.
            if (ses_angle_raw != INT16_MIN) {
                int16_t err = std::abs(m_active_angle - ses_angle_raw);
                if (err > 50) {  // 5° = 50 in 0.1° units
                    if (m_estop_following_err_start_ms == 0)
                        m_estop_following_err_start_ms = now_ms;
                    else if (now_ms - m_estop_following_err_start_ms > 1000) {
                        m_state = SteerState::FAULT;
                        return false;  // silent-stop — linkage likely jammed
                    }
                } else {
                    m_estop_following_err_start_ms = 0;
                }
            }

            build_command(out);
            return true;  // continue transmitting during ramp
        }

        case SteerState::ESTOP_HOLD_THEN_SILENT: {
            // Hold current angle for kSteerEstopHoldMs, then silent-stop
            if (now_ms - m_estop_hold_start_ms < static_cast<uint32_t>(kSteerEstopHoldMs)) {
                // Still in hold phase — transmit current hold angle
                m_active_angle = m_estop_hold_angle;
                build_command(out);
                return true;
            }
            // Hold period expired → silent-stop
            m_state = SteerState::FAULT;
            return false;
        }

        case SteerState::FAULT:
            return false;
        }
        return false;
    }

    // Set the desired steering angle (called by control task at 100 Hz).
    // angle_mdeg: millidegrees, +right. speed_mmps: current speed for slew rate calc.
    void set_target(int32_t angle_mdeg, int32_t speed_mmps) {
        if (m_state == SteerState::ACTIVE) {
            m_active_angle = int16_t(angle_mdeg / 100);
            m_speed_mmps = speed_mmps;
        }
        // In ESTOP states, ignore target updates — ramp/hold uses its own angle
    }

    // Trigger ESTOP behavior. obstacle_triggered=true → hold-then-silent.
    // obstacle_triggered=false → ramp-to-zero. Called from safety checks.
    void start_estop(bool obstacle_triggered) {
        if (m_state == SteerState::ACTIVE) {
            if (obstacle_triggered) {
                // Gap #9: clamp hold angle to dynamic limit for current speed.
                // At high speed the dynamic limit may be as low as 5° — holding an
                // angle beyond that during hard braking risks rollover.
                float speed_kmh = std::abs(m_speed_mmps) * 3.6f / 1000.0f;
                float max_deg = compute_dynamic_limit(speed_kmh);
                int16_t max_raw = static_cast<int16_t>(max_deg * 10.0f);  // 0.1° units
                m_state = SteerState::ESTOP_HOLD_THEN_SILENT;
                m_estop_hold_angle = std::clamp(m_active_angle, -max_raw, max_raw);
                m_estop_hold_start_ms = 0;
            } else {
                m_state = SteerState::ESTOP_RAMP_TO_ZERO;
                m_estop_following_err_start_ms = 0;
                // ramp starts from current m_active_angle toward 0°
            }
        }
        // If already in an ESTOP state, no change — first trigger wins
    }

    // Set hold timestamp for obstacle ESTOP (called after start_estop).
    void set_estop_hold_time(uint32_t now_ms) {
        if (m_state == SteerState::ESTOP_HOLD_THEN_SILENT && m_estop_hold_start_ms == 0) {
            m_estop_hold_start_ms = now_ms;
        }
    }

    // Reset from FAULT to LISTEN_SYNC (START button short-press retry).
    void reset_to_listen(uint32_t now_ms) {
        if (m_state == SteerState::FAULT) {
            m_state = SteerState::LISTEN_SYNC;
            m_sync_start_ms = now_ms;
        }
    }

    // Exit ESTOP states back to ACTIVE (when mode transitions away from ESTOP).
    void exit_estop() {
        if (m_state == SteerState::ESTOP_RAMP_TO_ZERO ||
            m_state == SteerState::ESTOP_HOLD_THEN_SILENT) {
            m_state = SteerState::ACTIVE;
        }
    }

private:
    void build_command(can::VcuSesReq& out) {
        out.align_enable = 1;
        out.control_enable = 1;
        out.target_angle = m_active_angle;
        // Dynamic slew rate: 125°/s at low speed, 525°/s at high speed
        float speed_kmh = std::abs(m_speed_mmps) * 3.6f / 1000.0f;
        float rate_deg_s = kSteerRateMinDegS
            + (speed_kmh - 2.0f) * (kSteerRateRangeDegS / kAngleClampSpeedRange);
        out.target_speed = static_cast<int>(std::clamp(rate_deg_s, kSteerRateMinDegS, kSteerRateMaxDegS));
        out.roll_cnt_enable = 1;
        out.checksum_enable = 1;
        out.rolling_counter = m_roll;
        m_roll = (m_roll + 1) & kRollCounterMask;
        out.vehicle_speed = static_cast<uint8_t>(std::clamp(speed_kmh, 0.0f, 255.0f));
    }

    static constexpr int kRollCounterMask = 0x0F;
    SteerState m_state = SteerState::BOOT_WAIT;
    int        m_timer = 0;
    int16_t    m_active_angle = 0;
    uint8_t    m_roll = 0;
    int32_t    m_speed_mmps = 0;
    uint32_t   m_sync_start_ms = 0;
    uint32_t   m_estop_hold_start_ms = 0;
    int16_t    m_estop_hold_angle = 0;
    uint32_t   m_estop_following_err_start_ms = 0;
};
}
