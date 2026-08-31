#pragma once
// Brake controller — brake-by-wire unit command generation.
// Pure domain logic: no CAN IDs, no IPC, no logging, no clock reads.
// Behavior-preserving port of sys-esp32 BrakeControl (brake_control.h),
// adapted to rta types and config. Wire encoding (seb::Command) stays in
// the protocol layer; the domain produces typed BrakeCommand values.

#include <cstdint>
#include "core/time.h"
#include "core/types.h"
#include "config/control_config.h"
#include "shared_config.h"  // kBrakeStrokeScale/Offset

namespace rta {

enum class BrakeState : std::uint8_t {
    BOOT_WAIT,
    LISTEN_SYNC,
    ACTIVE,
    DEGRADED,
};

constexpr const char* brake_state_name(BrakeState s) noexcept {
    switch (s) {
        case BrakeState::BOOT_WAIT:   return "BOOT_WAIT";
        case BrakeState::LISTEN_SYNC: return "LISTEN_SYNC";
        case BrakeState::ACTIVE:      return "ACTIVE";
        case BrakeState::DEGRADED:    return "DEGRADED";
    }
    return "?";
}

class BrakeControl {
public:
    void init() noexcept {
        m_state = BrakeState::BOOT_WAIT;
        m_boot_timer = 0;
        m_roll = 0;
        m_sync_stroke_raw = 0;
        m_use_sync_stroke = false;
    }

    BrakeState state() const noexcept { return m_state; }

    // Call @ 50 Hz.
    //   lever      : brake lever pressed (driver override).
    //   estop      : ESTOP active.
    //   brake_kpa  : commanded brake pressure (0 = release).
    //   mode       : current system mode.
    //   fb         : brake feedback (valid/alignment/stroke raw).
    //   out        : typed BrakeCommand (raw stroke/pressure, mode select).
    // Returns true if a brake command should be transmitted.
    bool tick(bool lever, bool estop, std::int32_t brake_kpa, Mode mode,
              const BrakeFeedback& fb, BrakeCommand& out);

private:
    void build_command(bool lever, bool estop, std::int32_t brake_kpa, BrakeCommand& out);

    BrakeState m_state = BrakeState::BOOT_WAIT;
    std::int32_t m_boot_timer = 0;
    std::uint8_t m_roll = 0;
    std::uint16_t m_sync_stroke_raw = 0;  // captured stroke during LISTEN_SYNC
    bool m_use_sync_stroke = false;       // first ACTIVE frame holds sync position
};

}  // namespace rta
