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
#include "system_mode.h"
#include "diag_rt.h"

namespace rt {

// ── Safety event (replaces g_estop_flag and g_mode_from_sys) ─

struct SafetyEvent {
    enum Type : uint8_t {
        ESTOP = 0,          // CAN 0x001 received or internal fault
        MODE_CHANGE,        // SYS 0x110 mode command (payload = new mode)
        SAFETY_CLEAR       // authoritative E-stop clear from SYS_SAFETY_STS (0x011)
    };
    Type    type;
    uint8_t payload;  // for MODE_CHANGE: 0=Manual, 1=Auto; for SAFETY_CLEAR: unused
};

// ── Safety check result ─────────────────────────────────────────────

struct SafetyResult {
    bool    zero_setpoints   = false;
    int32_t brake_kpa        = 0;
    bool    disable_steering = false;
    bool    obstacle_triggered = false;
    uint8_t estop_reason     = 0;
};

// ── MTR health supervisor (issue #8) ────────────────────────────────
// RT watchdogs its own propulsion actuator (MTR, 0x206 feedback). The health
// state is decoupled from the current command: in AUTO, past the AUTO-entry
// acquisition grace, a stale 0x206 means MTR is *unavailable* and propulsion is
// prohibited — even at standstill, so a dead MTR cannot hide until the next
// acceleration request. Brake escalation is a separate decision made by the
// caller (whether motion had recently been commanded).
//
// The acquisition grace is relative to entering AUTO (not boot), so a long
// MANUAL soak cannot expire it early. Recovery is confirmed: kMtrFbkRecoverFrames
// consecutive fresh 0x206 frames at the control-loop cadence.
struct MtrHealthSupervisor {
    int64_t auto_entered_us = -1;   // when AUTO was (re)entered, us
    bool    prev_auto = false;
    bool    mtr_unavailable = false;
    int     recover_count = 0;

    void update(int64_t now, bool mode_auto, bool mtr_fresh) {
        if (mode_auto != prev_auto) {
            if (mode_auto) {
                auto_entered_us = now;   // (re)start the acquisition grace
                recover_count = 0;
                // Do not clear mtr_unavailable here: an existing trip must be
                // confirmed-recovered, not masked by a mode bounce.
            } else {
                // Leaving AUTO drops the drive dependency: clear trip + state.
                mtr_unavailable = false;
                auto_entered_us = -1;
                recover_count = 0;
            }
        }
        prev_auto = mode_auto;
        if (!mode_auto) return;

        if (mtr_fresh) {
            if (mtr_unavailable) {
                if (++recover_count >= rt::kMtrFbkRecoverFrames) {
                    mtr_unavailable = false;
                    recover_count = 0;
                }
            } else {
                recover_count = 0;
            }
            return;
        }

        // mtr stale
        recover_count = 0;
        if (auto_entered_us >= 0
            && now - auto_entered_us > int64_t(rt::kMtrFbkAcquireGraceMs) * 1000) {
            mtr_unavailable = true;
        }
    }

    void reset() {
        auto_entered_us = -1;
        prev_auto = false;
        mtr_unavailable = false;
        recover_count = 0;
    }
};

// Global MTR-health supervisor (defined in main.cpp; declared here so the
// free-inline run_safety_checks() below shares one instance across TUs).
extern MtrHealthSupervisor g_mtr_health;

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
// estop_pending: set true when ESTOP event received, cleared by an
//                authoritative SAFETY_CLEAR (two-frame 0x011 sequence) or
//                a fresh SYS_SAFETY_STS (0x011) stream loss (fail-safe).
// current_mode:  current operating mode (0=Manual, 1=Auto).
// seb_takeover:  in/out — SEB takeover state (true = RT owns 0x7B9).

inline rt::SafetyResult run_safety_checks(int64_t now, bool startup_grace,
                                           uint32_t obstacle_mm,
                                           bool& estop_pending,
                                           uint8_t current_mode,
                                           bool& seb_takeover) {
    using rt::SafetyResult;
    SafetyResult r{};

    // 1. ESTOP event — latch until an authoritative clear.
    // CAN 0x001 is not a one-shot; it holds ESTOP until SYS releases it via
    // SYS_SAFETY_STS (0x011) estop_active==0 for two consecutive fresh frames
    // (asymmetric clear), or until the 0x011 stream is lost (fail-safe in
    // t_control). The SAFETY_CLEAR handler in t_control clears m_estop_pending.
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

    // Issue #8: MTR feedback health — RT watchdogs its own propulsion actuator.
    // In AUTO, past the AUTO-entry acquisition grace, a stale 0x206 makes MTR
    // *unavailable*: propulsion is prohibited even at standstill (so a dead MTR
    // cannot hide until the next acceleration request). Brake escalation is a
    // separate decision — max brake only if a non-zero propulsion command was
    // recently active, because 0x206 speed is an echoed command and true motion
    // is not measurable without an independent sensor. Recovery is confirmed:
    // kMtrFbkRecoverFrames consecutive fresh 0x206 frames.
    {
        const bool  mode_auto = (current_mode == uint8_t(can::Mode::Auto));
        const int64_t last_fbk = g_last_mtr_feedback_us.load();
        const bool  mtr_fresh = (last_fbk >= 0
            && (now - last_fbk) <= int64_t(rt::kMtrFbkTimeoutMs) * 1000);
        rt::g_mtr_health.update(now, mode_auto && !g_bypass_mtr_absent, mtr_fresh);
        if (rt::g_mtr_health.mtr_unavailable) {
            const bool prior_zero = r.zero_setpoints;
            r.zero_setpoints = true;
            if (!prior_zero) {
                r.estop_reason = rt::kEstopReasonWatchdog;
                rt::diag().raise(etrike::diagnostics::DiagId::RtMtrFbkTimeout,
                                 static_cast<std::uint16_t>(
                                     last_fbk >= 0 ? (now - last_fbk) / 1000 : 0));
            }
            const int64_t last_nonzero = g_last_nonzero_cmd_us.load();
            constexpr int64_t kMotionWindowUs = 500'000;  // 500 ms
            if (last_nonzero >= 0 && (now - last_nonzero) <= kMotionWindowUs) {
                r.brake_kpa = shared::kMaxBrakeKpa;
            }
        }
    }

    // 3. SYS heartbeat timeout (architecture §8.6: 200ms)
    // Issue #3: heartbeat loss ALONE does not grant RT brake ownership. RT
    // zeros propulsion here (motion prohibited) and enters SYS_DEGRADED; the
    // SEB brake fallback machine (brake_fallback.h) decides whether RT must
    // become the emergency 0x7B9 writer (only once SYS's 0x7B9 has also
    // disappeared). seb_takeover is owned by that machine, not this check.
    int64_t sys_hb = g_last_sys_hb_us.load();
    if (!g_bench_solo_mode && sys_hb > 0
        && (now - sys_hb) > int64_t(rt::kHeartbeatTimeoutMsSys) * 1000) {
        // Rate-limit: control loop is 100 Hz; do not spam the log every tick.
        static int64_t last_sys_hb_log_us = 0;
        if (now - last_sys_hb_log_us > 1'000'000) {
            last_sys_hb_log_us = now;
            ESP_LOGW("rt", "SYS heartbeat timeout — motion prohibited (brake ownership pending)");
        }
        r.zero_setpoints = true;
        r.estop_reason = rt::kEstopReasonHeartbeat;
        rt::diag().raise(etrike::diagnostics::DiagId::RtSysHeartbeatTimeout,
                         static_cast<std::uint16_t>((now - sys_hb) / 1000));
    }

    // 4. Host heartbeat timeout (arch §7.6: 1500ms → assisted stop)
    int64_t host_hb = g_last_host_hb_us.load();
    if (!g_bench_solo_mode && host_hb > 0
        && (now - host_hb) > int64_t(shared::kHeartbeatTimeoutMsHost) * 1000) {
        ESP_LOGW("rt", "Host heartbeat timeout — assisted stop brake=2000kPa");
        r.zero_setpoints = true;
        r.estop_reason = rt::kEstopReasonHeartbeat;
        g_brake_request_kpa.store(shared::kAssistStopKpa);
        rt::diag().raise(etrike::diagnostics::DiagId::RtHostHeartbeatTimeout,
                         static_cast<std::uint16_t>((now - host_hb) / 1000));
    }

    // 5. Steering following-error check (arch §7.6, fix #5)
    static int steer_follow_err_ticks = 0;
    if (!g_bypass_eps_sync && !r.zero_setpoints
        && g_steering.state() == rt::SteerState::STEER_ACTIVE) {
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
                    rt::diag().raise(etrike::diagnostics::DiagId::RtSteerFollowingError,
                                     static_cast<std::uint16_t>(err_0_1deg));
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
