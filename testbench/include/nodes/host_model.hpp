#pragma once

#include <cstdint>
#include "ecu_node.hpp"
#include "protocol/compat/can.hpp"

namespace testbench {

class HostModel : public IEcuNode {
public:
    explicit HostModel(ICanBus& high_can);
    ~HostModel() override = default;

    NodeId id() const override { return NodeId::HOST; }
    const char* name() const override { return "HOST"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    // Direct scenario actions
    void send_drive_cmd(int32_t speed_mmps, int32_t yaw_mrad_s = 0);
    void request_mode(can::Mode mode);
    void request_power(bool power_on);
    void set_alive(bool alive) { alive_ = alive; }

    // Full Host actuator intent (0x303 steer / 0x301 brake) + gear selection.
    void set_steer_deg(float deg) { steer_deg_ = deg; steer_set_ = true; }
    void set_brake_kpa(int32_t kpa) { brake_kpa_ = kpa; brake_set_ = true; }
    void set_gear(can::Gear g) { gear_ = g; }
    int32_t cmd_speed_mmps() const { return cmd_speed_mmps_; }

private:
    ICanBus& high_can_;

    bool alive_{true};
    int32_t cmd_speed_mmps_{0};
    int32_t cmd_yaw_mrad_s_{0};
    can::Gear gear_{can::Gear::D};
    float steer_deg_{0.0f};
    bool steer_set_{false};
    int32_t brake_kpa_{0};
    bool brake_set_{false};
    uint8_t drive_counter_{0};
    uint8_t hb_counter_{0};
    uint8_t hmi_counter_{0};
    uint8_t steer_counter_{0};
    uint8_t brake_counter_{0};

    uint32_t last_drive_tx_ms_{0};
    uint32_t last_hb_tx_ms_{0};
    uint32_t last_steer_tx_ms_{0};
    uint32_t last_brake_tx_ms_{0};

    void publish_heartbeat(uint32_t now_ms);
    void publish_drive_cmd(uint32_t now_ms);
    void publish_steer_cmd(uint32_t now_ms);
    void publish_brake_cmd(uint32_t now_ms);
};

} // namespace testbench
