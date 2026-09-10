// RtCore implementation — see rt_core.h for the firmware-path map.
#include "rt_core.h"

#include <algorithm>
#include <cstdlib>

namespace sim {

void RtCore::boot(int64_t now_us) {
    estop_pending_ = false;
    estop_reason_ = kEstopReasonNone;
    pending_estop_event_ = false;
    pending_estop_reason_ = kEstopReasonCanEstop;
    pending_mode_event_ = -1;
    pending_safety_clear_ = false;
    current_mode_ = 0;
    steering_active_ = false;
    no_sys_authority_ = true;
    zero_setpoints_ = false;
    brake_kpa_ = 0;
    host_cmd_ = {};
    host_cmd_feed_us_ = -1;
    last_host_hb_us_ = -1;
    last_sys_hb_us_ = -1;
    last_sys_safety_sts_us_ = -1;
    last_mtr_feedback_us_ = -1;
    last_nonzero_cmd_us_ = -1;
    mtr_cmd_speed_mmps_ = 0;
    mtr_gear_state_ = 0;
    host_hb_roll_ = RollTracker{};
    sys_hb_roll_ = RollTracker{};
    mode_roll_ = RollTracker{};
    ssts_roll_ = RollTracker{};
    ssts_latched_ = false;
    ssts_clear_confirm_ = 0;
    ssts_last_zero_ = false;
    ssts_clear_last_ctr_ = 0;
    mtr_unavailable_ = false;
    mtr_prev_auto_ = false;
    mtr_auto_entered_us_ = -1;
    mtr_recover_count_ = 0;
    last_setpoint_mmps_ = 0;
    tx_speed_mmps_ = 0;
    tx_gear_ = 0;
    last_control_tick_us_ = now_us;
    last_tx_204_us_ = now_us;
    authority_.reset(now_us);
    booted_ = true;
}

void RtCore::on_frame(bool from_high, const etrike::protocol::Frame& fr,
                      int64_t now_us) {
    const can::FrameView view = fr.view();

    // Host drive command (0x300, HIGH). Held between updates; feed drives
    // t_watchdog stale-command zeroing.
    if (fr.id == can::kIdHostDriveCmd && from_high) {
        can::gen::HostDriveCmd cmd{};
        if (can::decode_frame(view, cmd) != can::gen::CodecStatus::Ok) return;
        host_cmd_ = cmd;
        host_cmd_feed_us_ = now_us;
    }
    // Host heartbeat (0x7FC, HIGH) — advancing counter only.
    if (fr.id == can::kIdHostHeartbeat && from_high) {
        can::gen::HostHeartbeat hb{};
        if (can::decode_frame(view, hb) != can::gen::CodecStatus::Ok) return;
        if (host_hb_roll_.advance(hb.alive_ctr)) last_host_hb_us_ = now_us;
    }
    // SYS heartbeat (0x7FE, LOW) — advancing counter only.
    if (fr.id == can::kIdSysHeartbeat && !from_high) {
        can::gen::SysHeartbeat hb{};
        if (can::decode_frame(view, hb) != can::gen::CodecStatus::Ok) return;
        if (sys_hb_roll_.advance(hb.alive_ctr)) last_sys_hb_us_ = now_us;
    }
    // MTR feedback (0x206, LOW): echoed command + gear + freshness.
    if (fr.id == can::kIdMtrMotorFbk && !from_high) {
        can::gen::MtrMotorFbk fbk{};
        if (can::decode_frame(view, fbk) != can::gen::CodecStatus::Ok) return;
        mtr_cmd_speed_mmps_ = fbk.motor_command_speed_mmps;
        mtr_gear_state_ = fbk.gear_state;
        last_mtr_feedback_us_ = now_us;
    }
    // SYS mode authority (0x110, LOW): MANUAL/AUTO only (ESTOP lives on
    // 0x001/0x011), advancing counter only.
    if (fr.id == can::kIdSysModeCmd && !from_high) {
        can::gen::SysModeCmd msg{};
        if (can::decode_frame(view, msg) != can::gen::CodecStatus::Ok) return;
        if (mode_roll_.advance(msg.rolling_counter))
            pending_mode_event_ = static_cast<int16_t>(msg.mode ? kModeAuto : 0);
    }
    // 0x001 SAFETY_ESTOP (DLC 0): canonical ESTOP event (either bus).
    if (fr.id == can::kIdSafetyEstop) {
        pending_estop_event_ = true;
        pending_estop_reason_ = kEstopReasonCanEstop;
    }
    // 0x011 SYS_SAFETY_STS (LOW): authoritative estop latch + asymmetric clear.
    if (fr.id == can::kIdSysSafetySts && !from_high) {
        can::gen::SysSafetySts sts{};
        if (can::decode_frame(view, sts) != can::gen::CodecStatus::Ok) return;
        feed_safety_sts(now_us, fr, sts);
    }
}

void RtCore::feed_safety_sts(int64_t now_us, const can::Frame& raw,
                             const can::gen::SysSafetySts& sts) {
    // can_dispatch order: CRC over protected bytes [0..3] FIRST; a mismatch
    // invalidates the frame (never refresh freshness or clear credit).
    const uint8_t computed = can::e2e::sys_safety_sts_crc(raw.data.data());
    if (computed != sts.e2e_crc) return;

    // Rolling counter: duplicate / reorder must not refresh freshness.
    if (!ssts_roll_.advance(sts.rolling_counter)) return;
    last_sys_safety_sts_us_ = now_us;

    if (sts.estop_active) {
        // Assert frame: (re)start the clear sequence + (re)latch.
        ssts_latched_ = true;
        ssts_clear_confirm_ = 0;
        ssts_last_zero_ = false;
        pending_estop_event_ = true;
        pending_estop_reason_ = kEstopReasonCanEstop;
        return;
    }
    if (!ssts_latched_) {
        // Continuous zeros while NOT latched never accumulate clear credit.
        ssts_clear_confirm_ = 0;
        ssts_last_zero_ = true;
        ssts_clear_last_ctr_ = sts.rolling_counter;
        return;
    }
    // Asymmetric clear: TWO consecutive zero frames whose rolling counters
    // advance by exactly +1 from the previous clear zero. Duplicate/gap/missed
    // frame restarts the sequence from that frame as the new baseline.
    const bool first_zero = !ssts_last_zero_;
    const bool advances =
        sts.rolling_counter == static_cast<uint8_t>(ssts_clear_last_ctr_ + 1u);
    if (first_zero) {
        ssts_clear_confirm_ = 1;
    } else if (advances) {
        ++ssts_clear_confirm_;
    } else {
        ssts_clear_confirm_ = 1;
    }
    ssts_clear_last_ctr_ = sts.rolling_counter;
    ssts_last_zero_ = true;
    if (ssts_clear_confirm_ >= 2) {
        ssts_latched_ = false;
        pending_safety_clear_ = true;
    }
}

void RtCore::step(int64_t now_us) {
    if (!booted_) boot(now_us);

    if (now_us - last_control_tick_us_ >= 10'000) {
        last_control_tick_us_ = now_us;
        drain_events();
        run_safety_checks(now_us);
        resolve_setpoint(now_us);
    }
    if (now_us - last_tx_204_us_ >= 10'000) {
        last_tx_204_us_ = now_us;
        tx_low_tick();
    }
}

void RtCore::drain_events() {
    if (pending_estop_event_) {
        pending_estop_event_ = false;
        latch_estop(pending_estop_reason_);
    }
    const int16_t mode = pending_mode_event_;
    if (mode >= 0) {
        pending_mode_event_ = -1;
        // A mode change never clears an established E-stop latch.
        current_mode_ = static_cast<uint8_t>(mode);
    }
    if (pending_safety_clear_) {
        pending_safety_clear_ = false;
        estop_pending_ = false;
        estop_reason_ = kEstopReasonNone;
    }
}

void RtCore::run_safety_checks(int64_t now_us) {
    // Issue #10: authority acquisition / loss (real SafetyStreamSupervisor).
    const auto sst = authority_.update(now_us, last_sys_safety_sts_us_);
    if (sst.estop_latch_required) latch_estop(kEstopReasonCanEstop);
    no_sys_authority_ = !sst.motion_authorized;

    const bool startup_grace = now_us < int64_t(kStartupGracePeriodMs) * 1000;

    zero_setpoints_ = false;
    brake_kpa_ = 0;

    // 1. ESTOP latch / ESTOP mode -> zero + max brake.
    if (estop_pending_ || current_mode_ == 2 /* can::Mode::Estop */) {
        zero_setpoints_ = true;
        brake_kpa_ = kMaxBrakeKpa;
        if (estop_reason_ == kEstopReasonNone) estop_reason_ = kEstopReasonCanEstop;
    }
    // Issue #10: no propulsion/steering authority until 0x011 is acquired
    // (boot never grants authority).
    if (no_sys_authority_) zero_setpoints_ = true;

    if (startup_grace) return;

    // 2. SYS heartbeat timeout (200 ms) -> motion prohibited.
    if (!estop_pending_ && last_sys_hb_us_ > 0 &&
        now_us - last_sys_hb_us_ > int64_t(kHeartbeatTimeoutMsSys) * 1000) {
        zero_setpoints_ = true;
        estop_reason_ = kEstopReasonHeartbeat;
    }
    // 3. Host heartbeat timeout (1500 ms) -> assisted stop 2000 kPa.
    if (!estop_pending_ && last_host_hb_us_ > 0 &&
        now_us - last_host_hb_us_ > int64_t(kHeartbeatTimeoutMsHost) * 1000) {
        zero_setpoints_ = true;
        estop_reason_ = kEstopReasonHeartbeat;
        brake_kpa_ = kAssistStopKpa;
    }
    // 4. MTR-feedback health (issue #8): stale 0x206 in AUTO past the acquire
    //    grace -> propulsion prohibited (even at standstill); max brake only
    //    when motion was recently commanded. Confirmed recovery: 3 consecutive
    //    fresh frames at the control cadence.
    {
        const bool auto_mode = current_mode_ == kModeAuto;
        const bool fresh = last_mtr_feedback_us_ >= 0 &&
            now_us - last_mtr_feedback_us_ <= int64_t(kMtrFbkTimeoutMs) * 1000;
        if (auto_mode != mtr_prev_auto_) {
            mtr_prev_auto_ = auto_mode;
            if (auto_mode) {
                mtr_auto_entered_us_ = now_us;  // (re)start acquire grace
            } else {
                mtr_unavailable_ = false;       // leaving AUTO drops the trip
                mtr_auto_entered_us_ = -1;
                mtr_recover_count_ = 0;
            }
        }
        if (!auto_mode) {
            mtr_unavailable_ = false;
        } else if (fresh) {
            if (mtr_unavailable_) {
                if (++mtr_recover_count_ >= kMtrFbkRecoverFrames) {
                    mtr_unavailable_ = false;
                    mtr_recover_count_ = 0;
                }
            } else {
                mtr_recover_count_ = 0;
            }
        } else {
            mtr_recover_count_ = 0;
            if (mtr_auto_entered_us_ >= 0 &&
                now_us - mtr_auto_entered_us_ >
                    int64_t(kMtrFbkAcquireGraceMs) * 1000) {
                if (!mtr_unavailable_ && !estop_pending_)
                    estop_reason_ = kEstopReasonWatchdog;
                mtr_unavailable_ = true;
            }
            if (mtr_unavailable_) {
                zero_setpoints_ = true;
                if (last_nonzero_cmd_us_ >= 0 &&
                    now_us - last_nonzero_cmd_us_ <= kMotionWindowUs)
                    brake_kpa_ = kMaxBrakeKpa;
            }
        }
    }
}

void RtCore::resolve_setpoint(int64_t now_us) {
    // t_watchdog: a stale host command (no fresh 0x300) is overwritten with 0.
    if (host_cmd_feed_us_ > 0 &&
        now_us - host_cmd_feed_us_ >= int64_t(kHeartbeatTimeoutMsHost) * 1000) {
        host_cmd_ = {};
    }
    if (zero_setpoints_) {
        last_setpoint_mmps_ = 0;
        return;
    }
    // Longitudinal slice: PhysicsModel obstacle-free resolution is a clamp to
    // the vehicle envelope (real resolve(): speed + yaw -> ResolvedSetpoint).
    last_setpoint_mmps_ =
        std::clamp<int32_t>(host_cmd_.speed_mmps, -kMaxSpeedRevMmps,
                            kMaxSpeedFwdMmps);

    // Issue #8 motion window: most recent non-zero propulsion command (AUTO).
    if (current_mode_ == kModeAuto &&
        std::abs(last_setpoint_mmps_) > kLowSpeedThreshMmps) {
        last_nonzero_cmd_us_ = now_us;
    }
}

void RtCore::tx_low_tick() {
    // 0x204 RT_DRIVE_CMD at 10 Hz. MANUAL/ESTOP still publishes a keep-alive
    // {0,N}; only AUTO + steering-ready + not safety-zeroed may command speed.
    const bool motion_mode = current_mode_ == kModeAuto;
    const bool drive_allowed = motion_mode && !zero_setpoints_ && steering_active_;

    int32_t speed_out = 0;
    uint8_t gear_out = uint8_t(can::Gear::N);
    if (drive_allowed) {
        speed_out = last_setpoint_mmps_;
        if (host_cmd_.gear != 0) {
            gear_out = host_cmd_.gear;  // CAN override
        } else if (speed_out > 0) {
            gear_out = uint8_t(can::Gear::D);
        } else if (speed_out < 0) {
            gear_out = uint8_t(can::Gear::R);
        } else {
            gear_out = uint8_t(can::Gear::N);
        }
    }
    tx_speed_mmps_ = speed_out;
    tx_gear_ = gear_out;

    can::gen::RtDriveCmd msg{};
    msg.motor_speed_mmps = speed_out;
    msg.gear = gear_out;
    can::Frame fr;
    if (can::encode_frame(msg, fr) == can::gen::CodecStatus::Ok) emit_low(fr);
}

RtCore::Snapshot RtCore::snapshot() const noexcept {
    Snapshot s{};
    s.mode = current_mode_;
    s.estop_active = estop_pending_;
    s.no_sys_authority = no_sys_authority_;
    s.zero_setpoints = zero_setpoints_;
    s.steering_active = steering_active_;
    s.estop_reason = estop_reason_;
    s.last_setpoint_mmps = last_setpoint_mmps_;
    s.tx_speed_mmps = tx_speed_mmps_;
    s.tx_gear = tx_gear_;
    s.brake_kpa = brake_kpa_;
    s.mtr_cmd_speed_mmps = mtr_cmd_speed_mmps_;
    s.mtr_unavailable = mtr_unavailable_;
    s.last_sys_safety_sts_us = last_sys_safety_sts_us_;
    return s;
}

}  // namespace sim
