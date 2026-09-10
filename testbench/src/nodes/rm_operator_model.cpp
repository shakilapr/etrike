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
}

void RmOperatorModel::receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) {
    (void)bus_name;
    (void)frame;
}

void RmOperatorModel::step(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    if (!enabled_) return;  // inert unless explicitly enabled by a scenario
    // Emit on a 10 ms cadence (matches rm task_can_tx 100 Hz loop gating). The
    // emitter internally applies its own 10 Hz / 2 Hz sub-cadences from tick_10ms.
    if (now_ms - last_tx_ms_ >= 10) {
        const uint32_t tick_10ms = now_ms / 10;  // monotonic 10 ms counter
        emitter_.emit_cluster(snap_, tick_10ms, [this](const can::Frame& f) {
            bus_.send(NodeId::RM, f);
        });
        last_tx_ms_ = now_ms;
    }
}

} // namespace testbench
