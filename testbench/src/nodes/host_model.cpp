#include "nodes/host_model.hpp"

namespace testbench {

HostModel::HostModel(ICanBus& high_can)
    : high_can_(high_can) {
    init();
}

void HostModel::init() {
    alive_ = true;
    cmd_speed_mmps_ = 0;
    cmd_yaw_mrad_s_ = 0;
    drive_counter_ = 0;
    hb_counter_ = 0;
    hmi_counter_ = 0;
    last_drive_tx_ms_ = 0;
    last_hb_tx_ms_ = 0;
}

void HostModel::send_drive_cmd(int32_t speed_mmps, int32_t yaw_mrad_s) {
    cmd_speed_mmps_ = speed_mmps;
    cmd_yaw_mrad_s_ = yaw_mrad_s;
}

void HostModel::request_mode(can::Mode mode) {
    can::gen::HmiModeReq req{};
    req.req_mode = (mode == can::Mode::Auto);
    req.rolling_counter = hmi_counter_++;

    can::Frame cf;
    if (can::encode_frame(req, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

void HostModel::request_power(bool power_on) {
    can::gen::HmiPwrReq req{};
    req.req_start = power_on;
    req.rolling_counter = hmi_counter_++;

    can::Frame cf;
    if (can::encode_frame(req, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

void HostModel::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    (void)frame;
}

void HostModel::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    if (!alive_) return;

    // 50 ms Heartbeat
    if (now_ms - last_hb_tx_ms_ >= 50) {
        publish_heartbeat(now_ms);
        last_hb_tx_ms_ = now_ms;
    }

    // 20 ms Drive command (only if we have an active drive setpoint or keeping it alive)
    if (now_ms - last_drive_tx_ms_ >= 20) {
        publish_drive_cmd(now_ms);
        last_drive_tx_ms_ = now_ms;
    }
}

void HostModel::publish_heartbeat(uint32_t now_ms) {
    (void)now_ms;
    can::gen::HostHeartbeat hb{};
    hb.alive_ctr = hb_counter_++;
    hb.health_flags = 0;

    can::Frame cf;
    if (can::encode_frame(hb, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

void HostModel::publish_drive_cmd(uint32_t now_ms) {
    (void)now_ms;
    can::gen::HostDriveCmd cmd{};
    cmd.speed_mmps = cmd_speed_mmps_;
    cmd.yaw_rate_mrad_s = cmd_yaw_mrad_s_;
    cmd.gear = static_cast<uint8_t>(can::Gear::D);

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

} // namespace testbench
