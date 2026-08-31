#pragma once
// Steering controller — steer-by-wire unit command generation.
// Pure domain logic: no CAN IDs, no IPC, no logging, no clock reads.
// Time is passed in (TimeUs). Behavior-preserving port of the RT ESP32
// SteeringControl (rt-esp32/src/steering_control.h), adapted to rta types
// and config. The wire encoding (ses::Command) stays in the protocol layer.

#include <cstdint>
#include "core/time.h"
#include "core/types.h"
#include "config/control_config.h"

namespace rta {

enum class SteerState : std::uint8_t {
    BOOT_WAIT,          // power-on delay — do NOT transmit
    LISTEN_SYNC,        // waiting for aligned steering feedback
    ACTIVE,             // normal operation — command at 50 Hz
    ESTOP_RAMP_TO_ZERO, // non-obstacle ESTOP: ramp to 0° then hold
    ESTOP_HOLD_THEN_SILENT, // obstacle ESTOP: hold, then silent-stop
    FAULT,              // timeout or silent-stop — stop transmitting
};

constexpr const char* steer_state_name(SteerState s) noexcept {
    switch (s) {
        case SteerState::BOOT_WAIT:            return "BOOT_WAIT";
        case SteerState::LISTEN_SYNC:          return "LISTEN_SYNC";
        case SteerState::ACTIVE:               return "ACTIVE";
        case SteerState::ESTOP_RAMP_TO_ZERO:   return "ESTOP_RAMP_TO_ZERO";
        case SteerState::ESTOP_HOLD_THEN_SILENT: return "ESTOP_HOLD_THEN_SILENT";
        case SteerState::FAULT:                return "FAULT";
    }
    return "?";
}

class SteeringControl {
public:
    void init() noexcept {
        m_state = SteerState::BOOT_WAIT;
        m_timer = 0;
        m_active_angle_0_1deg = 0;
        m_speed_mmps = 0;
        m_roll = 0;
        m_sync_start_us = 0;
        m_estop_hold_start_us = 0;
        m_estop_hold_angle = 0;
        m_estop_follow_err_start_us = 0;
        m_estop_exit_pending = false;
    }

    SteerState state() const noexcept { return m_state; }

    // Call at 50 Hz. feedback: steering feedback (valid/angle/aligned).
    // now_us: monotonic time. Fills `out` (typed, offset-free 0.1° units).
    // Returns true if the caller should transmit a steering command.
    bool tick(const SteeringFeedback& fb, TimeUs now_us, SteeringCommand& out);

    // Set the desired steering angle (called at control rate).
    // angle_0_1deg: offset-free 0.1° units (+right). speed_mmps: current speed.
    void set_target(std::int32_t angle_0_1deg, std::int32_t speed_mmps) noexcept {
        if (m_state == SteerState::ACTIVE) {
            m_active_angle_0_1deg = static_cast<std::int16_t>(angle_0_1deg);
            m_speed_mmps = speed_mmps;
            m_estop_exit_pending = false;  // fresh command clears pending exit
        }
        // In ESTOP states, ignore target updates — ramp/hold uses its own angle.
    }

    // Trigger ESTOP. obstacle_triggered=true -> hold-then-silent;
    // false -> ramp-to-zero. First trigger wins.
    void start_estop(bool obstacle_triggered) noexcept;

    // Set the hold timestamp for obstacle ESTOP (called after start_estop).
    void set_estop_hold_time(TimeUs now_us) noexcept {
        if (m_state == SteerState::ESTOP_HOLD_THEN_SILENT && m_estop_hold_start_us == 0) {
            m_estop_hold_start_us = now_us;
        }
    }

    // Reset from FAULT to LISTEN_SYNC (START button retry).
    void reset_to_listen(TimeUs now_us) noexcept {
        if (m_state == SteerState::FAULT) {
            m_state = SteerState::LISTEN_SYNC;
            m_sync_start_us = now_us;
        }
    }

    // Exit ESTOP — deferred until ramp/hold completes.
    void exit_estop() noexcept {
        if (m_state == SteerState::ESTOP_RAMP_TO_ZERO ||
            m_state == SteerState::ESTOP_HOLD_THEN_SILENT) {
            m_estop_exit_pending = true;
        }
    }

private:
    void build_command(SteeringCommand& out);

    SteerState m_state = SteerState::BOOT_WAIT;
    std::int32_t m_timer = 0;
    std::int16_t m_active_angle_0_1deg = 0;
    std::int32_t m_speed_mmps = 0;
    std::uint8_t m_roll = 0;
    TimeUs       m_sync_start_us = 0;
    TimeUs       m_estop_hold_start_us = 0;
    std::int16_t m_estop_hold_angle = 0;
    TimeUs       m_estop_follow_err_start_us = 0;
    bool         m_estop_exit_pending = false;
};

}  // namespace rta
