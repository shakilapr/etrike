#include "nodes/rt_node.hpp"
#include "protocol/codecs/ses.hpp"

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

    // System mode authority (0x110) may arrive on either bus (rm emulating SYS).
    if (cf.id == can::kIdSysModeCmd) {
        can::gen::SysModeCmd mcmd{};
        if (can::decode_frame(cf, mcmd) == can::gen::CodecStatus::Ok) {
            active_mode_ = (mcmd.mode == 1) ? can::Mode::Auto : can::Mode::Manual;
        }
    }

    // ── High CAN Ingest & Gatewaying ─────────────────────────────────
    if (is_high) {
        if (cf.id == can::kIdHostDriveCmd) {
            can::gen::HostDriveCmd cmd{};
            if (can::decode_frame(cf, cmd) == can::gen::CodecStatus::Ok) {
                host_speed_mmps_ = cmd.speed_mmps;
                host_yaw_rate_ = cmd.yaw_rate_mrad_s;
                host_gear_ = static_cast<can::Gear>(cmd.gear);
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
            can::gen::SysSafetySts smsg{};
            if (can::decode_frame(cf, smsg) == can::gen::CodecStatus::Ok) {
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
            // Forward safety status to High CAN for Host
            high_can_.send(NodeId::RT, cf);
        } else if (cf.id == can::kIdSysModeCmd) {
            can::gen::SysModeCmd mcmd{};
            if (can::decode_frame(cf, mcmd) == can::gen::CodecStatus::Ok) {
                active_mode_ = (mcmd.mode == 1) ? can::Mode::Auto : can::Mode::Manual;
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

    // Safety arbitration for motion authority:
    // If in AUTO, no ESTOP, and MTR feedback is healthy, command host speed.
    // If MTR feedback is lost: zero setpoint (recoverable inhibit), but NO global ESTOP!
    if (active_mode_ == can::Mode::Auto && !estop_latched_ && !mtr_feedback_lost_) {
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
    if (!host_brake_seen_) return;
    if (active_mode_ != can::Mode::Auto || estop_latched_) return;

    can::gen::RtBrakeCmd cmd{};
    cmd.brake_pressure_kpa = host_brake_kpa_;

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
