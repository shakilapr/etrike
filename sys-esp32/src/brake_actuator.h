#pragma once
// ═══ DEPRECATED — Phase S5 ════════════════════════════════════════
// Brake actuation is exclusively through SYNTREE SEB via CAN 0x720.
// This GPIO-based solenoid/relay pattern is legacy.
// See: brake_control.h for the CAN-based SEB implementation.
// ═══════════════════════════════════════════════════════════════════

namespace sys {

class BrakeActuator {
public:
    BrakeActuator() = default;

    void init();
    void engage();
    void release();
    bool is_engaged() const { return m_engaged; }

private:
    bool m_engaged = false;
};

}  // namespace sys
