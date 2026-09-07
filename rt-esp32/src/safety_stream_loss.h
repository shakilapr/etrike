#pragma once
// SYS_SAFETY_STS (0x011) freshness fail-safe (architecture §8.6).
//
// If SYS stops publishing 0x011 for longer than kSysSafetyStsTimeoutUs, RT
// must KEEP (or SET) the E-stop latch — never silently clear it. The trigger
// condition is isolated here so it can be unit-tested without the control
// loop / FreeRTOS. t_control calls this and, when true, sets m_estop_pending.
//
// last_rx_us == 0 means "never received" (startup) — NOT a loss.

#include <cstdint>

namespace rt {

constexpr int64_t kSysSafetyStsTimeoutUs = 700000;  // 0x011 lost > 700 ms

inline bool sys_safety_sts_lost(int64_t last_rx_us, int64_t now_us) {
    return last_rx_us != 0 && (now_us - last_rx_us > kSysSafetyStsTimeoutUs);
}

}  // namespace rt
