#pragma once
// SYS ESP32-S3 Status LED Priority Cascade Evaluator
// Maps live SYS safety/mode/bus states to shared::led::VisualPattern.
// Reference: docs/hardware/rgb-status-led-visual-language.md

#include "status_led.h"

namespace sys {

struct SysLedInputs {
    bool twai_bus_off         = false;  // P1: Low CAN TWAI in Bus-Off
    bool estop_active         = false;  // P2: Mode == ESTOP or safety ESTOP asserted
    bool local_estop_cause    = false;  // P2: Hardware mushroom btn, EGAS mismatch, task stall, or latched fault
    bool mtr_ack_retrying     = false;  // P2: Waiting for MTR 0x206 ESTOP_ACTIVE confirmation
    bool actuator_fault       = false;  // P3: SEB Level 3, persistent brake error, or MTR fault flags
    bool actuator_inhibit     = false;  // P3: Transient inhibit (MTR 0x206 or SEB 0x721 missing / excursion)
    bool rt_hb_ok             = false;  // P4: RT 0x7FD heartbeat healthy
    bool rt_cmd_stale         = false;  // P4: RT 0x204 setpoint stale while in AUTO
    bool boot_grace_active    = false;  // P5: Cold start grace window with no peers yet
    bool mode_auto            = false;  // P6: true = AUTO, false = MANUAL
    bool drive_cmd_nonzero    = false;  // P6: Autonomous speed command (0x204) > 0
    bool manual_active_input  = false;  // P6: Rider throttle > 0 or brake lever pressed
    bool brake_lever_override = false;  // P6: Rider brake lever overriding AUTO motion
};

constexpr shared::led::VisualPattern evaluate_sys_led(const SysLedInputs& in) {
    using namespace shared::led;

    // P1: Physical CAN interface failure supersedes all logic states
    if (in.twai_bus_off) {
        return {DomainColor::Orange, BaseCadence::FastBlink, OverlayPip::None};
    }

    // P2: Safety ESTOP (never breathes — either solid or step-toggled)
    if (in.estop_active) {
        if (in.local_estop_cause) {
            return {
                DomainColor::Red,
                BaseCadence::Solid,
                in.mtr_ack_retrying ? OverlayPip::WhiteActivity : OverlayPip::None
            };
        }
        return {DomainColor::Red, BaseCadence::RemoteEstop, OverlayPip::None};
    }

    // P3: Actuator Domain (MTR / SEB faults or inhibits)
    if (in.actuator_fault) {
        return {DomainColor::Cyan, BaseCadence::Breathe, OverlayPip::RedFault};
    }
    if (in.actuator_inhibit) {
        return {
            DomainColor::Cyan,
            BaseCadence::Breathe,
            in.drive_cmd_nonzero ? OverlayPip::AmberModified : OverlayPip::None
        };
    }

    // P4: Peer Controller Domain (RT heartbeat or command stream)
    if (!in.rt_hb_ok) {
        if (in.boot_grace_active) {
            return {DomainColor::White, BaseCadence::Breathe, OverlayPip::None};
        }
        return {
            DomainColor::Yellow,
            BaseCadence::Breathe,
            in.drive_cmd_nonzero ? OverlayPip::BlueAuthority : OverlayPip::None
        };
    }
    if (in.rt_cmd_stale && in.mode_auto) {
        return {DomainColor::Yellow, BaseCadence::Breathe, OverlayPip::AmberModified};
    }

    // P5: Cold Boot Grace
    if (in.boot_grace_active) {
        return {DomainColor::White, BaseCadence::Breathe, OverlayPip::None};
    }

    // P6: Normal Operating Modes (MANUAL = Purple, AUTO = Green)
    if (!in.mode_auto) {
        if (in.drive_cmd_nonzero) {
            // Conflict: MANUAL selected, but receiving non-zero autonomous commands
            return {DomainColor::Purple, BaseCadence::Breathe, OverlayPip::AmberModified};
        }
        if (in.manual_active_input) {
            return {DomainColor::Purple, BaseCadence::Solid, OverlayPip::WhiteActivity};
        }
        return {DomainColor::Purple, BaseCadence::Breathe, OverlayPip::None};
    }

    // AUTO Mode
    if (in.brake_lever_override) {
        return {DomainColor::Green, BaseCadence::Solid, OverlayPip::AmberModified};
    }
    if (in.drive_cmd_nonzero) {
        return {DomainColor::Green, BaseCadence::Solid, OverlayPip::WhiteActivity};
    }
    return {DomainColor::Green, BaseCadence::Breathe, OverlayPip::None};
}

} // namespace sys
