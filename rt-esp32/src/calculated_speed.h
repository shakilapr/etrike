#pragma once
// Calculated speed estimator — SpeedFeedbackSource::Calculated (value 3).
// Active when ETRIKE_RT_SPEED_FEEDBACK_SOURCE=3.
//
// Estimates the current vehicle speed from the last executed motor setpoint
// plus a first-order plant model lag.  This is a software-only estimate with
// no physical sensor dependency.
//
// Intended use:
//   - SIL (Software-in-the-Loop) bench testing without encoders or MTR.
//   - Early closed-loop PID tuning on the bench before physical sensors are
//     installed and validated.
//
// WARNING: This estimator MUST NOT be used as the sole feedback source on the
//   physical vehicle.  It cannot detect motor stall, slip, or encoder failure.
//   It is accepted for PID Active only in SIL/testing configurations.
//
// Model: exponential first-order lag approximation.
//   estimated_speed(t) += (setpoint - estimated_speed(t-1)) * (1 - exp(-dt/tau))
//   where tau = kPlantTimeconstantS (plant time constant in seconds).
//
// docs/rt-sys-feature-configuration-and-test-plan.md §"Speed feedback source"

#include <cstdint>
#include <cmath>

namespace rt {

class CalculatedSpeedEstimator {
public:
    // Update the estimator with the latest commanded motor setpoint.
    // dt_s: elapsed time since last call, in seconds (normally 0.01 at 100 Hz).
    // Returns the estimated current speed in mm/s.
    int32_t update(int32_t setpoint_mmps, float dt_s) {
        // First-order lag: alpha = 1 - exp(-dt/tau)
        const float alpha = 1.0f - std::exp(-dt_s / kPlantTimeconstantS);
        m_estimated_mmps += (static_cast<float>(setpoint_mmps) - m_estimated_mmps) * alpha;
        return static_cast<int32_t>(m_estimated_mmps);
    }

    // Reset estimator state (call on ESTOP or startup).
    void reset() { m_estimated_mmps = 0.0f; }

    // Read the last estimated speed without updating the model.
    int32_t get() const { return static_cast<int32_t>(m_estimated_mmps); }

private:
    // Plant time constant: approximate motor + drivetrain lag.
    // Tune empirically from a step response on the bench.
    static constexpr float kPlantTimeconstantS = 0.15f;  // 150 ms initial estimate

    float m_estimated_mmps = 0.0f;
};

}  // namespace rt
