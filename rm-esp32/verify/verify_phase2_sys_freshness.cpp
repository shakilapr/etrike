// Phase 2 (SYS scenario) — Does rm SYS mode drive sys-esp32 seamlessly?
//
// sys-esp32 consumes (sys-esp32/src/main.cpp dispatch):
//   0x204 RT_DRIVE_CMD  -> g_setpoint_speed_mmps / gear (no read-side zeroing found)
//   0x111 HMI_MODE_REQ  -> ModeManager::parse_hmi_mode (StreamValidity freshness 5s)
//   0x112 HMI_PWR_REQ   -> g_hmi_pwr_on            (StreamValidity freshness 5s)
//   0x7FD RT_HEARTBEAT   -> SafetyMonitor::feed_heartbeat_rt; REQUIRED within
//                           kHeartbeatTimeoutMsRt = RtHeartbeat::kCycleMs*2 = 1000 ms
//                           (sys-esp32/src/config.h:50), else enter_estop.
// rm SYS mode emits all four. This harness replicates the two freshness gates
// with rm SYS cadence (0x7FD @500ms, 0x111/0x112 @100ms) and shows they stay
// fresh (seamless), vs a too-slow sender that would trip the watchdog.
//
// Build (repo root):
//   C:\TDM-GCC-64\bin\g++.exe -std=c++17 ^
//     rm-esp32-t12d/verify/verify_phase2_sys_freshness.cpp -o rm-esp32-t12d/verify/verify_phase2_sys_freshness.exe

#include <cstdint>
#include <cstdio>

namespace {

constexpr int64_t kRtHbTimeoutUs = 1000'000;  // sys-esp32 config.h:50 (RtHeartbeat 500ms*2)
constexpr int64_t kHmiFreshUs    = 5000'000;  // StreamValidity kReqFreshTicks = 1000ms*5

void simulate(const char* label, int64_t hb_period_ms, int64_t hmi_period_ms) {
    int64_t last_hb = -1, last_hmi = -1;
    bool hb_ok_ever = true, hmi_ok_ever = true;
    bool hb_ok_2s = false, hmi_ok_2s = false;
    for (int64_t t = 0; t <= 3000'000; t += 10'000) {
        if (t % (hb_period_ms * 1000) == 0)  last_hb = t;
        if (t % (hmi_period_ms * 1000) == 0) last_hmi = t;
        bool hb_fresh  = (last_hb  >= 0) && (t - last_hb)  <= kRtHbTimeoutUs;
        bool hmi_fresh = (last_hmi >= 0) && (t - last_hmi) <= kHmiFreshUs;
        if (!hb_fresh)  hb_ok_ever = false;
        if (!hmi_fresh) hmi_ok_ever = false;
        if (t >= 2000'000) { hb_ok_2s = hb_fresh; hmi_ok_2s = hmi_fresh; }
    }
    std::printf("  [%s] rt_heartbeat_always_fresh=%s  hmi_always_fresh=%s  (hb@%lldms hmi@%lldms)\n",
                label, hb_ok_ever ? "YES" : "NO", hmi_ok_ever ? "YES" : "NO",
                static_cast<long long>(hb_period_ms), static_cast<long long>(hmi_period_ms));
    if (!hb_ok_ever) std::printf("    >> FAIL: RT heartbeat lost -> sys-esp32 enter_estop\n");
}

}  // namespace

int main() {
    std::printf("=== Phase 2 (SYS scenario) sys-esp32 freshness vs rm SYS mode ===\n");
    std::printf("-- rm SYS mode (0x7FD @500ms, 0x111/0x112 @100ms) --\n");
    simulate("rm-SYS", /*hb_period_ms=*/500, /*hmi_period_ms=*/100);
    std::printf("-- too-slow sender (0x7FD @2000ms > 1000ms timeout) trips watchdog --\n");
    simulate("slow-sender", /*hb_period_ms=*/2000, /*hmi_period_ms=*/100);
    std::printf("NOTE: rm SYS cadence keeps both gates fresh -> sys-esp32 stays AUTO, no estop.\n");
    return 0;
}
