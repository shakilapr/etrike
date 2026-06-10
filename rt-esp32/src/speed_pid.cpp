// Speed PID — implementation.

#include "speed_pid.h"
#include <algorithm>
#include <cmath>

#ifndef __cpp_lib_clamp
namespace std {
template<typename T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
}
#endif
#include "esp_log.h"

namespace rt {
namespace {
constexpr const char* kTag = "pid";
}

void SpeedPid::reset() {
    m_integral   = 0.0f;
    m_prev_error = 0.0f;
    m_output     = 0.0f;
    m_first_call = true;
}

float SpeedPid::update(float setpoint, float measured, float dt) {
    if (dt <= 0.0f) return m_output;  // safety: zero-dt returns previous

    float err = setpoint - measured;
    float p_term = m_gains.kp * err;

    // Integral with anti-windup clamping
    m_integral += err * dt;
    m_integral  = std::clamp(m_integral, -m_gains.max_integral, m_gains.max_integral);
    float i_term = m_gains.ki * m_integral;

    // Derivative — skip on first sample after reset (prevents kick)
    float d_term = 0.0f;
    if (!m_first_call) {
        d_term = m_gains.kd * (err - m_prev_error) / dt;
    }
    m_first_call = false;
    m_prev_error = err;

    m_output = p_term + i_term + d_term;

    ESP_LOGD(kTag, "SP=%.0f M=%.0f E=%.0f out=%.1f (P=%.1f I=%.1f D=%.1f)",
             static_cast<double>(setpoint), static_cast<double>(measured),
             static_cast<double>(err), static_cast<double>(m_output),
             static_cast<double>(p_term), static_cast<double>(i_term), static_cast<double>(d_term));
    return m_output;
}

}  // namespace rt
