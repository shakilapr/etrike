#pragma once
// RtCore — host-pure RT control core for the full-system simulator (Phase B).
//
// Faithful port of the RT control-path semantics exercised by the vertical
// slice, mapped from rt-esp32/src/main.cpp:
//   * can_dispatch.h       0x300/0x7FC/0x7FE/0x206/0x110/0x001/0x011 decode,
//                          0x011 CRC + rolling-counter validity, the latched
//                          E-stop / two-advancing-zero asymmetric clear.
//   * safety_stream_loss.h REAL class (included): 0x011 authority acquisition
//                          UNACQUIRED -> ACQUIRED -> LOST (issue #10).
//   * run_safety_checks    estop/mode/no-authority zero, SYS+Host heartbeat
//                          timeouts, MTR-feedback health (issue #8), brake.
//   * t_can_tx_low         0x204 RT_DRIVE_CMD at 10 Hz, gear + drive gate.
//   * t_watchdog           stale host command -> zero.
//
// Platform seams (documented for the firmware-rewire milestone):
//   * time is injected (now_us); firmware maps esp_timer_get_time().
//   * frames arrive via on_frame(); firmware uses FreeRTOS RX queues.
//   * steering readiness is a bool (set_steering_active). Firmware reads
//     rt::SteeringControl::state() (EPS/SES extraction = Phase G); the drive
//     gate (AUTO && steering ACTIVE) is identical.
//   * emitted frames go to a sink callback; firmware writes the CAN drivers
//     through the gateway queues.
//
// Pure C++17, no ESP-IDF/FreeRTOS dependency: the simulator links it directly
// and firmware can call it verbatim after the task-wrapper rewire.
#include <cstdint>
#include <functional>

#include "protocol/compat/can.hpp"
#include "../safety_stream_loss.h"  // real rt::SafetyStreamSupervisor (pure C++)

namespace sim {

class RtCore {
public:
    // Mirrors rt-esp32/src/config.h estop reasons.
    static constexpr uint8_t kEstopReasonNone      = 0;
    static constexpr uint8_t kEstopReasonHeartbeat = 2;
    static constexpr uint8_t kEstopReasonCanEstop  = 5;
    static constexpr uint8_t kEstopReasonWatchdog  = 10;

    // Timeouts mirrored from rt-esp32/src/config.h + shared/shared_config.h.
    static constexpr int kControlLoopHz         = 100;
    static constexpr int kHeartbeatTimeoutMsSys  = 200;
    static constexpr int kHeartbeatTimeoutMsHost = 1500;
    static constexpr int kStartupGracePeriodMs   = 3000;
    static constexpr int kMtrFbkTimeoutMs        = 200;
    static constexpr int kMtrFbkAcquireGraceMs   = 300;
    static constexpr int kMtrFbkRecoverFrames    = 3;
    static constexpr int kMaxBrakeKpa            = 5000;
    static constexpr int kAssistStopKpa          = 2000;
    static constexpr int kLowSpeedThreshMmps     = 50;
    static constexpr int kMaxSpeedFwdMmps        = 3000;
    static constexpr int kMaxSpeedRevMmps        = 500;
    static constexpr int kMotionWindowUs         = 500000;
    static constexpr uint8_t kModeAuto           = 1;  // can::Mode::Auto

    using TxCallback = std::function<void(const etrike::protocol::Frame&)>;

    struct Snapshot {
        uint8_t mode = 0;                   // can::Mode (Manual/Auto/Estop)
        bool    estop_active = false;
        bool    no_sys_authority = true;
        bool    zero_setpoints = false;
        bool    steering_active = false;
        uint8_t estop_reason = kEstopReasonNone;
        int32_t last_setpoint_mmps = 0;
        int32_t tx_speed_mmps = 0;          // last 0x204 speed emitted
        uint8_t tx_gear = 0;                // can::Gear of last 0x204
        int32_t brake_kpa = 0;
        int32_t mtr_cmd_speed_mmps = 0;     // last 0x206 echoed speed
        bool    mtr_unavailable = false;
        int64_t last_sys_safety_sts_us = -1;
    };

    RtCore() = default;

    void set_low_tx_cb(TxCallback cb) { tx_low_ = std::move(cb); }

    void boot(int64_t now_us);
    // One simulated frame; now_us is the delivery quantum timestamp.
    void on_frame(bool from_high, const etrike::protocol::Frame& frame,
                  int64_t now_us);
    // 1 ms driver quantum; runs the 100 Hz control loop + 10 Hz low TX.
    void step(int64_t now_us);

    void set_steering_active(bool active) noexcept { steering_active_ = active; }

    Snapshot snapshot() const noexcept;
    bool estop_active() const noexcept { return estop_pending_; }
    uint8_t mode() const noexcept { return current_mode_; }
    rt::SafetyStreamState authority_state() const noexcept {
        return authority_.state();
    }

private:
    // Per-frame rolling-counter tracker: only counter deltas of 1..2 refresh
    // freshness (a frozen/duplicate producer must never look alive).
    struct RollTracker {
        bool   first = true;
        uint8_t last = 0;
        bool advance(uint8_t counter) noexcept {
            if (first) { first = false; last = counter; return true; }
            const uint8_t delta = static_cast<uint8_t>(counter - last);
            last = counter;
            return delta == 1 || delta == 2;
        }
    };

    void feed_safety_sts(int64_t now_us, const can::Frame& raw,
                         const can::gen::SysSafetySts& sts);
    void drain_events();
    void run_safety_checks(int64_t now_us);
    void resolve_setpoint(int64_t now_us);
    void tx_low_tick();

    void latch_estop(uint8_t reason) noexcept {
        if (!estop_pending_) {
            estop_pending_ = true;
            estop_reason_ = reason;
        }
    }

    TxCallback tx_low_;
    void emit_low(const can::Frame& frame) const {
        if (tx_low_) tx_low_(frame);
    }

    // ---- event flags drained by the control loop (g_safety_evt_q analog) ----
    bool    estop_pending_ = false;
    uint8_t estop_reason_  = kEstopReasonNone;
    bool    pending_estop_event_ = false;
    uint8_t pending_estop_reason_ = kEstopReasonCanEstop;
    int16_t pending_mode_event_ = -1;
    bool    pending_safety_clear_ = false;

    // ---- authority (issue #10) ----
    rt::SafetyStreamSupervisor authority_;
    bool    no_sys_authority_ = true;

    // ---- latest-value state ----
    can::gen::HostDriveCmd host_cmd_{};
    int64_t host_cmd_feed_us_ = -1;   // last valid 0x300 (t_watchdog feed)
    int64_t last_host_hb_us_ = -1;
    int64_t last_sys_hb_us_ = -1;
    int64_t last_sys_safety_sts_us_ = -1;
    int64_t last_mtr_feedback_us_ = -1;
    int64_t last_nonzero_cmd_us_ = -1;
    int32_t mtr_cmd_speed_mmps_ = 0;
    uint8_t mtr_gear_state_ = 0;
    RollTracker host_hb_roll_;
    RollTracker sys_hb_roll_;
    RollTracker mode_roll_;

    // ---- 0x011 asymmetric-clear machine (can_dispatch.h post-routing) ----
    RollTracker ssts_roll_;
    bool    ssts_latched_ = false;
    uint8_t ssts_clear_confirm_ = 0;
    bool    ssts_last_zero_ = false;
    uint8_t ssts_clear_last_ctr_ = 0;

    // ---- control-loop state (t_control locals) ----
    uint8_t current_mode_ = 0;              // 0 Manual, 1 Auto
    bool    steering_active_ = false;

    // ---- MTR feedback health supervisor (issue #8) ----
    int64_t mtr_auto_entered_us_ = -1;
    bool    mtr_prev_auto_ = false;
    bool    mtr_unavailable_ = false;
    int     mtr_recover_count_ = 0;

    int64_t last_control_tick_us_ = 0;
    int64_t last_tx_204_us_ = 0;
    bool    booted_ = false;

    // ---- per-cycle outputs ----
    bool    zero_setpoints_ = false;
    int32_t brake_kpa_ = 0;
    int32_t last_setpoint_mmps_ = 0;
    int32_t tx_speed_mmps_ = 0;
    uint8_t tx_gear_ = 0;
};

}  // namespace sim
