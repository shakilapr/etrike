#pragma once
// Speed PID — closed-loop rear motor control using encoder feedback.
// Parallel-form PID with anti-windup and derivative-kick prevention.

namespace rt {

class SpeedPid {
public:
    struct Gains {
        float kp = 1.0f;
        float ki = 0.1f;
        float kd = 0.05f;
        float max_integral = 500.0f;
    };

    SpeedPid() = default;
    explicit SpeedPid(const Gains& g) : m_gains(g) {}

    void reset();
    float update(float setpoint_mmps, float measured_mmps, float dt_s);
    float output() const { return m_output; }

private:
    Gains m_gains;
    float m_integral   = 0.0f;
    float m_prev_error = 0.0f;
    float m_output     = 0.0f;
    bool  m_first_call = true;
};

}  // namespace rt
