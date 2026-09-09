#include "nodes/seb_model.hpp"
#include <cmath>
#include <algorithm>
#include "protocol/profiles/xor8_ff_v1.hpp"

namespace testbench {

SebModel::SebModel(ICanBus& low_can)
    : low_can_(low_can) {
    init();
}

void SebModel::init() {
    actual_stroke_mm_ = 0.0f;
    target_stroke_mm_ = 0.0f;
    actual_pressure_kpa_ = 0.0f;
    control_mode_ = 0;
    error_status_ = 0;
    rolling_counter_ = 0;
    last_tx_ms_ = 0;
    last_cmd_ms_ = 0;
    fault_stuck_ = false;
    stuck_stroke_mm_ = 0.0f;
    fault_l3_ = false;
    fault_comms_loss_ = false;
}

void SebModel::inject_stuck(float stroke_mm) {
    fault_stuck_ = true;
    stuck_stroke_mm_ = stroke_mm;
    actual_stroke_mm_ = stroke_mm;
}

void SebModel::inject_l3_fault() {
    fault_l3_ = true;
    error_status_ = 3;
}

void SebModel::inject_comms_loss(bool loss) {
    fault_comms_loss_ = loss;
}

void SebModel::clear_fault() {
    fault_stuck_ = false;
    fault_l3_ = false;
    fault_comms_loss_ = false;
    error_status_ = 0;
}

void SebModel::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    if (frame.id == etrike::protocol::codecs::seb::kCommandId) {
        etrike::protocol::codecs::seb::Command cmd{};
        if (etrike::protocol::codecs::seb::decode_command(frame.view(), cmd) == etrike::protocol::CodecStatus::Ok) {
            control_mode_ = static_cast<uint8_t>(cmd.control_mode);
            if (cmd.control_mode == etrike::protocol::codecs::seb::ControlMode::Stroke) {
                target_stroke_mm_ = (cmd.stroke_request_raw * 0.05f) - 30.0f;
            } else {
                target_stroke_mm_ = (cmd.pressure_request_raw > 0) ? 20.0f : 0.0f;
                actual_pressure_kpa_ = cmd.pressure_request_raw * 10.0f;
            }
        }
    }
}

void SebModel::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;

    if (fault_l3_) {
        error_status_ = 3;
    }

    // Update physical cylinder position & publish status at 20 ms intervals (50 Hz)
    if (now_ms - last_tx_ms_ >= 20) {
        if (fault_stuck_) {
            actual_stroke_mm_ = stuck_stroke_mm_;
        } else {
            // Physical cylinder response lag: ~50% step per 20ms
            float diff = target_stroke_mm_ - actual_stroke_mm_;
            actual_stroke_mm_ += diff * 0.4f;
        }
        publish_status(now_ms);
        last_tx_ms_ = now_ms;
    }
}

void SebModel::publish_status(uint32_t now_ms) {
    (void)now_ms;
    if (fault_comms_loss_) {
        return;
    }

    etrike::protocol::Frame frame = etrike::protocol::Frame::standard(
        etrike::protocol::codecs::seb::kStatusId,
        etrike::protocol::codecs::seb::kDlc
    );
    frame.data.fill(0);

    // Byte 0: Alignment(0), ControlEnable(1), ControlMode(2..3), AutoBrake(4), ErrorStatus(6..7)
    frame.data[0] = static_cast<uint8_t>(0x03 | ((control_mode_ & 0x03) << 2) | ((error_status_ & 0x03) << 6));

    // Byte 1: Reserved = 0

    // Bytes 2-3: Stroke Raw LE
    uint16_t s_raw = static_cast<uint16_t>(std::round((actual_stroke_mm_ + 30.0f) / 0.05f));
    frame.data[2] = static_cast<uint8_t>(s_raw & 0xFF);
    frame.data[3] = static_cast<uint8_t>((s_raw >> 8) & 0xFF);

    // Bytes 4-5: Pressure / angle raw
    frame.data[4] = 0;
    frame.data[5] = 0;

    // Byte 6: Rolling counter enabled (bit0=1), checksum enabled (bit1=1), rolling counter (bits4-7)
    frame.data[6] = static_cast<uint8_t>(0x03 | ((rolling_counter_ & 0x0F) << 4));

    // Byte 7: XOR8_FF checksum
    frame.data[7] = etrike::protocol::profiles::xor8_ff_v1(frame.data.data(), 7);

    rolling_counter_ = (rolling_counter_ + 1) & 0x0F;

    low_can_.send(NodeId::SEB, frame);
}

} // namespace testbench
