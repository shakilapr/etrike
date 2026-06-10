#pragma once
// Drive-by-wire steering servo — PWM with slew-rate limiting.
// Active only in AUTO mode.  ±45° range, 50 Hz PWM (standard servo).

#include <cstdint>

namespace rt {

class SteeringServo {
public:
    SteeringServo() = default;

    void init();                       // Configure LEDC PWM, start disabled
    void set_target(int32_t mdeg);     // +right, -left
    void enable();
    void disable();
    void tick();                       // Call at CONTROL_LOOP_HZ — applies slew limiting

    int32_t current() const { return m_current_mdeg; }
    int32_t target()  const { return m_target_mdeg; }
    bool    at_target() const { return std::abs(m_current_mdeg - m_target_mdeg) < 500; }
    bool    is_enabled() const { return m_enabled; }

private:
    static uint32_t angle_to_duty(int32_t mdeg);

    int32_t m_target_mdeg  = 0;
    int32_t m_current_mdeg = 0;
    bool    m_enabled      = false;
    int64_t m_last_tick_us = 0;
};

}  // namespace rt
