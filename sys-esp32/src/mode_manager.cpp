// Mode state machine implementation.  Architecture.md §8.6.

#include "mode_manager.h"
#include <cstdio>

namespace sys {

void ModeManager::init() {
    m_mode = can::Mode::Manual;
    m_debounce = 0;
    m_prev_mode_btn = true;
    m_prev_start_btn = true;
}

bool ModeManager::tick(bool mode_btn_pressed, bool start_btn_pressed) {
    if (m_debounce > 0) { m_debounce--; return false; }

    // START button — exit ESTOP→MANUAL only
    if (falling_edge(m_prev_start_btn, start_btn_pressed)) {
        if (m_mode == can::Mode::Estop) {
            set_mode(can::Mode::Manual);
            m_prev_mode_btn = mode_btn_pressed;
            m_prev_start_btn = start_btn_pressed;
            m_debounce = kDebounceMs / 100;  // 500ms → 5 ticks @ 10 Hz
            return true;
        }
    }

    // MODE button — toggle MANUAL↔AUTO. Ignored in ESTOP.
    if (falling_edge(m_prev_mode_btn, mode_btn_pressed)) {
        if (m_mode == can::Mode::Manual) {
            set_mode(can::Mode::Auto);
        } else if (m_mode == can::Mode::Auto) {
            set_mode(can::Mode::Manual);
        }
        m_prev_mode_btn = mode_btn_pressed;
        m_prev_start_btn = start_btn_pressed;
        if (m_mode != can::Mode::Estop) {
            m_debounce = kDebounceMs / 100;
            return true;
        }
    }

    m_prev_mode_btn = mode_btn_pressed;
    m_prev_start_btn = start_btn_pressed;
    return false;
}

void ModeManager::force_estop() { set_mode(can::Mode::Estop); }

void ModeManager::set_from_can(uint8_t m) {
    if (m <= 2) set_mode(static_cast<can::Mode>(m));
}

const char* ModeManager::name() const {
    switch (m_mode) {
        case can::Mode::Manual: return "MANUAL";
        case can::Mode::Auto:   return "AUTO";
        case can::Mode::Estop:  return "ESTOP";
    }
    return "?";
}

}  // namespace sys
