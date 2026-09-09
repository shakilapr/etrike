#pragma once

#include <cstdint>
#include "ecu_node.hpp"
#include "protocol/codecs/seb.hpp"

namespace testbench {

class SebModel : public IEcuNode {
public:
    explicit SebModel(ICanBus& low_can);
    ~SebModel() override = default;

    NodeId id() const override { return NodeId::SEB; }
    const char* name() const override { return "SEB"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    // Physical status
    float actual_stroke_mm() const { return actual_stroke_mm_; }
    float target_stroke_mm() const { return target_stroke_mm_; }
    float actual_pressure_kpa() const { return actual_pressure_kpa_; }
    uint8_t error_status() const { return error_status_; }

    // Fault injection
    void inject_stuck(float stroke_mm);
    void inject_l3_fault();
    void inject_comms_loss(bool loss);
    void clear_fault();

private:
    ICanBus& low_can_;

    float actual_stroke_mm_{0.0f};
    float target_stroke_mm_{0.0f};
    float actual_pressure_kpa_{0.0f};
    uint8_t control_mode_{0}; // 0 = stroke, 1 = pressure
    uint8_t error_status_{0}; // 0 = normal, 3 = L3 fault
    uint8_t rolling_counter_{0};

    uint32_t last_tx_ms_{0};
    uint32_t last_cmd_ms_{0};

    // Fault flags
    bool fault_stuck_{false};
    float stuck_stroke_mm_{0.0f};
    bool fault_l3_{false};
    bool fault_comms_loss_{false};

    void publish_status(uint32_t now_ms);
};

} // namespace testbench
