// Owned definitions for the independent traction-inhibit / latched-fault state
// (issues #5/#7). Kept in their own translation unit so the same state links in
// both the ESP firmware and the native unit-test build (which excludes main.cpp).
//
// Ownership contract (see inhibit_state.h): each detector sets/clears ONLY its
// own bit via the atomic RMW helpers. g_latched_fault_reasons is mutated ONLY by
// the validated reset transaction (ModeManager::try_exit_estop).
#include "inhibit_state.h"

// SEB 0x721-derived live state at GLOBAL scope (matches main.cpp's unqualified
// references). 0xFF on g_seb_status_byte0 = "no 0x721 frame seen yet".
std::atomic<uint8_t> g_seb_status_byte0{0xFF};
std::atomic<uint8_t> g_seb_error_status{0};
std::atomic<bool>    g_seb_seen{false};

namespace sys {

std::atomic<uint32_t> g_inhibit_reasons{0};
std::atomic<uint32_t> g_latched_fault_reasons{0};

}  // namespace sys
