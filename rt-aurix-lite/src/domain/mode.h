#pragma once
// Mode and ESTOP state model — pure domain logic.
// Behavior-preserving port of sys-esp32 ModeManager, adapted to rta types
// (typed ModeRequest from HMI 0x111; no CAN IDs, no logging, no clock).

#include <cstdint>
#include "core/time.h"
#include "core/types.h"
#include "config/timing_config.h"

namespace rta {

class ModeManager {
public:
    void init() noexcept {
        m_mode = Mode::Manual;
        m_debounce = 0;
        m_prev_mode_btn = false;   // button NOT pressed (active-low, pull-up)
        m_prev_start_btn = false;
        m_estop_longpress_ctr = 0;
    }

    Mode mode() const noexcept { return m_mode; }
    std::uint8_t mode_u8() const noexcept { return static_cast<std::uint8_t>(m_mode); }

    // Call at kModeTickRateHz (10 Hz). Returns true if mode changed.
    bool tick(bool mode_btn_pressed, bool start_btn_pressed) noexcept;

    // ESTOP is a safety state — triggered by hardware button, CAN 0x001,
    // or safety faults. Never by the mode button or HMI.
    void force_estop() noexcept { m_mode = Mode::Estop; }

    // Apply a mode request from HMI (typed). Only MANUAL/AUTO selectable;
    // ESTOP cannot be requested over HMI. Ignored while in ESTOP.
    bool apply_hmi_request(const ModeRequest& req) noexcept;

private:
    void set_mode(Mode m) noexcept { m_mode = m; }
    static bool falling_edge(bool prev, bool now) noexcept { return prev && !now; }

    Mode m_mode = Mode::Manual;
    int  m_debounce = 0;
    bool m_prev_mode_btn = false;
    bool m_prev_start_btn = false;
    int  m_estop_longpress_ctr = 0;
};

}  // namespace rta
