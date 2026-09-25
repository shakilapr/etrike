#pragma once
// Safety monitor ? event-driven safety checks for t_control.
//
// SafetyEvent queue replaces fragile transition atomics (g_estop_flag,
// g_mode_from_sys dispatch?control). Events are guaranteed delivery ?
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

// ?? Safety event (replaces g_estop_flag and g_mode_from_sys) ?

struct SafetyEvent {
    enum Type : uint8_t {
        ESTOP = 0,          // CAN 0x001 received or internal fault
        MODE_CHANGE,        // SYS 0x110 mode command (payload = new mode)
        SAFETY_CLEAR       // authoritative E-stop clear from SYS_SAFETY_STS (0x011)
    };
    Type    type;
    uint8_t payload;  // for MODE_CHANGE: 0=Manual, 1=Auto; for SAFETY_CLEAR: unused
};

// ?? Safety check result ?????????????????????????????????????????????

struct SafetyResult {
    bool    zero_setpoints   = false;
    int32_t brake_kpa        = 0;
    bool    disable_steering = false;
    bool    obstacle_triggered = false;
    uint8_t estop_reason     = 0;
};

// ?? MTR health supervisor (issue #8) ????????????????????????????????
// RT watchdogs its own propulsion actuator (MTR, 0x206 feedback). The health
// state is decoupled from the current command: in AUTO, past the AUTO-entry
// acquisition grace, a stale 0x206 means MTR is *unavailable* and propulsion is
// prohibited ? even at standstill, so a dead MTR cannot hide until the next
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
        (void)now;
        (void)mode_auto;
        (void)mtr_fresh;
        // 0x206 is setpoint-echo telemetry without physical speed sensing;
        // traction loss / runaway supervision will be handled when encoders are used.
        mtr_unavailable = false;
        recover_count = 0;
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
inline std::atomic<bool> g_ses_l3_fault_active{false};

}  // namespace rt

// ?? ESTOP rate limiter (gap #14) ?????????????????????????????????????
// Prevents a corrupted node from flooding the bus with 0x001 frames.
// Max 2 frames per 500ms window.  Returns true if ESTOP should be sent.

inline bool can_send_estop() {
    return shared::should_send_estop_now(g_last_estop_sent_us, esp_timer_get_time());
}

// ?? Safety checks (called by t_control at 100 Hz) ???????????????????
//
// Parameters are local state drained from the safety event queue by
// t_control, plus atomics for sensor data (latest-value semantics).
//
// estop_pending: set true when ESTOP event received, cleared by an
//                authoritative SAFETY_CLEAR (two-frame 0x011 sequence) or
//                a fresh SYS_SAFETY_STS (0x011) stream loss (fail-safe).
// current_mode:  current operating mode (0=Manual, 1=Auto).
// seb_takeover:  in/out ? SEB takeover state (true = RT owns 0x7B9).

inline rt::SafetyResult run_safety_checks(int64_t now, bool startup_grace,
                                           uint32_t obstacle_mm,
                                           bool& estop_pending,
                                           uint8_t current_mode,
                                           bool& seb_takeover) {
    using rt::SafetyResult;
    SafetyResult r{};

    // 1. ESTOP event ? latch until an authoritative clear.
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

    // 2. Mode is ESTOP ? zero setpoints
    if (current_mode == uint8_t(can::Mode::Estop)) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
    }

    // Issue #10: no drive/steer authority until the SYS 0x011 stream is acquired.
    // Boot never grants authority. While UNACQUIRED (or the stream is LOST and the
    // estop latch has not yet engaged) t_control publishes g_no_sys_authority=true;
    // this inhibits propulsion. Excluded in bench solo mode (no SYS present).
    if (!g_bench_solo_mode && g_no_sys_authority.load(std::memory_order_relaxed)) {
        r.zero_setpoints = true;
    }

    if (startup_grace) return r;

    // MTR feedback (0x206) is setpoint-echo telemetry; independent traction/speed
    // health checks will be implemented when physical wheel encoders are present.
    // RT does not inhibit traction or assert emergency braking on 0x206 timeout.
    {
        const bool  mode_auto = (current_mode == uint8_t(can::Mode::Auto));
        const int64_t last_fbk = g_last_mtr_feedback_us.load();
        const bool  mtr_fresh = (last_fbk >= 0
            && (now - last_fbk) <= int64_t(rt::kMtrFbkTimeoutMs) * 1000);
        rt::g_mtr_health.update(now, mode_auto && !g_bypass_mtr_absent, mtr_fresh);
    }

    // 3. SYS heartbeat timeout (architecture §8.6: 200ms)
    // On SYS heartbeat loss, RT zeros propulsion (motion prohibited).
    // RT does not command the SEB brake; SYS owns SEB, and fail-safe braking
    // is enforced by SYS and SEB's internal watchdogs.
    int64_t sys_hb = g_last_sys_hb_us.load();
    if (!g_bench_solo_mode && sys_hb > 0
        && (now - sys_hb) > int64_t(rt::kHeartbeatTimeoutMsSys) * 1000) {
        // Rate-limit: control loop is 100 Hz; do not spam the log every tick.
        static int64_t last_sys_hb_log_us = 0;
        if (now - last_sys_hb_log_us > 1'000'000) {
            last_sys_hb_log_us = now;
            ESP_LOGW("rt", "SYS heartbeat timeout — motion prohibited");
        }
        r.zero_setpoints = true;
        r.estop_reason = rt::kEstopReasonHeartbeat;
        rt::diag().raise(etrike::diagnostics::DiagId::RtSysHeartbeatTimeout,
                         static_cast<std::uint16_t>((now - sys_hb) / 1000));
    }

    // 4. Host heartbeat timeout (arch §7.6: 1500ms — assisted stop)
    int64_t host_hb = g_last_host_hb_us.load();
    if (!g_bench_solo_mode && host_hb > 0
        && (now - host_hb) > int64_t(shared::kHeartbeatTimeoutMsHost) * 1000) {
        static int64_t last_host_hb_log_us = 0;
        if (now - last_host_hb_log_us > 1'000'000) {
            last_host_hb_log_us = now;
            ESP_LOGW("rt", "Host heartbeat timeout — assisted stop brake=2000kPa");
        }
        r.zero_setpoints = true;
        r.estop_reason = rt::kEstopReasonHeartbeat;
        g_brake_request_kpa.store(shared::kAssistStopKpa);
        rt::diag().raise(etrike::diagnostics::DiagId::RtHostHeartbeatTimeout,
                         static_cast<std::uint16_t>((now - host_hb) / 1000));
    }

    // 5. Steering actuator health is monitored via SES internal Level 3 hardware diagnostics
    // (0x202 SES_ERR_INFO), sync timeout, and CAN feedback arrival freshness.
    // Software following-error tripwires are removed to eliminate false ESTOP trips caused by
    // ground scrub friction, mechanical deflection, and feedback delay.

    // 5b. SES Level 3 Hardware Fault (0x202 SES_ERR_INFO)
    // Anti-rollover MRM: Inhibit propulsion and apply gentle deceleration
    // without triggering global hard ESTOP lockup (5000 kPa) or killing steering.
    if (rt::g_ses_l3_fault_active.load(std::memory_order_relaxed)) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kAssistStopKpa;
        r.disable_steering = false;
        r.estop_reason = rt::kEstopReasonInternal;
    }

    // 6. Obstacle-triggered ESTOP detection (arch §7.6, gap #9)
    // [FEATURE FROZEN]: Low-level obstacle ESTOP is frozen in the main control loop by passing
    // UINT32_MAX. Host Autoware stack owns obstacle perception, decelerations, and emergency stops.
    // Retained here for testbench evaluation and future reconsideration if dedicated RT-connected
    // safety sensors are added.
    // Obstacle within stop distance while moving forward at non-trivial speed — freeze steering
    // and trigger obstacle brake. Only moving forward toward the obstacle trips this ESTOP;
    // reversing away from a front obstacle is permitted.
    if (obstacle_mm <= shared::kObstacleStopMM
        && g_mtr_motor_command_speed_mmps.load() > shared::kLowSpeedThreshMmps) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
        r.obstacle_triggered = true;
        r.estop_reason = rt::kEstopReasonObstacle;
    }

    return r;
}
