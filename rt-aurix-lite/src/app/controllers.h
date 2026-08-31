#pragma once
// App orchestration controllers — compose the domain modules.
// This is the layer the deterministic simulation and the future runtime
// both drive. Pure logic: no CAN IDs, no IPC, no logging, no clock reads.

#include <cstdint>
#include "core/time.h"
#include "core/types.h"
#include "config/timing_config.h"
#include "shared_config.h"  // kStartupGracePeriodMs
#include "domain/kinematics.h"
#include "domain/steering.h"
#include "domain/brake.h"
#include "domain/mode.h"
#include "domain/safety.h"
#include "domain/liveness.h"

namespace rta {

// ── MotionController ────────────────────────────────────────────────
// Composes kinematics + steering + brake + safety from typed inputs into
// typed actuator commands. Called by the motion executor (CPU1, 100 Hz
// control + 50 Hz brake/steer cadence in the runtime).
class MotionController {
public:
    MotionController() = default;

    // Outputs produced each control cycle.
    struct Output {
        DriveCommand    drive;     // 0x204
        SteeringCommand steer;     // 0x169
        BrakeCommand    brake;     // 0x7B9
        bool            estop_required = false;  // caller broadcasts 0x001
        std::uint8_t    estop_reason = 0;
        SafetyResult    safety;
    };

    void init() {
        m_steering.init();
        m_brake.init();
        m_mode.init();
        m_safety = SafetySupervisor{};
        m_host_liveness = LivenessMonitor{};
        m_mtr_fresh = FreshnessMonitor{};
    }

    // Control step (100 Hz). Fills `out` for TX.
    //   now_us     : monotonic time.
    //   demand     : resolved drive demand (from gateway/host arbitration).
    //   motor_fb   : MTR feedback.
    //   steer_fb   : EPS-C steering feedback.
    //   brake_fb   : SEB brake feedback.
    //   host_counter / host_fresh : host heartbeat.
    //   mtr_fresh  : MTR feedback freshness (implicit 0x206 liveness).
    //   obstacle_mm: obstacle distance.
    void control(TimeUs now_us, const DriveDemand& demand,
                 const MotorFeedback& motor_fb,
                 const SteeringFeedback& steer_fb,
                 const BrakeFeedback& brake_fb,
                 std::uint8_t host_counter,
                 bool host_fresh,
                 bool mtr_fresh,
                 std::uint32_t obstacle_mm,
                 Output& out);

    // Mode interface.
    Mode mode() const { return m_mode.mode(); }
    bool mode_tick(bool mode_btn, bool start_btn) { return m_mode.tick(mode_btn, start_btn); }
    void force_estop() { m_mode.force_estop(); }
    bool apply_hmi(const ModeRequest& req) { return m_mode.apply_hmi_request(req); }

    // Steering/brake sub-controllers exposed for TX scheduling.
    SteeringControl& steering() { return m_steering; }
    BrakeControl& brake() { return m_brake; }

private:
    Kinematics      m_kinematics;
    SteeringControl m_steering;
    BrakeControl    m_brake;
    ModeManager     m_mode;
    SafetySupervisor m_safety;
    LivenessMonitor m_host_liveness;
    FreshnessMonitor m_mtr_fresh;
    TimeUs startup_grace_end_us = static_cast<TimeUs>(kStartupGracePeriodMs) * 1000;
};

// ── BodyController ──────────────────────────────────────────────────
// Composes lights/indicators from typed inputs (mode + light requests).
class BodyController {
public:
    BodyController() = default;

    struct Output {
        LightState lights;
    };

    void update(Mode mode, const LightState& requested, Output& out) {
        // In ESTOP: brake light on, others follow requested.
        out.lights = requested;
        if (mode == Mode::Estop) {
            out.lights.brake = true;
        }
    }
};

// ── GatewayController ───────────────────────────────────────────────
// Applies route-table decisions and generates derived frames. This is
// the orchestration that translates host commands into actuator commands
// (Category 2). For the host, it produces typed outputs from typed inputs;
// the wire encoding is applied by the adapters at TX.
class GatewayController {
public:
    GatewayController() = default;

    struct Input {
        DriveDemand    drive;       // from decode_host_drive (0x300)
        std::int32_t   brake_kpa   = 0;  // from 0x301
        std::int16_t   steer_0_1deg = 0; // from 0x303 (optional direct steer)
        bool           steer_valid = false;
    };

    struct Output {
        DriveDemand    drive_demand;   // to motion control
        std::int32_t   brake_kpa = 0;  // to motion control (arbitrated)
        bool           steer_from_host = false;
        std::int16_t   steer_0_1deg = 0;
    };

    // Arbitrate a host drive command into a motion demand. The kinematics
    // resolver is applied inside MotionController; here we just pass the
    // demand + brake through (arbitration with obstacle is in safety).
    Output process(const Input& in) const {
        Output out;
        out.drive_demand = in.drive;
        out.brake_kpa = in.brake_kpa;
        out.steer_from_host = in.steer_valid;
        out.steer_0_1deg = in.steer_0_1deg;
        return out;
    }
};

}  // namespace rta
