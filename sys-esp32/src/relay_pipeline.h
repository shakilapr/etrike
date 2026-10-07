#pragma once

#include <cstdint>
#include <algorithm>
#include "bypass_modes.h"
#include "inhibit_state.h"
#include "protocol/compat/can.hpp"
#include "protocol/codecs/seb.hpp"

namespace sys {

// Rule 8b & Rule 11: Pure relay logic for Mode and Power commands.
// In BenchPassthrough: 1:1 relay from high-level requests; no START latch or inhibit checks.
// In Production: ASIL authority resolution via resolve_authority().
inline void compute_mode_and_pwr_relay(
    etrike::ControlPath control_path,
    can::Mode requested_mode,
    bool power_requested,
    bool run_enabled,
    bool any_inhibit,
    bool& out_mode_auto,
    bool& out_power_on
) {
    if (control_path == etrike::ControlPath::BenchPassthrough) {
        // Pure relay: passthrough requested mode and power directly
        out_mode_auto = (requested_mode == can::Mode::Auto);
        out_power_on  = power_requested;
    } else {
        // Production: safety arbitration
        const bool mode_is_estop = (requested_mode == can::Mode::Estop);
        const bool resolved_auto = (requested_mode == can::Mode::Auto);
        const auto auth = sys::resolve_authority(mode_is_estop, resolved_auto, power_requested, run_enabled);
        out_mode_auto = auth.mode_auto;
        out_power_on  = auth.power_on;
    }
}

// Rule 8b & Rule 11: Pure relay logic for SEB brake command.
// In BenchPassthrough: 1:1 relay from RT brake intent (0x205) -> SEB command (0x7B9).
inline void compute_brake_relay(
    etrike::ControlPath control_path,
    int32_t brake_kpa,
    can::custom::seb::Command& out_cmd
) {
    if (control_path == etrike::ControlPath::BenchPassthrough) {
        out_cmd = {};
        out_cmd.alignment_enable = true;
        out_cmd.control_enable = true;
        out_cmd.auto_brake = (brake_kpa > 0);
        if (brake_kpa > 0) {
            out_cmd.control_mode = etrike::protocol::codecs::seb::ControlMode::Pressure;
            out_cmd.stroke_request_raw = 600; // 0mm
            int32_t raw = (brake_kpa + 25) / 50;
            out_cmd.pressure_request_raw = static_cast<uint8_t>(raw > 100 ? 100 : (raw < 0 ? 0 : raw));
        } else {
            out_cmd.control_mode = etrike::protocol::codecs::seb::ControlMode::Stroke;
            out_cmd.stroke_request_raw = 600;
            out_cmd.pressure_request_raw = 0;
        }
    }
}

} // namespace sys
