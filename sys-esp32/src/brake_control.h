#pragma once
// SYNTREE SEB brake control — boot state machine, 50 Hz TX, rolling ctr + checksum.
// Architecture.md §8.6. CAN 0x7B9 command, 0x721 status.
#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"
namespace sys {
// Architecture.md §8.5: BRAKE_BOOT_WAIT, BRAKE_LISTEN_SYNC, BRAKE_ACTIVE, BRAKE_DEGRADED
enum class BrakeState : uint8_t { BOOT_WAIT, LISTEN_SYNC, ACTIVE, DEGRADED };

// Derived timing constants from config
constexpr int kBrakeTickMs        = 1000 / kBrakeCmdRateHz;  // 20 ms per tick @ 50 Hz
constexpr int kBrakeBootTicks     = kBrakeBootWaitMs / kBrakeTickMs;
constexpr int kBrakeSyncTimeoutMs = 2000;
constexpr int kBrakeSyncTicks     = kBrakeSyncTimeoutMs / kBrakeTickMs;

class BrakeControl {
public:
    void init() { m_state=BrakeState::BOOT_WAIT; m_boot_timer=0; m_roll=0; }
    BrakeState state() const { return m_state; }

    // Call @ 50 Hz. lever: brake lever pressed. estop: ESTOP active. brake_kpa: from 0x205 (0=release).
    // seb_status: raw 8 bytes from 0x721 (nullptr if none received yet).
    // Returns true if a 0x7B9 frame should be transmitted. Fills out_frame.
    bool tick(bool lever, bool estop, int32_t brake_kpa,
              const uint8_t* seb_status, can::VcuSebReq& out) {
        switch (m_state) {
        case BrakeState::BOOT_WAIT:
            if (++m_boot_timer >= kBrakeBootTicks) {
                m_state = BrakeState::LISTEN_SYNC; m_boot_timer = 0;
            }
            return false;

        case BrakeState::LISTEN_SYNC:
            if (seb_status && (seb_status[0] & 1)) {
                m_state = BrakeState::ACTIVE;
                build_command(lever, estop, brake_kpa, out);
                return true;
            }
            // 2-second sync timeout → DEGRADED (architecture §8.6 BRAKE_DEGRADED)
            if (++m_boot_timer >= kBrakeSyncTicks) {
                m_state = BrakeState::DEGRADED;
                build_command(lever, estop, 0, out); // DEGRADED: lever-only, no CAN pressure
                return true;
            }
            return false;

        case BrakeState::ACTIVE:
            build_command(lever, estop, brake_kpa, out);
            return true;

        case BrakeState::DEGRADED:
            // Architecture §8.6: transmit 50 Hz with lever-based defaults, ignore CAN 0x205
            if (seb_status && (seb_status[0] & 1)) {
                m_state = BrakeState::ACTIVE;    // recover when 0x721 arrives
            }
            build_command(lever, estop, 0, out); // DEGRADED: lever-only, no CAN pressure
            return true;
        }
        return false;
    }

private:
    // Stroke raw value for 0mm (released) — formula: (stroke_mm + 30.0) / 0.05
    static constexpr uint16_t kStrokeRawZero =
        static_cast<uint16_t>((0.0f - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);

    void build_command(bool lever, bool estop, int32_t brake_kpa, can::VcuSebReq& out) {
        out.align_enable   = 1;
        out.control_enable = 1;
        out.roll_cnt_enable = 1;  // required by SYNTREE spec
        out.checksum_enable = 1;  // required by SYNTREE spec

        if (estop) {
            // ESTOP: Stroke Mode (0), max stroke 27mm → raw = (27+30)/0.05 = 1140
            // Architecture §8.6: ESTOP uses Stroke Mode, not Pressure Mode
            out.control_mode = 0;
            out.stroke_req = uint16_t((kBrakeMaxStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
            out.pressure_req = 0;
        } else if (brake_kpa > 0) {
            // Pressure Mode from 0x205 — verified kPa→raw conversion
            // Scale: 0.05 MPa/bit, range 0–5 MPa → raw = kPa * 0.02, clamp to kSebMaxPressureRaw
            out.control_mode = 1;  // Pressure mode (1-bit: 0=Stroke, 1=Pressure)
            out.stroke_req = kStrokeRawZero; // hold at 0mm
            // kPa * 0.02 = kPa / 50. Round to nearest integer.
            int32_t raw = (brake_kpa + 25) / 50;
            out.pressure_req = uint8_t(raw > shared::kSebMaxPressureRaw ? shared::kSebMaxPressureRaw : raw);
        } else if (lever) {
            // Manual lever: Stroke Mode (0), 15mm → raw = (15+30)/0.05 = 900
            // Architecture §8.6: MANUAL lever uses Stroke Mode, not Pressure Mode
            out.control_mode = 0;
            out.stroke_req = uint16_t((kBrakeManualStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
            out.pressure_req = 0;
        } else {
            // Released: Stroke Mode (0), 0mm → raw = (0+30)/0.05 = 600
            out.control_mode = 0;
            out.stroke_req = kStrokeRawZero;
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
