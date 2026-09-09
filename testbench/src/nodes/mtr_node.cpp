#include "nodes/mtr_node.hpp"

namespace testbench {

MtrNode::MtrNode(ICanBus& low_can)
    : low_can_(low_can),
      motor_mgr_(relays_, dac_) {
    init();
}

void MtrNode::init() {
    relays_.init();
    dac_.init();
    motor_mgr_.init();
    last_fbk_ms_ = 0;
}

void MtrNode::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    can::Frame cf = frame;
    // Handle specific frames with motor_mgr_
    if (cf.id == can::kIdSysSafetySts) {
        // We pass the current time from the frame or last tick
        // MotorManager::handle_safety_status expects now_ms
        motor_mgr_.handle_safety_status(cf, last_fbk_ms_);
    } else {
        motor_mgr_.handle_frame(cf, last_fbk_ms_);
    }
}

void MtrNode::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    motor_mgr_.tick(now_ms);

    // 50 Hz feedback on low CAN (every 20 ms)
    if (now_ms - last_fbk_ms_ >= 20) {
        publish_feedback(now_ms);
        last_fbk_ms_ = now_ms;
    }
}

void MtrNode::publish_feedback(uint32_t now_ms) {
    (void)now_ms;
    can::Frame cf = motor_mgr_.build_motor_feedback_frame();
    low_can_.send(NodeId::MTR, cf);

    can::gen::MtrNodeStatus ns{};
    motor_mgr_.fill_node_status(ns);
    can::Frame nsf;
    if (can::encode_frame(ns, nsf) == can::gen::CodecStatus::Ok) {
        low_can_.send(NodeId::MTR, nsf);
    }
}

} // namespace testbench
