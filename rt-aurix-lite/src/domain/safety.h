#pragma once
// Safety supervision — EGAS L2, follow-error, obstacle, ESTOP latch.
// Pure domain logic (no CAN IDs, no IPC, no logging, no clock reads).
// Port of the RT ESP32 safety checks (rt-esp32/src/safety_monitor.h) with
// the SYS-heartbeat / dual-owner (0x7FE, seb_takeover) logic removed.

#include <cstdint>
#include "core/time.h"
#include "core/types.h"
#include "core/result.h"
#include "config/safety_config.h"
#include "shared_config.h"  // kMaxBrakeKpa, kAssistStopKpa, kObstacle*, kLowSpeedThreshMmps

namespace rta {

enum class SafetyState : std::uint8_t {
    Normal = 0,
    InternalEstop = 1,
    Fault = 2,
};

// Result of a safety evaluation. The caller (app orchestrator) applies
// these to the actuator setpoints and broadcasts ESTOP as required.
struct SafetyResult {
    bool    zero_setpoints    = false;
    std::int32_t brake_kpa    = 0;
    bool    disable_steering  = false;
    bool    obstacle_triggered = false;
    std::uint8_t estop_reason = kEstopReasonNone;
    std::uint32_t faults      = 0;  // rta::Fault bitmask
};

class SafetySupervisor {
public:
    // Evaluate safety once per control cycle (100 Hz).
    //   now_us        : monotonic time.
    //   startup_grace : suppress checks during boot (kStartupGracePeriodMs).
    //   estop_pending : latched ESTOP (hardware button / CAN 0x001 / fault).
    //   mode          : current mode (Estop forces zero setpoints).
    //   motor_fb      : MTR feedback (0x206) for EGAS L2.
    //   drive_cmd     : commanded drive (0x204) for EGAS L2 setpoint.
    //   steer_fb      : steering feedback (0x201) for follow-error.
    //   steer_cmd     : commanded steering angle (0.1° units).
    //   steer_active  : steering FSM is ACTIVE (follow-error gated).
    //   obstacle_mm   : obstacle distance (0x400).
    //   host_fresh    : host heartbeat/command is fresh (0x7FC).
    //   mtr_fresh     : MTR feedback is fresh (implicit 0x206 liveness).
    SafetyResult evaluate(TimeUs now_us, bool startup_grace,
                          bool estop_pending, Mode mode,
                          const MotorFeedback& motor_fb,
                          const DriveCommand& drive_cmd,
                          const SteeringFeedback& steer_fb,
                          std::int16_t steer_cmd_0_1deg,
                          bool steer_active,
                          std::uint32_t obstacle_mm,
                          bool host_fresh, bool mtr_fresh);

    // Reset follow-error accumulation (e.g., on recovery).
    void reset_follow_error() noexcept {
        m_follow_err_active = false;
        m_follow_err_start_us = 0;
    }

    // Re-check whether the peer liveness should force an assisted stop.
    bool host_lost() const noexcept { return m_host_lost; }
    bool mtr_lost() const noexcept { return m_mtr_lost; }

private:
    bool   m_follow_err_active = false;
    TimeUs m_follow_err_start_us = 0;
    bool   m_host_lost = false;
    bool   m_mtr_lost = false;
};

}  // namespace rta
