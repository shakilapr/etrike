// Section 13: heartbeat timeout margin (WS6).
//
// Previously both SYS-RT heartbeat timeouts were `cycle * 2`, i.e. a single
// dropped heartbeat frame tripped them. That is fragile on a real FreeRTOS +
// WiFi + CAN system. They are now `cycle * 3` (tolerate two missed frames).
#include <iostream>
#include "protocol/compat/can.hpp"
#include "sys-esp32/src/config.h"
#include "rt-esp32/src/config.h"
#include "sys-esp32/src/safety_monitor.h"

namespace testbench {

// Compile-time policy assertions (fail the build if the policy regresses).
static_assert(rt::kHeartbeatTimeoutMsSys == can::gen::SysHeartbeat::kCycleMs * 3,
              "rt SYS-heartbeat timeout must tolerate two missed frames");
static_assert(sys::kHeartbeatTimeoutMsRt == can::gen::RtHeartbeat::kCycleMs * 3,
              "sys RT-heartbeat timeout must tolerate two missed frames");

bool test_heartbeat_margin() {
    std::cout << "TEST: heartbeat margins tolerate 2 missed frames, trip on the 3rd\n";

    // Real SYS heartbeat supervisor (instance used by sys_node).
    sys::SafetyMonitor sm;
    sm.init();
    sys::g_sys_test_time_us = 10'000'000;  // 10 s: past startup grace
    sm.feed_heartbeat_rt(1);

    // 1 missed frame (one cycle): still healthy.
    sys::g_sys_test_time_us += can::gen::RtHeartbeat::kCycleMs * 1000;
    const bool ok_one_missed = sm.heartbeat_ok();

    // refresh, then 2 missed frames (two cycles): with *3 still healthy.
    sm.feed_heartbeat_rt(2);
    sys::g_sys_test_time_us += can::gen::RtHeartbeat::kCycleMs * 2 * 1000;
    const bool ok_two_missed = sm.heartbeat_ok();

    // beyond the timeout (three+ cycles): unhealthy.
    sys::g_sys_test_time_us += can::gen::RtHeartbeat::kCycleMs * 2 * 1000;
    const bool ok_after = sm.heartbeat_ok();

    std::cout << "  rt->sys: timeout=" << sys::kHeartbeatTimeoutMsRt << "ms cycle="
              << can::gen::RtHeartbeat::kCycleMs << "ms  1miss=" << ok_one_missed
              << " 2miss=" << ok_two_missed << " expired=" << (!ok_after)
              << "\n";
    std::cout << "  sys->rt: timeout=" << rt::kHeartbeatTimeoutMsSys << "ms cycle="
              << can::gen::SysHeartbeat::kCycleMs << "ms (policy asserted)\n";

    if (!ok_one_missed || !ok_two_missed || ok_after) {
        std::cerr << "  FAIL: heartbeat margin policy incorrect\n";
        return false;
    }
    return true;
}

}  // namespace testbench
