#pragma once
// RmOperatorModel - wraps the real rm-esp32-t12d CAN emitter (rm::CanEmitter) so
// the rm-esp32-t12d gateway can be exercised as an operator/source node inside
// the testbench (replacing or alongside HostModel). It builds an rm::RcSnapshot
// from scripted operator inputs and emits the full per-mode CAN cluster onto the
// configured bus using the firmware's actual encode path.

#include <cstdint>
#include <string>
#include "ecu_node.hpp"
#include "protocol/compat/can.hpp"
#include "rm-esp32-t12d/src/config.h"
#include "rm-esp32-t12d/src/rc_decoder.h"
#include "rm-esp32-t12d/src/can_emitter.h"

namespace testbench {

class RmOperatorModel : public IEcuNode {
public:
    explicit RmOperatorModel(ICanBus& bus);
    ~RmOperatorModel() override = default;

    NodeId id() const override { return NodeId::RM; }
    const char* name() const override { return "RM"; }

    void init() override;
    void step(uint32_t now_ms, uint32_t dt_ms) override;
    void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) override;

    // Scripted operator inputs (mirror rm::RcSnapshot fields)
    void set_op_mode(rm::OperatingMode m) { snap_.op_mode = m; }
    void set_drive_enable(bool on)       { snap_.drive_enable_req = on; }
    void set_park_hold(bool on)          { snap_.park_hold_req = on; }
    void set_gear(can::Gear g)          { snap_.gear = g; }
    void set_target_speed_mmps(int32_t s){ snap_.target_speed_mmps = s; }
    void set_steering_deg(float d)      { snap_.steering_deg = d; }
    void set_brake_stroke_mm(float b)   { snap_.brake_stroke_mm = b; }
    void set_signal_valid(bool v)        { snap_.signal_valid = v; }
    void set_aux_vra(float v)            { snap_.aux_vra = v; }

    // Convenience: arm drive in gear D at a target speed (AUTO-like emulation).
    void drive(int32_t speed_mmps, float steer_deg = 0.0f, can::Gear g = can::Gear::D) {
        enabled_                = true;  // arming implies emitting
        snap_.signal_valid       = true;
        snap_.drive_enable_req  = true;
        snap_.park_hold_req     = false;
        snap_.gear              = g;
        snap_.target_speed_mmps = speed_mmps;
        snap_.steering_deg      = steer_deg;
        if (g == can::Gear::D) {
            snap_.aux_vra = static_cast<float>(speed_mmps) / static_cast<float>(rm::kSpeedFwdMaxMmps);
        } else if (g == can::Gear::R) {
            snap_.aux_vra = static_cast<float>(speed_mmps) / static_cast<float>(rm::kSpeedRevMaxMmps);
        } else {
            snap_.aux_vra = 0.0f;
        }
    }

    // rm is inert unless enabled (default off) so it does not disturb other
    // scenarios in the shared TestBench. Tests enable it explicitly.
    void set_enabled(bool e) { enabled_ = e; }

private:
    ICanBus& bus_;
    rm::RcSnapshot snap_{};
    rm::CanEmitter emitter_{};
    uint32_t last_tx_ms_{0};
    bool enabled_{false};
};

} // namespace testbench
