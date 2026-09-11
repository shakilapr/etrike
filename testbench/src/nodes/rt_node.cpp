#include "nodes/rt_node.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/compat/e2e.hpp"
#include "shared_config.h"

namespace testbench {

RtNode::RtNode(ICanBus& high_can, ICanBus& low_can)
    : high_can_(high_can), low_can_(low_can) {
    init();
}

void RtNode::init() {
    estop_latched_ = false;
    software_estop_active_ = false;
    active_mode_ = can::Mode::Manual;
    host_speed_mmps_ = 0;
    host_yaw_rate_ = 0;
    commanded_speed_mmps_ = 0;
    host_steer_0_1deg_ = 0;
    host_brake_kpa_ = 0;
    host_steer_seen_ = false;
    host_brake_seen_ = false;
    host_hb_seen_ = false;
    host_hb_lost_ = false;
    last_host_hb_ms_ = 0;
    last_host_hb_ctr_ = 0;
    host_hb_first_ = true;

    // Authority gates: boot is UNACQUIRED (never "all clear").
    safety_sup_.reset(0);
    safety_val_inited_ = false;
    last_safety_sts_ms_ = -1;
    sys_estop_ = false;
    mode_val_inited_ = false;
    mode_valid_ = false;
    last_mode_ms_ = 0;
    host_drive_seen_ = false;
    last_host_drive_ms_ = 0;
    safety_ok_ = false;
    mode_ok_ = false;
    host_ok_ = false;
    last_steer_tx_ms_ = 0;
    last_brake_tx_ms_ = 0;
    rt_hb_ctr_ = 0;
    rt_clear_confirm_count_ = 0;
    last_drive_tx_ms_ = 0;
    last_hb_tx_ms_ = 0;
    last_state_tx_ms_ = 0;
    last_now_ms_ = 0;
    last_mtr_fbk_ms_ = 0;
    consecutive_mtr_fbk_count_ = 0;
    mtr_feedback_lost_ = false;
}

void RtNode::trigger_software_estop() {
    estop_latched_ = true;
    software_estop_active_ = true;
    commanded_speed_mmps_ = 0;

    // Broadcast 0x001 on both buses (DLC 0)
    can::Frame cf = can::Frame::standard(can::kIdSafetyEstop, 0);
    high_can_.send(NodeId::RT, cf);
    low_can_.send(NodeId::RT, cf);
}

void RtNode::clear_software_estop() {
    software_estop_active_ = false;
    rt_clear_confirm_count_ = 0;
}

void RtNode::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    can::Frame cf = frame;
    bool is_high = (bus_name == high_can_.name());

    // ── High CAN Ingest & Gatewaying ─────────────────────────────────
    if (is_high) {
        if (cf.id == can::kIdHostDriveCmd) {
            can::gen::HostDriveCmd cmd{};
            if (can::decode_frame(cf, cmd) == can::gen::CodecStatus::Ok) {
                host_speed_mmps_ = cmd.speed_mmps;
                host_yaw_rate_ = cmd.yaw_rate_mrad_s;
                host_gear_ = static_cast<can::Gear>(cmd.gear);
                host_drive_seen_ = true;
                last_host_drive_ms_ = last_now_ms_;
            }
        } else if (cf.id == can::kIdHostSteerCmd) {
            can::gen::HostSteerCmd scmd{};
            if (can::decode_frame(cf, scmd) == can::gen::CodecStatus::Ok) {
                host_steer_0_1deg_ = scmd.steer_angle_0_1deg;
                host_steer_seen_ = true;
            }
        } else if (cf.id == can::kIdHostBrakeReq) {
            can::gen::HostBrakeReq bcmd{};
            if (can::decode_frame(cf, bcmd) == can::gen::CodecStatus::Ok) {
                host_brake_kpa_ = bcmd.brake_pressure_kpa;
                host_brake_seen_ = true;
            }
        } else if (cf.id == can::kIdHostHeartbeat) {
            can::gen::HostHeartbeat hb{};
            if (can::decode_frame(cf, hb) == can::gen::CodecStatus::Ok) {
                // Only an advancing alive counter re-arms the watchdog
                // (frozen counter == stuck producer; main.cpp:391-397).
                const uint8_t delta = hb.alive_ctr - last_host_hb_ctr_;
                if (host_hb_first_ || delta != 0) {
                    host_hb_first_ = false;
                    last_host_hb_ctr_ = hb.alive_ctr;
                    last_host_hb_ms_ = last_now_ms_;
                    host_hb_seen_ = true;
                    host_hb_lost_ = false;
                }
            }
        } else if (cf.id == can::kIdSafetyEstop) {
            estop_latched_ = true;
            commanded_speed_mmps_ = 0;
            // Gateway forward High -> Low
            low_can_.send(NodeId::RT, cf);
        } else if (cf.id == can::kIdHmiModeReq || cf.id == can::kIdHmiPwrReq || cf.id == can::kIdHostLightCmd) {
            // Gateway forward High -> Low
            low_can_.send(NodeId::RT, cf);
        }
    }
    // ── Low CAN Ingest & Gatewaying ──────────────────────────────────
    else {
        if (cf.id == can::kIdSafetyEstop) {
            estop_latched_ = true;
            commanded_speed_mmps_ = 0;
            // Gateway forward Low -> High (never echo back to Low!)
            high_can_.send(NodeId::RT, cf);
        } else if (cf.id == can::kIdSysSafetySts) {
            // 0x011 is rt's authoritative E-stop / safety authority source, but
            // ONLY on the low bus (can_dispatch.h:157). Validate the AUTOSAR E2E
            // CRC before feeding the readiness supervisor (safety_stream_loss.h).
            can::gen::SysSafetySts smsg{};
            if (can::decode_frame(cf, smsg) == can::gen::CodecStatus::Ok) {
                if (!safety_val_inited_) {
                    safety_val_.set_key(1, can::kIdSysSafetySts, 700);
                    safety_val_inited_ = true;
                }
                const uint8_t crc = can::e2e::sys_safety_sts_crc(cf.data.data());
                if (crc != smsg.e2e_crc) {
                    safety_val_.invalidate_now();
                } else if (safety_val_.observe(smsg.rolling_counter, last_now_ms_)) {
                    // Valid AND counter advancing: authoritative (main.cpp:712-715).
                    last_safety_sts_ms_ = last_now_ms_;
                    sys_estop_ = smsg.estop_active;
                    if (smsg.estop_active) {
                        estop_latched_ = true;
                        rt_clear_confirm_count_ = 0;
                        commanded_speed_mmps_ = 0;
                    } else if (!software_estop_active_) {
                        // Requires 2 consecutive clear frames before unlatching
                        rt_clear_confirm_count_++;
                        if (rt_clear_confirm_count_ >= 2) {
                            estop_latched_ = false;
                        }
                    }
                }
                // A frozen/invalid counter leaves last_safety_sts_ms_ stale;
                // SafetyStreamSupervisor then trips the fail-safe in step().
            }
            // Forward safety status to High CAN for Host
            high_can_.send(NodeId::RT, cf);
        } else if (cf.id == can::kIdSysModeCmd) {
            // 0x110 is authoritative ONLY on the low bus (can_rx_router.h:64-84).
            // A high-bus 0x110 (e.g. rm emulating SYS) must be ignored so the
            // model cannot mask a wrong-bus deployment.
            can::gen::SysModeCmd mcmd{};
            if (can::decode_frame(cf, mcmd) == can::gen::CodecStatus::Ok) {
                if (!mode_val_inited_) {
                    mode_val_.set_key(1, can::kIdSysModeCmd,
                                      can::gen::SysModeCmd::kCycleMs * 5);
                    mode_val_inited_ = true;
                }
                const bool ok = mode_val_.observe(mcmd.rolling_counter, last_now_ms_);
                mode_valid_ = ok;
                if (ok) {
                    active_mode_ = (mcmd.mode == 1) ? can::Mode::Auto : can::Mode::Manual;
                }
                last_mode_ms_ = last_now_ms_;
            }
        } else if (cf.id == can::kIdMtrMotorFbk) {
            last_mtr_fbk_ms_ = last_now_ms_;
            consecutive_mtr_fbk_count_++;
            if (consecutive_mtr_fbk_count_ >= 3) {
                mtr_feedback_lost_ = false;
            }
        }
    }
}

void RtNode::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    last_now_ms_ = now_ms;

    // Check MTR feedback timeout (200 ms threshold)
    if (now_ms > 1000 && last_mtr_fbk_ms_ > 0 && (now_ms - last_mtr_fbk_ms_ > 200)) {
        consecutive_mtr_fbk_count_ = 0;
        mtr_feedback_lost_ = true;
    }

    if (software_estop_active_) {
        estop_latched_ = true;
    }

    // Host heartbeat timeout (1500 ms) -> assisted stop (rt-esp32 safety_monitor.h:225).
    if (host_hb_seen_ && (now_ms - last_host_hb_ms_ > 1500)) {
        host_hb_lost_ = true;
    }

    // SYS authority readiness (mirrors rt-esp32 main.cpp:1100 + safety_stream_loss.h).
    // SAFETY (0x011, LOW) | MODE (0x110, LOW) | HOST (0x300, HIGH). Boot is
    // UNACQUIRED; a never-seen 0x011 can never license motion.
    {
        const int64_t now_us = static_cast<int64_t>(now_ms) * 1000;
        const int64_t last_us = (last_safety_sts_ms_ < 0)
                                    ? -1
                                    : static_cast<int64_t>(last_safety_sts_ms_) * 1000;
        rt::SafetyStreamStatus sst = safety_sup_.update(now_us, last_us);
        if (sst.estop_latch_required) estop_latched_ = true;  // ACQUIRED -> LOST fail-safe
        safety_ok_ = sst.motion_authorized && !sys_estop_;

        const uint32_t mode_window = can::gen::SysModeCmd::kCycleMs * 5;
        mode_ok_ = mode_valid_ && (now_ms - last_mode_ms_ <= mode_window)
                   && active_mode_ == can::Mode::Auto;

        host_ok_ = host_drive_seen_
                   && (now_ms - last_host_drive_ms_ <= shared::kHostCmdStaleTimeoutMs);
    }

    // Motion requires ALL THREE readiness bits, no ESTOP, healthy MTR feedback and
    // Host heartbeat. A missing bit zeroes the setpoint (recoverable inhibit) —
    // never a global ESTOP.
    if (safety_ok_ && mode_ok_ && host_ok_ && !estop_latched_
        && !mtr_feedback_lost_ && !host_hb_lost_) {
        commanded_speed_mmps_ = host_speed_mmps_;
    } else {
        commanded_speed_mmps_ = 0;
    }

    // 10 ms: 0x204 RT_DRIVE_CMD on Low CAN
    if (now_ms - last_drive_tx_ms_ >= 10) {
        publish_drive_cmd(now_ms);
        last_drive_tx_ms_ = now_ms;
    }

    // 20 ms: 0x169 VCU_SES_REQ (steer -> SES) and 0x205 RT_BRAKE_CMD (brake -> SYS)
    if (now_ms - last_steer_tx_ms_ >= 20) {
        publish_steer_cmd(now_ms);
        last_steer_tx_ms_ = now_ms;
    }
    if (now_ms - last_brake_tx_ms_ >= 20) {
        publish_brake_cmd(now_ms);
        last_brake_tx_ms_ = now_ms;
    }

    // 20 ms: RT Heartbeat
    if (now_ms - last_hb_tx_ms_ >= 20) {
        publish_heartbeat(now_ms);
        last_hb_tx_ms_ = now_ms;
    }

    // 100 ms: 0x210 RT_STATE_RPT
    if (now_ms - last_state_tx_ms_ >= 100) {
        publish_state_report(now_ms);
        last_state_tx_ms_ = now_ms;
    }
}

void RtNode::publish_drive_cmd(uint32_t now_ms) {
    (void)now_ms;
    can::gen::RtDriveCmd cmd{};
    cmd.motor_speed_mmps = commanded_speed_mmps_;
    cmd.gear = static_cast<uint8_t>(host_gear_);

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::RT, cf);
    }
}

void RtNode::publish_steer_cmd(uint32_t now_ms) {
    (void)now_ms;
    if (!host_steer_seen_) return;
    if (active_mode_ != can::Mode::Auto || estop_latched_) return;

    can::custom::ses::Command cmd{};
    cmd.alignment_enable = true;
    cmd.control_enable   = true;
    cmd.target_angle_raw = static_cast<int16_t>(30000 + host_steer_0_1deg_);
    cmd.target_speed_raw = 328;
    cmd.rolling_counter  = static_cast<uint8_t>(ses_counter_++ & 0x0F);

    can::Frame cf;
    if (can::custom::ses::encode_command(cmd, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::RT, cf);
    }
}

void RtNode::publish_brake_cmd(uint32_t now_ms) {
    (void)now_ms;
    // Emit on host brake intent, or unconditionally on Host-heartbeat loss
    // (assisted stop, 2000 kPa) regardless of mode.
    if (!host_brake_seen_ && !host_hb_lost_) return;
    if (!host_hb_lost_ && (active_mode_ != can::Mode::Auto || estop_latched_)) return;

    constexpr int32_t kAssistStopKpa = 2000;
    can::gen::RtBrakeCmd cmd{};
    cmd.brake_pressure_kpa = host_hb_lost_
        ? (host_brake_kpa_ > kAssistStopKpa ? host_brake_kpa_ : kAssistStopKpa)
        : host_brake_kpa_;

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::RT, cf);
    }
}

void RtNode::publish_heartbeat(uint32_t now_ms) {
    (void)now_ms;
    can::gen::RtHeartbeat hb{};
    hb.alive_ctr = rt_hb_ctr_++;

    can::Frame cf;
    if (can::encode_frame(hb, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::RT, cf);
        high_can_.send(NodeId::RT, cf);
    }
}

void RtNode::publish_state_report(uint32_t now_ms) {
    (void)now_ms;
    can::gen::RtStateRpt rpt{};
    rpt.mode = (active_mode_ == can::Mode::Auto) ? 1 : 0;
    rpt.safety_state = estop_latched_ ? 1 : 0;
    rpt.reversing = 0;
    rpt.rx_overflow = 0;
    rpt.task_health = 0xFF;
    rpt.steer_state = 2; // Active

    can::Frame cf;
    if (can::encode_frame(rpt, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::RT, cf);
        high_can_.send(NodeId::RT, cf);
    }
}

} // namespace testbench
