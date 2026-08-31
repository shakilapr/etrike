// Mode and ESTOP state model — pure domain logic (see mode.h).

#include "domain/mode.h"

namespace rta {

bool ModeManager::tick(bool mode_btn_pressed, bool start_btn_pressed) noexcept {
    // MODE long-press (3 s) in ESTOP -> MANUAL. Runs before debounce so a
    // held MODE button is never blocked. At 10 Hz: 3000 ms = 30 ticks.
    if (m_mode == Mode::Estop) {
        if (mode_btn_pressed) {
            if (++m_estop_longpress_ctr >= (kModeLongPressMs / (1000 / kModeTickRateHz))) {
                set_mode(Mode::Manual);
                m_estop_longpress_ctr = 0;
                m_prev_mode_btn = false;   // prevent release from toggling back to AUTO
                m_prev_start_btn = start_btn_pressed;
                m_debounce = kModeDebounceMs / (1000 / kModeTickRateHz);
                return true;
            }
        } else {
            m_estop_longpress_ctr = 0;
        }
    } else {
        m_estop_longpress_ctr = 0;
    }

    if (m_debounce > 0) { --m_debounce; return false; }

    // START button — exit ESTOP -> MANUAL only.
    if (falling_edge(m_prev_start_btn, start_btn_pressed)) {
        if (m_mode == Mode::Estop) {
            set_mode(Mode::Manual);
            m_prev_mode_btn = mode_btn_pressed;
            m_prev_start_btn = start_btn_pressed;
            m_debounce = kModeDebounceMs / (1000 / kModeTickRateHz);
            return true;
        }
    }

    // MODE button — toggle MANUAL<->AUTO. Ignored in ESTOP.
    if (falling_edge(m_prev_mode_btn, mode_btn_pressed)) {
        if (m_mode == Mode::Manual) {
            set_mode(Mode::Auto);
        } else if (m_mode == Mode::Auto) {
            set_mode(Mode::Manual);
        }
        m_prev_mode_btn = mode_btn_pressed;
        m_prev_start_btn = start_btn_pressed;
        if (m_mode != Mode::Estop) {
            m_debounce = kModeDebounceMs / (1000 / kModeTickRateHz);
            return true;
        }
    }

    m_prev_mode_btn = mode_btn_pressed;
    m_prev_start_btn = start_btn_pressed;
    return false;
}

bool ModeManager::apply_hmi_request(const ModeRequest& req) noexcept {
    if (!req.valid) return false;
    // Ignore HMI requests while in ESTOP (hardware overrides software).
    if (m_mode == Mode::Estop) return false;
    // Only MANUAL/AUTO are selectable via HMI.
    if (req.mode == Mode::Estop) return false;
    if (m_mode != req.mode) {
        set_mode(req.mode);
        return true;
    }
    return false;
}

}  // namespace rta
