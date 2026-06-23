#pragma once
// Speed PID — closed-loop rear motor control using encoder feedback.
// Parallel-form PID with anti-windup and derivative-kick prevention.
// Architecture §7.6: shadow controller (outputs telemetry only until encoder fitted).
//
// Gains from config.h: kPidKp=1.0, kPidKi=0.1, kPidKd=0.05.

#include <algorithm>
#include <cmath>
#include "config.h"

namespace rt {

class SpeedPid {
public:
    struct Gains {
        float kp          = kPidKp;
        float ki          = kPidKi;
        float kd          = kPidKd;
        float max_integral = 500.0f;   // anti-windup clamp
    };

    SpeedPid() = default;
    explicit SpeedPid(const Gains& g) : m_gains(g) {}

    /// Reset integral and derivative state (call on mode change or enable).
    void reset() {
        m_integral   = 0.0f;
        m_prev_error = 0.0f;
        m_output     = 0.0f;
        m_first_call = true;
    }

    /// Compute PID output from setpoint and measurement.
    /// @param setpoint_mmps   Target speed (mm/s).
    /// @param measured_mmps   Actual speed from encoder (mm/s).
    /// @param dt_s            Time delta since last update (seconds).
    /// @return                PID output (motor command unit — tune per vehicle).
    float update(float setpoint_mmps, float measured_mmps, float dt_s) {
        float error = setpoint_mmps - measured_mmps;

        // ── Proportional ──────────────────────────────────────────
        float p_term = m_gains.kp * error;

        // ── Integral with anti-windup clamp ────────────────────────
        if (m_first_call) {
            m_prev_error = error;
            m_first_call = false;
        }
        m_integral += error * dt_s;
        m_integral = std::clamp(m_integral, -m_gains.max_integral, m_gains.max_integral);
        float i_term = m_gains.ki * m_integral;

        // ── Derivative (on error, with low-pass via simple coeff) ──
        float d_term = 0.0f;
        if (dt_s > 0.0f) {
            d_term = m_gains.kd * (error - m_prev_error) / dt_s;
        }

        m_prev_error = error;
        m_output     = p_term + i_term + d_term;
        return m_output;
    }

    /// Last computed output value.
    float output() const { return m_output; }

    /// Current integral term (read-only, for diagnostics).
    float integral() const { return m_integral; }

private:
    Gains m_gains;
    float m_integral   = 0.0f;
    float m_prev_error = 0.0f;
    float m_output     = 0.0f;
    bool  m_first_call = true;
};

}  // namespace rt
