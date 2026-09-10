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
    gear_ = can::Gear::D;
    steer_deg_ = 0.0f;
    steer_set_ = false;
    brake_kpa_ = 0;
    brake_set_ = false;
    drive_counter_ = 0;
    hb_counter_ = 0;
    hmi_counter_ = 0;
    steer_counter_ = 0;
    brake_counter_ = 0;
    last_drive_tx_ms_ = 0;
    last_hb_tx_ms_ = 0;
    last_steer_tx_ms_ = 0;
    last_brake_tx_ms_ = 0;
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

    // 20 ms Steer command (0x303) once the Host has set an angle
    if (steer_set_ && now_ms - last_steer_tx_ms_ >= 20) {
        publish_steer_cmd(now_ms);
        last_steer_tx_ms_ = now_ms;
    }

    // 20 ms Brake command (0x301) once the Host has set a pressure
    if (brake_set_ && now_ms - last_brake_tx_ms_ >= 20) {
        publish_brake_cmd(now_ms);
        last_brake_tx_ms_ = now_ms;
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
    cmd.gear = static_cast<uint8_t>(gear_);

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

void HostModel::publish_steer_cmd(uint32_t now_ms) {
    (void)now_ms;
    can::gen::HostSteerCmd cmd{};
    cmd.steer_angle_0_1deg = static_cast<int16_t>(steer_deg_ * 10.0f);
    cmd.angle_valid = 1;
    cmd.rolling_counter = steer_counter_++;

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

void HostModel::publish_brake_cmd(uint32_t now_ms) {
    (void)now_ms;
    can::gen::HostBrakeReq cmd{};
    cmd.brake_pressure_kpa = brake_kpa_;

    can::Frame cf;
    if (can::encode_frame(cmd, cf) == can::gen::CodecStatus::Ok) {
        high_can_.send(NodeId::HOST, cf);
    }
}

} // namespace testbench
