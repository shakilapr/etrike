// App orchestration controllers — implementation (see controllers.h).

#include "app/controllers.h"

#include "config/timing_config.h"

namespace rta {

void MotionController::control(TimeUs now_us, const DriveDemand& demand,
                               const MotorFeedback& motor_fb,
                               const SteeringFeedback& steer_fb,
                               const BrakeFeedback& brake_fb,
                               std::uint8_t host_counter,
                               bool host_fresh,
                               bool mtr_fresh,
                               std::uint32_t obstacle_mm,
                               Output& out) {
    // Liveness observation.
    m_host_liveness.observe(now_us, host_counter,
                            static_cast<TimeUs>(kHeartbeatTimeoutMsJetson) * 1000);

    // Resolve kinematics.
    KinematicsResult kres;
    m_kinematics.resolve(demand, kres);

    // Drive command (0x204).
    out.drive.motor_speed_mmps = kres.motor_speed_mmps;
    out.drive.gear = demand.gear ? demand.gear : kres.cmd_gear;

    // Steering target: from kinematics unless the host provided a direct
    // steer override (handled by gateway); here kinematics is authoritative.
    m_steering.set_target(kres.steer_angle_mdeg / 100, kres.motor_speed_mmps);

    // Steering FSM step (50 Hz cadence approximated at control rate).
    SteeringCommand steer_out;
    m_steering.tick(steer_fb, now_us, steer_out);
    out.steer = steer_out;

    // Brake FSM step.
    BrakeCommand brake_out;
    // brake pressure input: use 0 unless an arbitrated brake is set by
    // the caller via demand (kept simple here; arbitration lives in safety).
    m_brake.tick(false, m_mode.mode() == Mode::Estop, 0, m_mode.mode(), brake_fb, brake_out);
    out.brake = brake_out;

    // Safety evaluation.
    SafetyResult safety = m_safety.evaluate(now_us, now_us < startup_grace_end_us,
                                            false /*estop_pending from events*/,
                                            m_mode.mode(), motor_fb, out.drive,
                                            steer_fb, out.steer.angle_0_1deg,
                                            m_steering.state() == SteerState::ACTIVE,
                                            obstacle_mm, host_fresh, mtr_fresh);
    out.safety = safety;

    if (safety.zero_setpoints) {
        out.drive.motor_speed_mmps = 0;
        out.steer.valid = false;
    }
    if (safety.disable_steering) {
        out.steer.valid = false;
    }
    if (safety.estop_reason != kEstopReasonNone) {
        out.estop_required = true;
        out.estop_reason = safety.estop_reason;
    }
    // Mode ESTOP also requires the ESTOP broadcast.
    if (m_mode.mode() == Mode::Estop) {
        out.estop_required = true;
        if (out.estop_reason == kEstopReasonNone) out.estop_reason = kEstopReasonButton;
    }
}

}  // namespace rta
