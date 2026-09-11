#include "nodes/seb_model.hpp"
#include <cmath>
#include <algorithm>
#include "protocol/codecs/seb.hpp"
#include "shared_config.h"

namespace testbench {

SebModel::SebModel(ICanBus& low_can)
    : low_can_(low_can) {
    init();
}

void SebModel::init() {
    actual_stroke_mm_ = 0.0f;
    target_stroke_mm_ = 0.0f;
    actual_pressure_kpa_ = 0.0f;
    target_pressure_kpa_ = 0.0f;
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
                target_stroke_mm_ = (cmd.stroke_request_raw * shared::kBrakeStrokeScale)
                                    + shared::kBrakeStrokeOffset;
                target_pressure_kpa_ = 0.0f;
            } else {
                // Pressure mode: vendor scale is 0.05 MPa/bit == 50 kPa/bit.
                target_pressure_kpa_ = static_cast<float>(cmd.pressure_request_raw) * 50.0f;
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
        } else if (control_mode_ == static_cast<uint8_t>(
                       etrike::protocol::codecs::seb::ControlMode::Pressure)) {
            // Pressure mode: hydraulic pressure follows the request (first-order lag).
            actual_pressure_kpa_ += (target_pressure_kpa_ - actual_pressure_kpa_) * 0.4f;
        } else {
            // Stroke mode: physical cylinder response lag: ~40% step per 20ms
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

    // Single source of truth: canonical SEB status encoder (protocol/codecs/seb.hpp).
    etrike::protocol::codecs::seb::Status st{};
    st.alignment_status = true;      // aligned (model is always aligned once powered)
    st.control_enabled = true;       // control enable feedback echoes the command
    st.control_mode = control_mode_;
    st.auto_brake_status = false;
    st.error_status = error_status_;
    st.stroke_value_raw = static_cast<uint16_t>(
        std::round((actual_stroke_mm_ - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale));
    int32_t praw = static_cast<int32_t>(std::round(actual_pressure_kpa_ / 50.0f));
    praw = std::clamp(praw, 0, static_cast<int32_t>(shared::kSebMaxPressureRaw));
    st.pressure_value_raw = static_cast<uint8_t>(praw);
    st.angle_value_raw = 0;
    st.rolling_counter_enabled = true;
    st.checksum_enabled = true;
    st.rolling_counter = rolling_counter_;

    etrike::protocol::Frame frame{};
    if (etrike::protocol::codecs::seb::encode_status(st, frame)
        != etrike::protocol::CodecStatus::Ok) {
        return;
    }

    rolling_counter_ = (rolling_counter_ + 1) & 0x0F;

    low_can_.send(NodeId::SEB, frame);
}

} // namespace testbench
