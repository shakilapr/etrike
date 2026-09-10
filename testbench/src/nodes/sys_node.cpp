#include "nodes/sys_node.hpp"
#include <algorithm>
#include <cmath>
#include "protocol/compat/e2e.hpp"
#include "protocol/codecs/seb.hpp"
#include "shared_config.h"

namespace testbench {

SysNode::SysNode(ICanBus& low_can)
    : low_can_(low_can) {
    init();
}

void SysNode::init() {
    mode_mgr_.init();
    safety_.init();
    brake_ctrl_.init();

    hw_estop_pressed_ = false;
    hw_start_pressed_ = false;
    hw_mode_pressed_ = false;

    safety_seq_ctr_ = 0;
    mode_seq_ctr_ = 0;
    pwr_seq_ctr_ = 0;
    hb_seq_ctr_ = 0;
    seb_seq_ctr_ = 0;

    last_safety_tx_ms_ = 0;
    last_mode_tx_ms_ = 0;
    last_pwr_tx_ms_ = 0;
    last_hb_tx_ms_ = 0;
    last_seb_tx_ms_ = 0;
    last_now_ms_ = 0;

    last_mtr_fbk_ms_ = 0;
    consecutive_mtr_fbk_count_ = 0;

    last_demanded_stroke_mm_ = 0.0f;
    following_excursion_start_ms_ = 0;
    rt_brake_kpa_ = 0;

    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    g_seb_error_status.store(0);
    g_seb_status_byte0.store(0xFF);
    sys::mark_estop_broadcast(0);
    sys::mark_estop_reset(0);
}

void SysNode::press_start_button() {
    hw_start_pressed_ = true;
}

void SysNode::press_mode_button() {
    hw_mode_pressed_ = true;
}

void SysNode::hold_mode_button_3s() {
    for (int i = 0; i < 35; ++i) {
        mode_mgr_.tick(true, false);
    }
    mode_mgr_.tick(false, false);
    if (mode_mgr_.mode() != can::Mode::Estop && !hw_estop_pressed_ && safety_.heartbeat_ok()) {
        safety_.set_estop(false);
        sys::mark_estop_reset(last_now_ms_);
    }
}

void SysNode::broadcast_estop() {
    sys::mark_estop_broadcast(last_now_ms_);
    can::Frame cf = can::Frame::standard(can::kIdSafetyEstop, 0);
    low_can_.send(NodeId::SYS, cf);
}

void SysNode::press_estop_button() {
    hw_estop_pressed_ = true;
    safety_.set_estop(true);
    mode_mgr_.force_estop();
    broadcast_estop();
}

void SysNode::release_estop_button() {
    hw_estop_pressed_ = false;
    safety_.set_estop(false);
}

void SysNode::set_brake_lever(bool pressed) {
    safety_.set_brake_lever(pressed);
}

void SysNode::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    can::Frame cf = frame;

    if (cf.id == can::kIdSafetyEstop) {
        if (!sys::rx_estop_suppressed(last_now_ms_)) {
            safety_.set_estop(true);
            mode_mgr_.force_estop();
        }
    } else if (cf.id == can::kIdRtBrakeCmd) {
        // RT expresses service-brake intent via 0x205 (kPa); SYS applies it to
        // SEB (0x7B9). Mirrors sys-esp32/src/main.cpp:296/892-906.
        can::gen::RtBrakeCmd brk{};
        if (can::gen::decode_rt_brake_cmd(cf.view(), brk) == can::gen::CodecStatus::Ok) {
            rt_brake_kpa_ = brk.brake_pressure_kpa;
        }
    } else if (cf.id == etrike::protocol::codecs::seb::kStatusId) {
        // Canonical decode: validates id, DLC and the XOR-8/0xFF checksum that
        // the hand-rolled parser used to ignore.
        etrike::protocol::codecs::seb::Status st{};
        if (etrike::protocol::codecs::seb::decode_status(cf.view(), st)
            != etrike::protocol::CodecStatus::Ok) {
            return;
        }
        g_seb_status_byte0.store(st.status_byte);
        g_seb_error_status.store(st.error_status);

        if (st.error_status == 3) {
            sys::set_latched_fault(sys::kLatchedSebL3);
            safety_.set_estop(true);
            mode_mgr_.force_estop();
        }

        float actual_stroke = (static_cast<float>(st.stroke_value_raw) * shared::kBrakeStrokeScale)
                              + shared::kBrakeStrokeOffset;

        // Following error detection: tolerance 5mm
        if (std::abs(actual_stroke - last_demanded_stroke_mm_) > 5.0f) {
            if (following_excursion_start_ms_ == 0) {
                following_excursion_start_ms_ = last_now_ms_;
            } else if (last_now_ms_ - following_excursion_start_ms_ >= 200) {
                sys::set_latched_fault(sys::kLatchedBrakeFollowing);
                safety_.set_estop(true);
                mode_mgr_.force_estop();
            }
        } else {
            following_excursion_start_ms_ = 0;
        }
    } else if (cf.id == can::kIdSebErrInfo) {
        etrike::protocol::codecs::seb::ErrorInfo info{};
        if (etrike::protocol::codecs::seb::decode_error_info(cf.view(), info)
            != etrike::protocol::CodecStatus::Ok) {
            return;
        }
        static const int kL3Bits[] = {2,3,4,5,6,7,8,9,10,11,13,17,18,20,21,22};
        bool l3_found = false;
        for (int i = 0; i < 16; ++i) {
            if (info.raw[kL3Bits[i] / 8] & (1 << (kL3Bits[i] % 8))) {
                l3_found = true;
                break;
            }
        }
        if (l3_found) {
            sys::set_latched_fault(sys::kLatchedSebL3);
            g_seb_error_status.store(3);
            safety_.set_estop(true);
            mode_mgr_.force_estop();
            broadcast_estop();
        } else if (g_seb_error_status.load() == 3) {
            g_seb_error_status.store(0);
        }
    } else if (cf.id == can::kIdRtStateRpt) {
        can::gen::RtStateRpt sts{};
        if (can::gen::decode_rt_state_rpt(cf.view(), sts) == can::gen::CodecStatus::Ok) {
            if (sts.safety_state == 1 /* InternalEstop */ || sts.safety_state == 2 /* Fault */) {
                if (!sys::rx_estop_suppressed(last_now_ms_)) {
                    safety_.set_estop(true);
                    mode_mgr_.force_estop();
                    broadcast_estop();
                }
            }
        }
    } else if (cf.id == can::kIdHmiModeReq) {
        can::gen::HmiModeReq req{};
        if (can::decode_frame(cf, req) == can::gen::CodecStatus::Ok) {
            mode_mgr_.parse_hmi_mode(req.req_mode ? 1 : 0);
        }
    } else if (cf.id == can::kIdRtHeartbeat) {
        can::gen::RtHeartbeat hb{};
        if (can::decode_frame(cf, hb) == can::gen::CodecStatus::Ok) {
            safety_.feed_heartbeat_rt(hb.alive_ctr);
        }
    } else if (cf.id == can::kIdMtrMotorFbk) {
        last_mtr_fbk_ms_ = last_now_ms_;
        consecutive_mtr_fbk_count_++;
        if (consecutive_mtr_fbk_count_ >= 3) {
            sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
        }
        can::gen::MtrMotorFbk fbk{};
        if (can::gen::decode_mtr_motor_fbk(cf.view(), fbk) == can::gen::CodecStatus::Ok) {
            if ((fbk.fault_flags & shared::kMtrFaultEstopActive) && !sys::rx_estop_suppressed(last_now_ms_)) {
                safety_.set_estop(true);
                mode_mgr_.force_estop();
                broadcast_estop();
            }
        }
    }
}

void SysNode::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    last_now_ms_ = now_ms;
    sys::g_sys_test_time_us = static_cast<int64_t>(now_ms) * 1000;

    // Continuous check of physical hardware button
    if (hw_estop_pressed_) {
        safety_.set_estop(true);
        mode_mgr_.force_estop();
    }

    // Handle operator push buttons
    if (hw_start_pressed_) {
        if (!hw_estop_pressed_ && safety_.heartbeat_ok()) {
            safety_.set_estop(false);
        }
        // Step debounce and attempt exit
        for (int i = 0; i < 6; ++i) mode_mgr_.tick(false, false);
        mode_mgr_.tick(false, true);
        mode_mgr_.tick(false, false);
        if (mode_mgr_.mode() != can::Mode::Estop) {
            sys::mark_estop_reset(now_ms);
        }
        hw_start_pressed_ = false;
    } else if (hw_mode_pressed_) {
        for (int i = 0; i < 6; ++i) mode_mgr_.tick(false, false);
        mode_mgr_.tick(true, false);
        mode_mgr_.tick(false, false);
        hw_mode_pressed_ = false;
    }

    // Check RT heartbeat watchdog
    if (!safety_.heartbeat_ok() && now_ms > 3000) {
        safety_.set_estop(true);
        if (mode_mgr_.mode() != can::Mode::Estop) {
            mode_mgr_.force_estop();
            broadcast_estop();
        }
    }

    // Check MTR feedback timeout (200 ms)
    if (now_ms > 1000 && last_mtr_fbk_ms_ > 0 && (now_ms - last_mtr_fbk_ms_ > 200)) {
        consecutive_mtr_fbk_count_ = 0;
        sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    }

    // 10 ms: 0x011 SYS_SAFETY_STS
    if (now_ms - last_safety_tx_ms_ >= 10) {
        publish_safety_status(now_ms);
        last_safety_tx_ms_ = now_ms;
    }

    // 20 ms: 0x7B9 SYS_SEB_CMD
    if (now_ms - last_seb_tx_ms_ >= 20) {
        publish_seb_cmd(now_ms);
        last_seb_tx_ms_ = now_ms;
    }

    // 100 ms: 0x110 SYS_MODE_CMD & 0x112 SYS_PWR_CMD
    if (now_ms - last_mode_tx_ms_ >= 100) {
        publish_mode_cmd(now_ms);
        publish_pwr_cmd(now_ms);
        last_mode_tx_ms_ = now_ms;
    }

    // 100 ms: 0x115 SYS_HEARTBEAT
    if (now_ms - last_hb_tx_ms_ >= 100) {
        publish_heartbeat(now_ms);
        last_hb_tx_ms_ = now_ms;
    }
}

void SysNode::publish_safety_status(uint32_t now_ms) {
    (void)now_ms;
    can::gen::SysSafetySts msg{};
    msg.estop_active = is_estop_latched();
    msg.heartbeat_ok = safety_.heartbeat_ok();
    msg.rolling_counter = safety_seq_ctr_++;
    msg.e2e_crc = 0;

    can::Frame cf;
    can::encode_frame(msg, cf);
    msg.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(cf.data.data()));
    can::encode_frame(msg, cf);

    low_can_.send(NodeId::SYS, cf);
}

void SysNode::publish_mode_cmd(uint32_t now_ms) {
    (void)now_ms;
    can::gen::SysModeCmd cmd{};
    // Clamp to MANUAL if inhibited
    cmd.mode = (mode() == can::Mode::Auto && !sys::any_inhibit()) ? 1 : 0;
    cmd.rolling_counter = mode_seq_ctr_++;

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::SYS, cf);
    }
}

void SysNode::publish_pwr_cmd(uint32_t now_ms) {
    (void)now_ms;
    can::gen::SysPwrCmd cmd{};
    cmd.power_state = (!is_estop_latched() && !sys::any_inhibit()) ? 1 : 0;
    cmd.rolling_counter = pwr_seq_ctr_++;

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::SYS, cf);
    }
}

void SysNode::publish_heartbeat(uint32_t now_ms) {
    (void)now_ms;
    can::gen::SysHeartbeat hb{};
    hb.alive_ctr = hb_seq_ctr_++;
    hb.estop_active = is_estop_latched();
    hb.mode_auto = (mode() == can::Mode::Auto);

    can::Frame cf;
    if (can::encode_frame(hb, cf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::SYS, cf);
    }
}

void SysNode::publish_seb_cmd(uint32_t now_ms) {
    (void)now_ms;
    bool estop = is_estop_latched();
    // Service brake from RT 0x205 (0..20000 kPa -> 0..27 mm); ESTOP overrides to
    // full stroke. Mirrors sys brake arbitration.
    float service_mm = (static_cast<float>(rt_brake_kpa_) / 20000.0f) * 27.0f;
    service_mm = std::clamp(service_mm, 0.0f, 27.0f);
    float demanded_stroke = estop ? 27.0f : service_mm;
    last_demanded_stroke_mm_ = demanded_stroke;

    etrike::protocol::codecs::seb::Command cmd{};
    cmd.control_enable = true;
    cmd.control_mode = etrike::protocol::codecs::seb::ControlMode::Stroke;
    cmd.stroke_request_raw = static_cast<uint16_t>(std::round((demanded_stroke + 30.0f) / 0.05f));
    cmd.rolling_counter = (seb_seq_ctr_++) & 0x0F;

    etrike::protocol::Frame pf;
    if (etrike::protocol::codecs::seb::encode_command(cmd, pf) == etrike::protocol::CodecStatus::Ok) {
        low_can_.send(NodeId::SYS, pf);
    }
}

} // namespace testbench
