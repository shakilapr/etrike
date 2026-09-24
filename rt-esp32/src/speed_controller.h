#pragma once
// Speed controller — bridges a control strategy to the trike speed domain.
//
// Currently: PID shadow controller (telemetry only — gap #5).
//   update_shadow_pid() guards against no-encoder condition (measured=0)
//   and scales controller output to mm/s for 0x220 RT_PID_RPT telemetry.
//
// Future: MPC (Model Predictive Control) will coexist alongside PID.
//   The SpeedController class can hold a variant (PID, MPC, or hybrid)
//   while exposing the same update_shadow_pid() interface.

#include <cstdint>
#include "pid_controller.h"
#include "shared_config.h"

namespace rt {

class SpeedController {
public:
    // Speed PID controller — Disabled: physical motor encoders not fitted yet.
    // Preserved for future closed-loop speed control once encoder hardware is installed.
    /*
    void update_shadow_pid(int32_t desired_mmps, int32_t measured_mmps, float dt,
                           int16_t& pid_output_mmps) {
        float effort = m_pid.update(
            static_cast<float>(desired_mmps),
            static_cast<float>(measured_mmps), dt);

        // Convert effort fraction → mm/s correction (scale to full speed range)
        pid_output_mmps = static_cast<int16_t>(effort * shared::kMaxSpeedFwdMmps);
    }
    */
    void update_shadow_pid(int32_t /*desired_mmps*/, int32_t /*measured_mmps*/, float /*dt*/,
                           int16_t& pid_output_mmps) {
        pid_output_mmps = 0;
    }

    // Future: MPC placeholder
    // void update_mpc(int32_t desired_mmps, int32_t measured_mmps, float dt, ...);

private:
    PidController m_pid;
};

}  // namespace rt
