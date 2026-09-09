#pragma once

#include <cstdint>
#include "ecu_node.hpp"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"

namespace testbench {

class MtrNode : public IEcuNode {
public:
    explicit MtrNode(ICanBus& low_can);
    ~MtrNode() override = default;

    NodeId id() const override { return NodeId::MTR; }
    const char* name() const override { return "MTR"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    // Status inspection
    bool is_estop_latched() const { return motor_mgr_.is_estop_active(); }
    bool is_propulsion_inhibited() const { return motor_mgr_.propulsion_inhibited(); }
    uint16_t dac_output() const { return dac_.current_code(); }
    can::Gear current_gear() const { return relays_.current_gear(); }
    bool is_traction_enabled() const { return dac_.current_code() > 0 && relays_.current_gear() == can::Gear::D; }

    can::gen::MtrNodeStatus node_status() const {
        can::gen::MtrNodeStatus ns{};
        motor_mgr_.fill_node_status(ns);
        return ns;
    }
    bool is_rearm_pending() const {
        return node_status().recovery_pending;
    }

    // Direct access to inner components if needed
    mtr::MotorManager& manager() { return motor_mgr_; }

private:
    ICanBus& low_can_;
    mtr::RelayController relays_;
    mtr::DacController dac_;
    mtr::MotorManager motor_mgr_;

    uint32_t last_fbk_ms_{0};

    void publish_feedback(uint32_t now_ms);
};

} // namespace testbench
