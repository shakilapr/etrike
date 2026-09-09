#include "nodes/ses_model.hpp"
#include "protocol/profiles/xor8_ff_v1.hpp"

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

    etrike::protocol::Frame frame = etrike::protocol::Frame::standard(
        etrike::protocol::codecs::ses::kStatusId,
        etrike::protocol::codecs::ses::kDlc
    );
    frame.data.fill(0);

    // Byte 0: Angle aligned (bit 0), control mode (bits 1-2)
    frame.data[0] = aligned_ ? 0x01 : 0x00;

    // Bytes 2-3: Steering angle LE
    etrike::protocol::write_le_i16(&frame.data[2], actual_angle_0_1deg_);

    // Byte 6: Rolling counter enabled (bit0=1), checksum enabled (bit1=1), rolling counter (bits4-7)
    frame.data[6] = static_cast<uint8_t>(0x03 | ((rolling_counter_ & 0x0F) << 4));

    // Byte 7: XOR8_FF checksum
    frame.data[7] = etrike::protocol::profiles::xor8_ff_v1(frame.data.data(), 7);

    rolling_counter_ = (rolling_counter_ + 1) & 0x0F;

    low_can_.send(NodeId::SES, frame);
}

} // namespace testbench
