// Mode state machine implementation.  Architecture.md §8.6.

#include "mode_manager.h"

namespace sys {

void ModeManager::init() {
    m_mode = can::Mode::Manual;
    m_run_enabled = false;
    m_debounce = 0;
    m_prev_mode_btn = false;   // match expected startup: button NOT pressed
    m_prev_start_btn = false;  // (gpio_get_level==0) → false when HIGH/pull-up
    m_start_seeded = false;
    m_estop_longpress_ctr = 0;
}

bool ModeManager::tick(bool mode_btn_pressed, bool start_btn_pressed,
                       bool hw_estop_active) {
    // Seed the START level on the first tick so a latching button already
    // engaged at power-up is not treated as a fresh enable edge. The operator
    // must cycle unlatch→latch to enable run after boot.
    if (!m_start_seeded) {
        m_prev_start_btn = start_btn_pressed;
        m_start_seeded = true;
    }

    // ── MODE button long-press (kEstopLongPressMs) validated reset ─────
    // Runs while in ESTOP, or while a latched fault is present in MANUAL.
    // Gated on the physical ESTOP being released so a held mushroom cannot be
    // cleared. Keeps counting on refusal so a held MODE re-attempts once the
    // cause clears. At 10 Hz: 5000 ms = 50 ticks.
    const bool reset_relevant =
        (m_mode == can::Mode::Estop) || sys::latched_fault_present();
    if (reset_relevant && !hw_estop_active) {
        if (mode_btn_pressed) {
            if (++m_estop_longpress_ctr >= (kEstopLongPressMs / 100)) {
                const bool ok = (m_mode == can::Mode::Estop)
                                    ? try_exit_estop()
                                    : reset_latched_faults_if_clearable();
                if (ok) {
                    m_estop_longpress_ctr = 0;
                    m_prev_mode_btn = false;  // prevent release from toggling to AUTO
                    m_prev_start_btn = start_btn_pressed;
                    m_debounce = kDebounceMs / 100;
                    return true;
                }
            }
        } else {
            m_estop_longpress_ctr = 0;  // released before timeout
        }
    } else {
        m_estop_longpress_ctr = 0;  // not reset-relevant, or ESTOP still asserted
    }

    if (m_debounce > 0) { m_debounce--; return false; }

    // ── START = run/enable latch (NOT an ESTOP reset) ─────────────────
    // Latched (rising edge) → run enabled. Released (falling edge) → stop:
    // drop power/mode authority via resolve_authority(); never force ESTOP,
    // never latch a fault, never broadcast 0x001.
    if (rising_edge(m_prev_start_btn, start_btn_pressed)) {
        if (m_mode != can::Mode::Estop) {
            m_run_enabled.store(true, std::memory_order_relaxed);
        }
    } else if (falling_edge(m_prev_start_btn, start_btn_pressed)) {
        m_run_enabled.store(false, std::memory_order_relaxed);
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

void ModeManager::force_estop() {
    // Entering ESTOP always drops the run/enable latch: the operator must
    // explicitly re-enable via START after recovering.
    set_mode(can::Mode::Estop);
    m_run_enabled.store(false, std::memory_order_relaxed);
}

bool ModeManager::try_exit_estop() {
    if (m_mode != can::Mode::Estop) return false;
    // Issue #7: never leave ESTOP while a latched fault's cause is still
    // asserted. Clearing the latch here, in the same step as the mode
    // transition, makes the reset a single transaction.
    if (!sys::latched_causes_currently_clearable()) return false;
    sys::clear_latched_fault_reasons();
    set_mode(can::Mode::Manual);
    return true;
}

bool ModeManager::reset_latched_faults_if_clearable() {
    // Validated clear while NOT in ESTOP (i.e. a latched fault clamped the
    // vehicle to MANUAL without ever latching ESTOP).
    if (m_mode == can::Mode::Estop) return false;
    if (!sys::latched_fault_present()) return false;
    if (!sys::latched_causes_currently_clearable()) return false;
    sys::clear_latched_fault_reasons();
    set_mode(can::Mode::Manual);
    return true;
}

bool ModeManager::try_exit_estop_remote(uint16_t blocker_mask) {
    if (m_mode != can::Mode::Estop) return false;
    if (blocker_mask != 0) return false;
    sys::clear_latched_fault_reasons();
    set_mode(can::Mode::Manual);
    return true;
}


void ModeManager::set_from_can(uint8_t m) {
    // Only MANUAL (0) and AUTO (1) are selectable via CAN 0x110.
    // ESTOP is a safety state triggered exclusively by hardware button,
    // CAN 0x001, or safety faults — never via mode command.
    // Safety guard: a CAN mode command must NOT clear a latched ESTOP
    // (only the MODE 5s long-press validated reset may).
    if (m_mode == can::Mode::Estop) return;
    if (m <= 1) set_mode(static_cast<can::Mode>(m));
}

bool ModeManager::parse_hmi_mode(uint8_t requested_mode) {
#if ENABLE_CAN_HMI
    // Ignore HMI requests if we are in ESTOP (hardware overrides software)
    if (m_mode == can::Mode::Estop) return false;

    // Only allow Manual (0) or Auto (1). Pure Sim (2) is not handled by SYS directly.
    if (requested_mode > 1) return false;

    can::Mode new_mode = static_cast<can::Mode>(requested_mode);
    if (m_mode.load() != new_mode) {
        set_mode(new_mode);
        return true; // Mode changed, caller should broadcast updated state
    }
#endif
    return false;
}

bool ModeManager::handle_driver_brake_takeover(bool lever_pressed) {
    if (lever_pressed && m_mode.load(std::memory_order_relaxed) == can::Mode::Auto) {
        set_mode(can::Mode::Manual);
        return true;
    }
    return false;
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
