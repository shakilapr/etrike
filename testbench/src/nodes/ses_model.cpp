#include "nodes/ses_model.hpp"
#include "protocol/codecs/ses.hpp"

namespace testbench {

SesModel::SesModel(ICanBus& low_can)
    : low_can_(low_can) {
    init();
}

void SesModel::init() {
    actual_angle_0_1deg_ = 0;
    target_angle_0_1deg_ = 0;
    aligned_ = true;
    rolling_counter_ = 0;
    last_tx_ms_ = 0;
    fault_comms_loss_ = false;
}

void SesModel::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    if (frame.id == etrike::protocol::codecs::ses::kCommandId) {
        etrike::protocol::codecs::ses::Command cmd{};
        if (etrike::protocol::codecs::ses::decode_command(frame.view(), cmd) == etrike::protocol::CodecStatus::Ok) {
            target_angle_0_1deg_ = cmd.target_angle_raw;
        }
    }
}

void SesModel::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;

    // Follow target angle with lag
    int16_t diff = target_angle_0_1deg_ - actual_angle_0_1deg_;
    actual_angle_0_1deg_ += static_cast<int16_t>(diff * 0.3f);

    // Publish status every 20 ms
    if (now_ms - last_tx_ms_ >= 20) {
        publish_status(now_ms);
        last_tx_ms_ = now_ms;
    }
}

void SesModel::publish_status(uint32_t now_ms) {
    (void)now_ms;
    if (fault_comms_loss_) {
        return;
    }

    // Single source of truth: canonical SES status encoder (protocol/codecs/ses.hpp).
    etrike::protocol::codecs::ses::Status st{};
    st.angle_aligned = aligned_;
    st.control_mode = 1;  // Automatic
    st.error_status = 0;
    st.steering_angle_raw = static_cast<uint16_t>(actual_angle_0_1deg_);
    st.target_angle_speed_raw = 0;
    st.steering_torque_raw = 0;
    st.rolling_counter_enabled = true;
    st.checksum_enabled = true;
    st.rolling_counter = rolling_counter_;

    etrike::protocol::Frame frame{};
    if (etrike::protocol::codecs::ses::encode_status(st, frame)
        != etrike::protocol::CodecStatus::Ok) {
        return;
    }

    rolling_counter_ = (rolling_counter_ + 1) & 0x0F;

    low_can_.send(NodeId::SES, frame);
}

} // namespace testbench
