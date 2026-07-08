#pragma once
// PID speed controller — standalone, reusable across domains.
//
// Derivatives-on-measurement to avoid derivative kick on setpoint steps.
// Optional D-term low-pass filter. Anti-windup with conditional integration
// plus setpoint-change I-reset.
// Gains are placeholder values; tune once rear motor encoder fitted (gap #5).
//
// Future: MPC controller will live in this directory alongside PID.
//   speed_controller.h bridges the controller to the trike speed domain.

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace rt {

struct PidController {
    float kp = 1.0f;       // proportional gain
    float ki = 0.1f;       // integral gain
    float kd = 0.05f;      // derivative gain
    float integral = 0.0f;
    float prev_error = 0.0f;
    float prev_measurement = 0.0f;  // for derivative-on-measurement
    float prev_setpoint = 0.0f;     // for setpoint-change anti-windup
    float output_min = -1.0f;       // fraction of full scale
    float output_max =  1.0f;

    // Optional: derivative low-pass filter (0.0 = no filter, ~0.7 = heavy)
    float d_filter_alpha = 0.0f;
    float d_filtered = 0.0f;

    // I-term reset threshold: large setpoint changes reset integral
    float setpoint_change_threshold = 500.0f;  // mm/s

    // PID update with anti-windup. setpoint/measured in mm/s, dt in seconds.
    // Returns effort correction (fraction of full-scale, -1..+1).
    // First call: seeds prev_measurement/prev_setpoint to avoid D-term spike.
    float update(float setpoint, float measured, float dt) {
        if (dt <= 0.0f) return 0.0f;

        // First-call seeding: set prev_measurement = measured so D-term is zero,
        // set prev_setpoint = setpoint so anti-windup reset isn't spuriously triggered.
        if (!m_first_call_done) {
            prev_measurement = measured;
            prev_setpoint = setpoint;
            m_first_call_done = true;
        }

        // Anti-windup: reset integral on large setpoint changes
        if (std::abs(setpoint - prev_setpoint) > setpoint_change_threshold) {
            integral = 0.0f;
            d_filtered = 0.0f;
        }

        float error = setpoint - measured;
        float p_term = kp * error;

        // Derivative-on-measurement (avoids derivative kick on setpoint steps):
        //   d_input = -(measured - prev_measurement) / dt
        // Negative sign: when measured increases (closing gap to setpoint),
        // the derivative opposes the motion (braking effect).
        // Optional low-pass filter.
        float d_term = 0.0f;
        float d_input = -(measured - prev_measurement) / dt;
        if (d_filter_alpha > 0.0f) {
            d_filtered = d_filter_alpha * d_filtered + (1.0f - d_filter_alpha) * d_input;
            d_term = kd * d_filtered;
        } else {
            d_term = kd * d_input;
        }

        // Integral anti-windup: trial-integrate, but do not accept growth
        // that pushes farther into output saturation.
        float max_integral = (output_max - output_min) / (std::max)(ki * 2.0f, 0.001f);
        float candidate_integral = integral + ki * error * dt;
        candidate_integral = std::clamp(candidate_integral, -max_integral, max_integral);

        float unclamped = p_term + candidate_integral + d_term;
        float output = std::clamp(unclamped, output_min, output_max);
        bool saturated_high = output >= output_max && error > 0.0f;
        bool saturated_low  = output <= output_min && error < 0.0f;
        if (!saturated_high && !saturated_low) {
            integral = candidate_integral;
        }

        prev_error = error;
        prev_measurement = measured;
        prev_setpoint = setpoint;

        return output;
    }

    void reset() {
        integral = 0.0f;
        prev_error = 0.0f;
        prev_measurement = 0.0f;
        prev_setpoint = 0.0f;
        d_filtered = 0.0f;
        m_first_call_done = false;
    }

private:
    bool m_first_call_done = false;
};

}  // namespace rt
