#pragma once
// Motor driver — mode-dependent throttle + gear output. Architecture.md §8.6.
#include "throttle_input.h"
#include "mcp4725_dac.h"
#include "can/can_protocol.h"
namespace sys {
class MotorDriver {
public:
    void init() { m_dac.init(); m_throttle.init(); }
    Mcp4725Dac& dac() { return m_dac; }
    ThrottleInput& throttle() { return m_throttle; }

    // Call @ 100 Hz. MANUAL: pass-through. AUTO: setpoint. ESTOP: zero.
    void tick(can::Mode mode, const can::RtDriveCmd* setpoint) {
        if (mode == can::Mode::Estop) { m_dac.write(0); return; }
        if (mode == can::Mode::Manual || !setpoint) {
            m_dac.set_speed_mmps(m_throttle.read_mmps());
        } else {
            m_dac.set_speed_mmps(setpoint->motor_speed_mmps);
        }
    }
private:
    Mcp4725Dac m_dac;
    ThrottleInput m_throttle;
};
}
