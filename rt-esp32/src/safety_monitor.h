#pragma once
// Safety monitor — event-driven safety checks for t_control.
//
// SafetyEvent queue replaces 3 fragile atomics (g_estop_flag, g_mode_from_sys
// dispatch→control, g_seb_takeover). Events are guaranteed delivery —
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
#include "can/can_protocol.h"
#include "steering_control.h"
#include "physics_model.h"

namespace rt {

// ── Safety event (replaces g_estop_flag, g_mode_from_sys, g_seb_takeover) ─

struct SafetyEvent {
    enum Type : uint8_t {
        ESTOP = 0,          // CAN 0x001 received or internal fault
        MODE_CHANGE,        // SYS 0x110 mode command (payload = new mode)
        SEB_TAKEOVER,       // SYS heartbeat lost → RT takes over 0x7B9
        SEB_RELEASE         // SYS heartbeat recovered → release takeover
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
};

}  // namespace rt

// ── ESTOP rate limiter (gap #14) ─────────────────────────────────────
// Prevents a corrupted node from flooding the bus with 0x001 frames.
// Max 2 frames per 500ms window.  Returns true if ESTOP should be sent.

inline bool can_send_estop() {
    constexpr int64_t kMinIntervalUs = 250'000;  // 250ms between broadcasts
    int64_t now = esp_timer_get_time();
    int64_t last = g_last_estop_sent_us.load(std::memory_order_relaxed);
    if (now - last < kMinIntervalUs) return false;
    g_last_estop_sent_us.store(now, std::memory_order_relaxed);
    return true;
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

    // 1. ESTOP event — zero everything, max brake, disable steering
    if (estop_pending) {
        estop_pending = false;
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
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
        ESP_LOGW("rt", "SYS heartbeat timeout — RT taking over brake via 0x7B9");
        r.zero_setpoints = true;
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
        g_brake_request_kpa.store(shared::kAssistStopKpa);
    }

    // 5. Steering following-error check (arch §7.6, fix #5)
    if (!r.zero_setpoints && g_steering.state() == rt::SteerState::STEER_ACTIVE) {
        static int steer_follow_err_ticks = 0;
        int16_t cmd_raw    = g_last_cmd_angle_raw.load();
        int16_t actual_raw = g_ses_angle_raw.load();
        if (actual_raw != INT16_MIN) {
            int32_t diff = int32_t(cmd_raw) - int32_t(actual_raw);
            int32_t err_mdeg = (diff >= 0 ? diff : -diff) * 100;
            float threshold_deg = rt::compute_following_error_threshold(g_mtr_actual_speed_mmps.load());
            int32_t kThresholdMdeg = static_cast<int32_t>(threshold_deg * 1000.0f);
            constexpr int kTickLimit = rt::kSteerFollowingErrMs / (1000 / rt::kControlLoopHz);
            if (err_mdeg > kThresholdMdeg) {
                if (++steer_follow_err_ticks >= kTickLimit) {
                    ESP_LOGW("rt", "Steer follow err >%.1f° for >%dms — ESTOP",
                             static_cast<double>(threshold_deg), rt::kSteerFollowingErrMs);
                    r.zero_setpoints = true;
                    r.brake_kpa = shared::kMaxBrakeKpa;
                    r.disable_steering = true;
                }
            } else {
                steer_follow_err_ticks = 0;
            }
        }
    }

    // 6. Obstacle-triggered ESTOP detection (arch §7.6, gap #9)
    if (r.disable_steering
        && obstacle_mm <= shared::kObstacleStopMM
        && std::abs(g_mtr_actual_speed_mmps.load()) > shared::kLowSpeedThreshMmps) {
        r.obstacle_triggered = true;
    }

    return r;
}
