// Brake controller — pure domain logic (see brake.h).

#include "domain/brake.h"

namespace rta {

namespace {
constexpr std::uint16_t kStrokeRawZero =
    static_cast<std::uint16_t>((0.0f - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
}  // namespace

void BrakeControl::build_command(bool lever, bool estop, std::int32_t brake_kpa,
                                 BrakeCommand& out) {
    out.valid = true;
    // Boot-sync hold: on first ACTIVE frame, command the captured stroke.
    if (m_use_sync_stroke && m_sync_stroke_raw > 0) {
        out.stroke_mode = true;
        out.stroke_raw = m_sync_stroke_raw;
        out.pressure_raw = 0;
        out.auto_brake = false;
    } else if (estop) {
        // ESTOP: Stroke mode, max stroke 27 mm.
        out.stroke_mode = true;
        out.stroke_raw = static_cast<std::uint16_t>(
            (kBrakeMaxStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        out.pressure_raw = 0;
        out.auto_brake = false;
    } else if (lever) {
        // Driver override always wins — 15 mm stroke.
        out.stroke_mode = true;
        out.stroke_raw = static_cast<std::uint16_t>(
            (kBrakeManualStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        out.pressure_raw = 0;
        out.auto_brake = false;
    } else if (brake_kpa > 0) {
        // Pressure mode from automated braking (Jetson via arbitration).
        out.stroke_mode = false;
        out.stroke_raw = kStrokeRawZero;  // hold at 0 mm
        std::int32_t raw = (brake_kpa + 25) / 50;  // kPa * 0.02, rounded
        out.pressure_raw = static_cast<std::uint8_t>(
            raw > shared::kSebMaxPressureRaw ? shared::kSebMaxPressureRaw : raw);
        out.auto_brake = true;
    } else {
        // Released: Stroke mode, 0 mm.
        out.stroke_mode = true;
        out.stroke_raw = kStrokeRawZero;
        out.pressure_raw = 0;
        out.auto_brake = false;
    }
    out.rolling_counter = m_roll;
    m_roll = (m_roll + 1) & 0x0Fu;
}

bool BrakeControl::tick(bool lever, bool estop, std::int32_t brake_kpa, Mode mode,
                        const BrakeFeedback& fb, BrakeCommand& out) {
    (void)mode;
    bool has_status = fb.valid;  // a 0x721 frame was received

    if (m_state == BrakeState::BOOT_WAIT) {
        if (++m_boot_timer >= (kBrakeBootWaitMs * kBrakeCmdRateHz) / 1000) {
            m_state = BrakeState::LISTEN_SYNC;
            m_boot_timer = 0;
        }
        out.valid = false;
        return false;
    }

    if (m_state == BrakeState::LISTEN_SYNC) {
        if (has_status) {
            if (m_sync_stroke_raw == 0) {
                m_sync_stroke_raw = fb.stroke_raw;  // capture for hold-on-sync
            }
            if (fb.alignment) {
                m_state = BrakeState::ACTIVE;
                m_use_sync_stroke = true;
                build_command(lever, estop, brake_kpa, out);
                m_use_sync_stroke = false;
                return true;
            }
        }
        // 2-second sync timeout -> DEGRADED (lever-only, no CAN pressure).
        if (++m_boot_timer >= (kBrakeSyncTimeoutMs * kBrakeCmdRateHz) / 1000) {
            m_state = BrakeState::DEGRADED;
            build_command(lever, estop, 0, out);
            return true;
        }
        out.valid = false;
        return false;
    }

    if (m_state == BrakeState::ACTIVE) {
        build_command(lever, estop, brake_kpa, out);
        return true;
    }

    // DEGRADED: lever-based only; recover to ACTIVE on aligned feedback.
    if (m_state == BrakeState::DEGRADED) {
        if (has_status && fb.alignment) {
            m_state = BrakeState::ACTIVE;
        }
        build_command(lever, estop, 0, out);
        return true;
    }

    out.valid = false;
    return false;
}

}  // namespace rta
