#pragma once
// SteeringControl — steer-by-wire unit command generation (0x169 VCU_SES_REQ).
//
// TASK OWNERSHIP: This class is owned by t_control (prio 4). All mutation
// (tick, start_estop, exit_estop, set_target, reset) must happen from
// t_control only. t_watchdog and t_can_tx_low may READ state() but must
// NOT mutate. This is enforced by convention, not by mutex — adding a
// mutex would risk priority inversion (p1 watchdog blocks p4 control).
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "config.h"
#include "esp_log.h"
#include "protocol/codecs/ses.hpp"
#include "physics_model.h"
#include "system_mode.h"
#include "physics_model.h"
namespace rt {
enum class SteerState : uint8_t {
    STEER_BOOT_WAIT,          // 500ms power-on delay — do NOT transmit
    STEER_LISTEN_SYNC,        // Waiting for 0x201 SES_STATUS (angle + alignment)
    STEER_ACTIVE,             // Normal operation — transmit 0x169 at 50 Hz
    ESTOP_RAMP_TO_ZERO,       // Non-obstacle ESTOP: ramp to 0° at 20°/s, then hold
    ESTOP_HOLD_THEN_SILENT,   // Obstacle ESTOP: hold 500ms, then silent-stop
    STEER_FAULT               // Timeout or silent-stop — stop transmitting
};
class SteeringControl {
public:
    void init() {
        m_state = SteerState::STEER_BOOT_WAIT;
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
              uint32_t now_ms, etrike::protocol::codecs::ses::Command& out) {
        constexpr int kBootWaitTicks = (kSteerBootWaitMs * kSteerCmdRateHz) / 1000;
        switch (m_state) {
        case SteerState::STEER_BOOT_WAIT:
            if (++m_timer >= kBootWaitTicks) {
                m_state = SteerState::STEER_LISTEN_SYNC;
                m_timer = 0;
                m_sync_start_ms = now_ms;
            }
            return false;

        case SteerState::STEER_LISTEN_SYNC: {
            if (g_bypass_eps_sync && g_bench_solo_mode) {
                // Bench mode: skip EPS-C listen-sync, assume centered
                m_active_angle = 0;
                m_state = SteerState::STEER_ACTIVE;
                build_command(out);
                return true;
            }
            // Timeout check (gap C1): 5s without valid 0x201 → FAULT
            if (now_ms - m_sync_start_ms > static_cast<uint32_t>(kSteerSyncTimeoutMs)) {
                m_state = SteerState::STEER_FAULT;
                return false;
            }
            // Wait for valid angle data
            if (ses_angle_raw == INT16_MIN) return false;
            // Alignment check (gap C2): EPS-C must report angle_status == 1
            if (ses_angle_status == 0) return false;  // still center-finding
            // Angle plausibility: at power-on wheels should be near center.
            // If >30° off, likely wrong offset or sensor fault — refuse ACTIVE.
            if (std::abs(ses_angle_raw) > 300) {  // 30° in 0.1° units
                ESP_LOGE("steer", "Angle implausible at sync: %d (0.1°) — check offset", ses_angle_raw);
                m_state = SteerState::STEER_FAULT;
                return false;
            }
            // Synchronized — capture current angle (ses_angle_raw is already offset-free 0.1°)
            m_active_angle = ses_angle_raw;
            m_state = SteerState::STEER_ACTIVE;
            build_command(out);
            return true;
        }

        case SteerState::STEER_ACTIVE:
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
                // Gap #6: if exit was requested during ramp, transition to ACTIVE now
                if (m_estop_exit_pending) {
                    m_state = SteerState::STEER_ACTIVE;
                    m_estop_exit_pending = false;
                }
                // Ramp complete — hold at 0°, continue transmitting
            }

            // Gap C3: following-error check during ESTOP centering ramp.
            // Both m_active_angle and ses_angle_raw are offset-free 0.1° units.
            // Use int32_t to avoid overflow when angles span large ranges
            // (e.g., m_active_angle=30000, ses_angle_raw=-30000 → diff=60000,
            //  which overflows int16_t).
            if (ses_angle_raw != INT16_MIN) {
                int32_t err = std::abs(int32_t(m_active_angle) - int32_t(ses_angle_raw));
                if (err > 50) {  // 5° = 50 in 0.1° units
                    if (m_estop_following_err_start_ms == 0)
                        m_estop_following_err_start_ms = now_ms;
                    else if (now_ms - m_estop_following_err_start_ms > 1000) {
                        m_state = SteerState::STEER_FAULT;
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
            // Gap #6: if exit was requested during hold, transition to ACTIVE.
            // Otherwise silent-stop (standard obstacle ESTOP behavior).
            if (m_estop_exit_pending) {
                m_state = SteerState::STEER_ACTIVE;
                m_estop_exit_pending = false;
            } else {
                m_state = SteerState::STEER_FAULT;
            }
            return false;
        }

        case SteerState::STEER_FAULT:
            return false;
        }
        return false;
    }

    // Set the desired steering angle (called by control task at 100 Hz).
    // angle_mdeg: millidegrees, +right. speed_mmps: current speed for slew rate calc.
    void set_target(int32_t angle_mdeg, int32_t speed_mmps) {
        if (m_state == SteerState::STEER_ACTIVE) {
            m_active_angle = int16_t(angle_mdeg / 100);
            m_speed_mmps = speed_mmps;
            m_estop_exit_pending = false;  // Gap #6: fresh command clears pending exit
        }
        // In ESTOP states, ignore target updates — ramp/hold uses its own angle
    }

    // Trigger ESTOP behavior. obstacle_triggered=true → hold-then-silent.
    // obstacle_triggered=false → ramp-to-zero. Called from safety checks.
    void start_estop(bool obstacle_triggered) {
        m_estop_exit_pending = false;  // Gap #6: new ESTOP overrides any pending exit
        if (m_state == SteerState::STEER_ACTIVE) {
            if (obstacle_triggered) {
                // Gap #9: clamp hold angle to dynamic limit for current speed.
                // At high speed the dynamic limit may be as low as 5° — holding an
                // angle beyond that during hard braking risks rollover.
                // compute_dynamic_limit expects mm/s, returns degrees.
                float max_deg = compute_dynamic_limit(static_cast<float>(std::abs(m_speed_mmps)));
                int16_t max_raw = static_cast<int16_t>(max_deg * 10.0f);  // 0.1° units
                m_state = SteerState::ESTOP_HOLD_THEN_SILENT;
                m_estop_hold_angle = std::clamp(m_active_angle, int16_t(-max_raw), max_raw);
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
        if (m_state == SteerState::STEER_FAULT) {
            m_state = SteerState::STEER_LISTEN_SYNC;
            m_sync_start_ms = now_ms;
        }
    }

    // Gap #6: Exit ESTOP states — deferred until ramp/hold completes.
    // Pressing START during centering ramp must NOT stop 0x169 mid-ramp.
    // The steering ramp completes first, then transitions to ACTIVE.
    // Brake/motor/lights transition immediately (handled by caller).
    void exit_estop() {
        if (m_state == SteerState::ESTOP_RAMP_TO_ZERO ||
            m_state == SteerState::ESTOP_HOLD_THEN_SILENT) {
            m_estop_exit_pending = true;  // defer until ramp/hold completes
        }
    }

private:
    void build_command(etrike::protocol::codecs::ses::Command& out) {
        out.alignment_enable = true;
        out.control_enable = 1;
        out.target_angle_raw = m_active_angle + kSbwAngleOffset;  // 0.1° → CAN raw (steer-by-wire offset)
        // Dynamic slew rate: 125°/s at low speed, 525°/s at high speed
        float speed_kmh = std::abs(m_speed_mmps) * 3.6f / 1000.0f;
        float rate_deg_s = kSteerRateMinDegS
            + (speed_kmh - 2.0f) * (kSteerRateRangeDegS / kAngleClampSpeedRange);
        out.target_speed_raw = static_cast<uint16_t>(std::clamp(rate_deg_s, kSteerRateMinDegS, kSteerRateMaxDegS));
        out.rolling_counter = m_roll;
        m_roll = (m_roll + 1) & kRollCounterMask;
        out.vehicle_speed_raw = static_cast<uint8_t>(std::clamp(speed_kmh, 0.0f, 255.0f));
    }

    static constexpr int kRollCounterMask = 0x0F;
    SteerState m_state = SteerState::STEER_BOOT_WAIT;
    int        m_timer = 0;
    int16_t    m_active_angle = 0;
    uint8_t    m_roll = 0;
    int32_t    m_speed_mmps = 0;
    uint32_t   m_sync_start_ms = 0;
    uint32_t   m_estop_hold_start_ms = 0;
    int16_t    m_estop_hold_angle = 0;
    uint32_t   m_estop_following_err_start_ms = 0;
    bool       m_estop_exit_pending = false;  // Gap #6: deferred exit flag
};
}
