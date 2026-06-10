#pragma once
// Rear motor PWM + direction.
// Mode-aware: AUTO -> inter-MCU setpoint, MANUAL -> throttle, ESTOP -> stop.

#include <cstdint>

namespace sys {

class MotorDriver {
public:
    MotorDriver() = default;

    void init();
    void set_speed(int32_t speed_mmps);  // Positive = forward
    void set_effort(int32_t effort_pwm); // Signed raw PWM effort [-8191, +8191]
    void stop();

private:
    static constexpr int kResBits = 13;
    static constexpr int kPwmMax  = (1 << kResBits) - 1;  // 8191
};

}  // namespace sys
