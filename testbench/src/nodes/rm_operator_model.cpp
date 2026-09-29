#include "nodes/rm_operator_model.hpp"

namespace testbench {

RmOperatorModel::RmOperatorModel(ICanBus& bus)
    : bus_(bus) {
    init();
}

void RmOperatorModel::init() {
    // Sensible default: BARE, armed, driving forward slowly. Scenarios override.
    snap_.signal_valid       = true;
    snap_.drive_enable_req  = true;
    snap_.park_hold_req     = false;
    snap_.gear              = can::Gear::D;
    snap_.target_speed_mmps = 0;
    snap_.steering_deg      = 0.0f;
    snap_.brake_stroke_mm   = 0.0f;
    snap_.aux_vra           = 0.0f;
    snap_.op_mode           = rm::OperatingMode::Bare;
    last_tx_ms_             = 0;
    last_now_ms_            = 0;
    emitter_.reset_counters();
}

void RmOperatorModel::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    if (frame.id == etrike::protocol::codecs::ses::kStatusId) {
        etrike::protocol::codecs::ses::Status st{};
        if (etrike::protocol::codecs::ses::decode_status(frame.view(), st) == etrike::protocol::CodecStatus::Ok) {
            float angle_deg = static_cast<float>(static_cast<int16_t>(st.steering_angle_raw) - 30000) * 0.1f;
            emitter_.on_ses_status_rx(st.control_mode, angle_deg, last_now_ms_);
        }
    }
}

void RmOperatorModel::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    last_now_ms_ = now_ms;
    if (!enabled_) return;  // inert unless explicitly enabled by a scenario
    // Emit on a 10 ms cadence (matches rm task_can_tx 100 Hz loop gating). The
    // emitter internally applies its own 10 Hz / 2 Hz sub-cadences from tick_10ms.
    if (now_ms - last_tx_ms_ >= 10) {
        const uint32_t tick_10ms = now_ms / 10;  // monotonic 10 ms counter
        emitter_.emit_cluster(snap_, tick_10ms, now_ms, [this](const can::Frame& f) {
            bus_.send(NodeId::RM, f);
        });
        last_tx_ms_ = now_ms;
    }
}

} // namespace testbench
