#pragma once
// RT ESP32-S3 Status LED Priority Cascade Evaluator
// Maps live RT gateway/motion/safety states to shared::led::VisualPattern.
// Reference: docs/hardware/rgb-status-led-visual-language.md

#include "status_led.h"

namespace rt {

struct RtLedInputs {
    bool can_or_spi_hw_fault    = false;  // P1: TWAI Bus-Off OR MCP2515 SPI/Bus-Off/TX-exhausted
    bool estop_active           = false;  // P2: Vehicle in ESTOP
    bool local_estop_cause      = false;  // P2: RT tripped ESTOP (steer error, MTR timeout, obstacle, stale 0x300)
    bool seb_emergency_takeover = false;  // P2: SYS 0x7B9 vanished; RT transmitting emergency 0x7B9
    bool actuator_fault         = false;  // P3: SES control enable rejected or SES error status != 0
    bool actuator_missing       = false;  // P3: SES 0x201 or MTR 0x206 feedback missing
    bool sys_hb_ok              = false;  // P4: SYS 0x7FE heartbeat fresh
    bool host_hb_ok             = false;  // P4: Host 0x7FC heartbeat fresh
    bool no_sys_authority       = false;  // P5: g_no_sys_authority == true (awaiting valid 0x011/0x110)
    bool boot_grace_active      = false;  // P5: Cold start grace window with no peers yet
    bool mode_auto              = false;  // P6: true = AUTO, false = MANUAL
    bool host_cmd_nonzero       = false;  // P6: Host 0x300 requesting non-zero motion
    bool spin_in_place_lockout  = false;  // P6: Host requesting yaw != 0 while speed == 0
    bool safety_clamp_active    = false;  // P6: Dynamic steering angle clamp or slew limiter active
};

constexpr shared::led::VisualPattern evaluate_rt_led(const RtLedInputs& in) {
    using namespace shared::led;

    // P1: Physical CAN or SPI interface failure supersedes all logic states
    if (in.can_or_spi_hw_fault) {
        return {DomainColor::Orange, BaseCadence::FastBlink, OverlayPip::None};
    }

    // P2: Safety ESTOP (never breathes — either solid or step-toggled)
    if (in.estop_active) {
        if (in.seb_emergency_takeover) {
            return {DomainColor::Red, BaseCadence::Solid, OverlayPip::BlueAuthority};
        }
        if (in.local_estop_cause) {
            return {DomainColor::Red, BaseCadence::Solid, OverlayPip::None};
        }
        return {DomainColor::Red, BaseCadence::RemoteEstop, OverlayPip::None};
    }

    // P3: Actuator Domain (SES / MTR faults or missing telemetry)
    if (in.actuator_fault) {
        return {DomainColor::Cyan, BaseCadence::Breathe, OverlayPip::RedFault};
    }
    if (in.actuator_missing && !in.boot_grace_active) {
        return {
            DomainColor::Cyan,
            BaseCadence::Breathe,
            in.host_cmd_nonzero ? OverlayPip::AmberModified : OverlayPip::None
        };
    }

    // P4: Boot / Authority Bootstrap
    if (in.no_sys_authority) {
        if (in.boot_grace_active && !in.sys_hb_ok && !in.host_hb_ok) {
            return {DomainColor::White, BaseCadence::Breathe, OverlayPip::None};
        }
        return {DomainColor::White, BaseCadence::Breathe, OverlayPip::BlueAuthority};
    }

    // P5: Peer Controller Domain (SYS or Host heartbeat missing)
    if (!in.sys_hb_ok || !in.host_hb_ok) {
        return {
            DomainColor::Yellow,
            BaseCadence::Breathe,
            in.host_cmd_nonzero ? OverlayPip::BlueAuthority : OverlayPip::None
        };
    }

    // P6: Normal Operating Modes (MANUAL = Purple, AUTO = Green)
    if (!in.mode_auto) {
        if (in.host_cmd_nonzero) {
            // Conflict: MANUAL selected, but receiving non-zero Host drive commands
            return {DomainColor::Purple, BaseCadence::Breathe, OverlayPip::AmberModified};
        }
        return {DomainColor::Purple, BaseCadence::Breathe, OverlayPip::None};
    }

    // AUTO Mode
    if (in.spin_in_place_lockout) {
        return {DomainColor::Green, BaseCadence::Breathe, OverlayPip::AmberModified};
    }
    if (in.host_cmd_nonzero) {
        if (in.safety_clamp_active) {
            return {DomainColor::Green, BaseCadence::Solid, OverlayPip::AmberModified};
        }
        return {DomainColor::Green, BaseCadence::Solid, OverlayPip::WhiteActivity};
    }
    return {DomainColor::Green, BaseCadence::Breathe, OverlayPip::None};
}

} // namespace rt
