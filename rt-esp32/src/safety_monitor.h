#pragma once
// Safety monitor — event-driven safety checks for t_control.
//
// SafetyEvent queue replaces fragile transition atomics (g_estop_flag,
// g_mode_from_sys dispatch→control). Events are guaranteed delivery —
// no transition is missed, unlike atomic exchange() which can drop events.
//
// Architecture principle #1: "Queues over shared state."

#include <cstdint>
#include <algorithm>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rt_state.h"
#include "config.h"
#include "shared_config.h"
#include "protocol/compat/can.hpp"
#include "steering_control.h"
#include "physics_model.h"

namespace rt {

// ── Safety event (replaces g_estop_flag and g_mode_from_sys) ─

struct SafetyEvent {
    enum Type : uint8_t {
        ESTOP = 0,          // CAN 0x001 received or internal fault
        MODE_CHANGE         // SYS 0x110 mode command (payload = new mode)
    };
    Type    type;
    uint8_t payload;  // for MODE_CHANGE: 0=Manual, 1=Auto, 2=Estop
};

// ── Safety check result ─────────────────────────────────────────────

struct SafetyResult {
    bool    zero_setpoints   = false;
    int32_t brake_kpa        = 0;
    bool    disable_steering = false;
    bool    obstacle_triggered = false;
    uint8_t estop_reason     = 0;
};

}  // namespace rt

// ── ESTOP rate limiter (gap #14) ─────────────────────────────────────
// Prevents a corrupted node from flooding the bus with 0x001 frames.
// Max 2 frames per 500ms window.  Returns true if ESTOP should be sent.

inline bool can_send_estop() {
    return shared::should_send_estop_now(g_last_estop_sent_us, esp_timer_get_time());
}

// ── Safety checks (called by t_control at 100 Hz) ───────────────────
//
// Parameters are local state drained from the safety event queue by
// t_control, plus atomics for sensor data (latest-value semantics).
//
// estop_pending: set true when ESTOP event received, cleared by mode change
//                away from ESTOP.  Consumed (set to false) by this function.
// current_mode:  current operating mode (0=Manual, 1=Auto, 2=Estop).
// seb_takeover:  in/out — SEB takeover state (true = RT owns 0x7B9).

inline rt::SafetyResult run_safety_checks(int64_t now, bool startup_grace,
                                           uint32_t obstacle_mm,
                                           bool& estop_pending,
                                           uint8_t current_mode,
                                           bool& seb_takeover) {
    using rt::SafetyResult;
    SafetyResult r{};

    // 1. ESTOP event — latch until SYS mode clears it (PCR3).
    // CAN 0x001 is not a one-shot; it holds ESTOP until SYS explicitly
    // transitions away from ESTOP mode via 0x110 MODE_CMD. The MODE_CHANGE
    // handler in t_control clears m_estop_pending on non-ESTOP transitions.
    if (estop_pending) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
        r.estop_reason = rt::kEstopReasonCanEstop;
    }

    // 2. Mode is ESTOP — zero setpoints
    if (current_mode == uint8_t(can::Mode::Estop)) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
    }

    if (startup_grace) return r;

    // 3. SYS heartbeat timeout (architecture §8.6: 200ms)
    int64_t sys_hb = g_last_sys_hb_us.load();
    if (sys_hb > 0 && (now - sys_hb) > int64_t(rt::kHeartbeatTimeoutMsSys) * 1000) {
        // Rate-limit: control loop is 100 Hz; do not spam the log every tick.
        static int64_t last_sys_hb_log_us = 0;
        if (now - last_sys_hb_log_us > 1'000'000) {
            last_sys_hb_log_us = now;
            ESP_LOGW("rt", "SYS heartbeat timeout — RT taking over brake via 0x7B9");
        }
        r.zero_setpoints = true;
        r.estop_reason = rt::kEstopReasonHeartbeat;
        seb_takeover = true;
    } else if (seb_takeover) {
        // SYS heartbeat recovered — release takeover
        seb_takeover = false;
    }

    // 4. Host heartbeat timeout (arch §7.6: 1500ms → assisted stop)
    int64_t host_hb = g_last_host_hb_us.load();
    if (host_hb > 0 && (now - host_hb) > int64_t(shared::kHeartbeatTimeoutMsHost) * 1000) {
        ESP_LOGW("rt", "Host heartbeat timeout — assisted stop brake=2000kPa");
        r.zero_setpoints = true;
        r.estop_reason = rt::kEstopReasonHeartbeat;
        g_brake_request_kpa.store(shared::kAssistStopKpa);
    }

    // 5. Steering following-error check (arch §7.6, fix #5)
    static int steer_follow_err_ticks = 0;
    if (!r.zero_setpoints && g_steering.state() == rt::SteerState::STEER_ACTIVE) {
        int16_t cmd_0_1deg    = g_last_cmd_angle_0_1deg.load();
        int16_t actual_0_1deg = g_ses_angle_0_1deg.load();
        if (actual_0_1deg != INT16_MIN) {
            int32_t diff = int32_t(cmd_0_1deg) - int32_t(actual_0_1deg);
            int32_t err_0_1deg = (diff >= 0 ? diff : -diff);
            float threshold_deg = rt::compute_following_error_threshold(g_mtr_actual_speed_mmps.load());
            int32_t threshold_0_1deg = static_cast<int32_t>(threshold_deg * 10.0f);
            constexpr int kTickLimit = rt::kSteerFollowingErrMs / (1000 / rt::kControlLoopHz);
            if (err_0_1deg > threshold_0_1deg) {
                if (++steer_follow_err_ticks >= kTickLimit) {
                    ESP_LOGW("rt", "Steer follow err >%.1f° for >%dms — ESTOP",
                             static_cast<double>(threshold_deg), rt::kSteerFollowingErrMs);
                    r.zero_setpoints = true;
                    r.brake_kpa = shared::kMaxBrakeKpa;
                    r.disable_steering = true;
                    r.estop_reason = rt::kEstopReasonFollowingError;
                }
            } else {
                steer_follow_err_ticks = 0;
            }
        }
    } else {
        steer_follow_err_ticks = 0;  // Reset on state transitions (e.g., ESTOP → recovery)
    }

    // 6. Obstacle-triggered ESTOP detection (arch §7.6, gap #9)
    // Obstacle within stop distance at non-trivial speed → freeze steering
    // and trigger obstacle brake. Must set disable_steering independently of
    // ESTOP events — the previous condition required disable_steering to already
    // be true, which only ESTOP events set, making this dead code (bug 4.11).
    if (obstacle_mm <= shared::kObstacleStopMM
        && std::abs(g_mtr_actual_speed_mmps.load()) > shared::kLowSpeedThreshMmps) {
        r.disable_steering = true;
        r.obstacle_triggered = true;
        r.estop_reason = rt::kEstopReasonObstacle;
    }

    return r;
}
