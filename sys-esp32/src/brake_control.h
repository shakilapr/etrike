#pragma once
// SYNTREE SEB brake control — boot state machine, 50 Hz TX, rolling ctr + checksum.
// Architecture.md §8.6. CAN 0x720 command, 0x721 status.
#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"
namespace sys {
enum class BrakeState : uint8_t { BOOT_WAIT, LISTEN_SYNC, ACTIVE, FAULT };
class BrakeControl {
public:
    void init() { m_state=BrakeState::BOOT_WAIT; m_boot_timer=0; m_roll=0; }
    BrakeState state() const { return m_state; }

    // Call @ 50 Hz. lever: brake lever pressed. estop: ESTOP active. brake_kpa: from 0x203 (0=release).
    // seb_status: raw 8 bytes from 0x721 (nullptr if none received yet).
    // Returns true if a 0x720 frame should be transmitted. Fills out_frame.
    bool tick(bool lever, bool estop, int32_t brake_kpa,
              const uint8_t* seb_status, can::VcuSebReq& out) {
        switch (m_state) {
        case BrakeState::BOOT_WAIT:
            if (++m_boot_timer >= (kBrakeBootWaitMs / 20)) { // 500ms / 20ms = 25 ticks
                m_state = BrakeState::LISTEN_SYNC; m_boot_timer = 0;
            }
            return false;
        case BrakeState::LISTEN_SYNC:
            if (seb_status) {
                // Check alignment bit (Byte 0, bit 0)
                if (seb_status[0] & 1) {
                    m_state = BrakeState::ACTIVE;
                    goto build_frame;  // send first frame immediately
                } else { return false; }
            } else { return false; }
        case BrakeState::ACTIVE:
        build_frame:
            build_command(lever, estop, brake_kpa, out);
            return true;
        case BrakeState::FAULT:
            return false;
        }
        return false;
    }

private:
    void build_command(bool lever, bool estop, int32_t brake_kpa, can::VcuSebReq& out) {
        out.align_enable   = 1;
        out.control_enable = 1;
        out.roll_cnt_enable = 1;  // required by SYNTREE spec
        out.checksum_enable = 1;  // required by SYNTREE spec

        if (estop) {
            // ESTOP: max stroke, Stroke Mode
            out.control_mode = 1;
            out.stroke_req = uint16_t((kBrakeMaxStroke - kBrakeStrokeOffset) / kBrakeStrokeScale);
            out.pressure_req = 0;
        } else if (brake_kpa > 0) {
            // Pressure Mode from 0x203 — verified kPa→raw conversion
            // Scale: 0.05 MPa/bit, range 0–5 MPa → raw = kPa * 0.02, clamp to 100
            out.control_mode = 2;
            out.stroke_req = 600; // hold at 0mm
            // kPa * 0.02 = kPa / 50. Round to nearest integer.
            int32_t raw = (brake_kpa + 25) / 50;
            out.pressure_req = uint8_t(raw > 100 ? 100 : raw);
        } else if (lever) {
            // Manual lever: Stroke Mode
            out.control_mode = 1;
            out.stroke_req = uint16_t((kBrakeManualStroke - kBrakeStrokeOffset) / kBrakeStrokeScale);
            out.pressure_req = 0;
        } else {
            // Released
            out.control_mode = 1;
            out.stroke_req = 600; // 0mm
            out.pressure_req = 0;
        }

        out.rolling_counter = m_roll;
        m_roll = (m_roll + 1) & 0x0F;
        // Checksum computed in pack()
    }

    BrakeState m_state;
    int m_boot_timer = 0;
    uint8_t m_roll = 0;
};
}
