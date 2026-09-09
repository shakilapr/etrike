#pragma once

#include <cstdint>
#include "ecu_node.hpp"
#include "protocol/codecs/ses.hpp"

namespace testbench {

class SesModel : public IEcuNode {
public:
    explicit SesModel(ICanBus& low_can);
    ~SesModel() override = default;

    NodeId id() const override { return NodeId::SES; }
    const char* name() const override { return "SES"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    void set_actual_angle_0_1deg(int16_t angle) { actual_angle_0_1deg_ = angle; }
    int16_t actual_angle_0_1deg() const { return actual_angle_0_1deg_; }

    void set_aligned(bool aligned) { aligned_ = aligned; }
    bool is_aligned() const { return aligned_; }

    void inject_comms_loss(bool loss) { fault_comms_loss_ = loss; }

private:
    ICanBus& low_can_;

    int16_t actual_angle_0_1deg_{0};
    int16_t target_angle_0_1deg_{0};
    bool aligned_{true};
    uint8_t rolling_counter_{0};
    uint32_t last_tx_ms_{0};
    bool fault_comms_loss_{false};

    void publish_status(uint32_t now_ms);
};

} // namespace testbench
