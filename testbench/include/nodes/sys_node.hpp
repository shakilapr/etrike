#pragma once

#include <cstdint>
#include "ecu_node.hpp"
#include "protocol/compat/can.hpp"
#include "sys-esp32/src/config.h"
#include "sys-esp32/src/mode_manager.h"
#include "sys-esp32/src/safety_monitor.h"
#include "sys-esp32/src/inhibit_state.h"
#include "sys-esp32/src/brake_control.h"

namespace testbench {

class SysNode : public IEcuNode {
public:
    explicit SysNode(ICanBus& low_can);
    ~SysNode() override = default;

    NodeId id() const override { return NodeId::SYS; }
    const char* name() const override { return "SYS"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    // Status inspection
    can::Mode mode() const { return mode_mgr_.mode(); }
    bool is_estop_latched() const { return sys::ModeManager::estop_latched(mode_mgr_.mode(), safety_.estop_active()); }
    uint32_t inhibit_reasons() const { return sys::g_inhibit_reasons.load(); }
    uint32_t latched_faults() const { return sys::g_latched_fault_reasons.load(); }
    bool is_mtr_feedback_lost() const { return (sys::g_inhibit_reasons.load() & sys::kInhibitMtrFbkLoss) != 0; }

    // Operator physical inputs
    void press_start_button();
    void press_mode_button();
    void hold_mode_button_3s();
    void press_estop_button();
    void release_estop_button();
    void set_brake_lever(bool pressed);
    void broadcast_estop();

    // Component access
    sys::ModeManager& mode_manager() { return mode_mgr_; }
    sys::SafetyMonitor& safety_monitor() { return safety_; }

private:
    ICanBus& low_can_;

    sys::ModeManager mode_mgr_;
    sys::SafetyMonitor safety_;
    sys::BrakeControl brake_ctrl_;

    bool hw_estop_pressed_{false};
    bool hw_start_pressed_{false};
    bool hw_mode_pressed_{false};

    uint8_t safety_seq_ctr_{0};
    uint8_t mode_seq_ctr_{0};
    uint8_t pwr_seq_ctr_{0};
    uint8_t hb_seq_ctr_{0};
    uint8_t seb_seq_ctr_{0};

    uint32_t last_safety_tx_ms_{0};
    uint32_t last_mode_tx_ms_{0};
    uint32_t last_pwr_tx_ms_{0};
    uint32_t last_hb_tx_ms_{0};
    uint32_t last_seb_tx_ms_{0};
    uint32_t last_now_ms_{0};

    // MTR feedback supervision
    uint32_t last_mtr_fbk_ms_{0};
    uint32_t consecutive_mtr_fbk_count_{0};

    // Brake following tracking
    float last_demanded_stroke_mm_{0.0f};
    uint32_t following_excursion_start_ms_{0};

    void publish_safety_status(uint32_t now_ms);
    void publish_mode_cmd(uint32_t now_ms);
    void publish_pwr_cmd(uint32_t now_ms);
    void publish_heartbeat(uint32_t now_ms);
    void publish_seb_cmd(uint32_t now_ms);
};

} // namespace testbench
