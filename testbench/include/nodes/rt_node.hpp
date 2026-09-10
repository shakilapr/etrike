#pragma once

#include <cstdint>
#include "ecu_node.hpp"
#include "protocol/compat/can.hpp"

namespace testbench {

class RtNode : public IEcuNode {
public:
    RtNode(ICanBus& high_can, ICanBus& low_can);
    ~RtNode() override = default;

    NodeId id() const override { return NodeId::RT; }
    const char* name() const override { return "RT"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    // Status inspection
    bool is_estop_latched() const { return estop_latched_; }
    can::Mode active_mode() const { return active_mode_; }
    int32_t commanded_speed_mmps() const { return commanded_speed_mmps_; }
    bool is_mtr_feedback_lost() const { return mtr_feedback_lost_; }
    int16_t host_steer_0_1deg() const { return host_steer_0_1deg_; }
    int32_t host_brake_kpa() const { return host_brake_kpa_; }
    bool has_host_steer() const { return host_steer_seen_; }
    bool has_host_brake() const { return host_brake_seen_; }
    bool is_host_heartbeat_lost() const { return host_hb_lost_; }

    void trigger_software_estop();
    void clear_software_estop();

private:
    ICanBus& high_can_;
    ICanBus& low_can_;

    bool estop_latched_{false};
    bool software_estop_active_{false};
    can::Mode active_mode_{can::Mode::Manual};
    int32_t host_speed_mmps_{0};
    int32_t host_yaw_rate_{0};
    can::Gear host_gear_{can::Gear::D};
    int32_t commanded_speed_mmps_{0};

    // Host steer/brake intent forwarded to actuators (0x169 SES / 0x205 SYS)
    int16_t host_steer_0_1deg_{0};
    int32_t host_brake_kpa_{0};
    bool    host_steer_seen_{false};
    bool    host_brake_seen_{false};
    uint8_t ses_counter_{0};
    uint32_t last_steer_tx_ms_{0};
    uint32_t last_brake_tx_ms_{0};

    // Host heartbeat (0x7FC) supervision: 1500 ms -> assisted stop (2000 kPa).
    bool     host_hb_seen_{false};
    bool     host_hb_lost_{false};
    uint32_t last_host_hb_ms_{0};

    uint8_t rt_hb_ctr_{0};
    uint8_t rt_clear_confirm_count_{0};

    uint32_t last_drive_tx_ms_{0};
    uint32_t last_hb_tx_ms_{0};
    uint32_t last_state_tx_ms_{0};
    uint32_t last_now_ms_{0};

    // MTR feedback supervision
    uint32_t last_mtr_fbk_ms_{0};
    uint32_t consecutive_mtr_fbk_count_{0};
    bool mtr_feedback_lost_{false};

    void publish_drive_cmd(uint32_t now_ms);
    void publish_steer_cmd(uint32_t now_ms);
    void publish_brake_cmd(uint32_t now_ms);
    void publish_heartbeat(uint32_t now_ms);
    void publish_state_report(uint32_t now_ms);
};

} // namespace testbench
