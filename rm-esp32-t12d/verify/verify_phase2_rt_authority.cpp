// Phase 2 (RT scenario) — Does rm RT-mode drive rt-esp32 seamlessly?
//
// rt-esp32 grants motion authority only when:
//   kMotionRequired = READY_BIT_SAFETY(0x011) | READY_BIT_MODE(0x110) | READY_BIT_HOST(0x300)
// (see rt-esp32/src/safety_stream_loss.h:50-51). rm RT mode emits 0x300/0x301/0x303/
// 0x7FC/0x111/0x112 but NOT 0x011 or 0x110. This harness drives the REAL
// rt-esp32 SafetyStreamSupervisor with rm's RT frame cadence and shows whether
// motion becomes ready — with and without a SYS-authority source.
//
// Build (repo root):
//   C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Irt-esp32/src ^
//     rm-esp32-t12d/verify/verify_phase2_rt_authority.cpp -o rm-esp32-t12d/verify/verify_phase2_rt_authority.exe

#include <cstdint>
#include <cstdio>
#include <string>

#include "safety_stream_loss.h"   // real rt-esp32 authority state machine

namespace {

// Freshness windows (mirror rt-esp32 runtime): HOST drive cmd 0x300 @10ms,
// HOST heartbeat 0x7FC @500ms, SYS_MODE_CMD 0x110 @100ms, SYS_SAFETY_STS 0x011 @200ms.
constexpr int64_t kHostFreshUs    = 1500'000;   // shared::kHeartbeatTimeoutMsHost
constexpr int64_t kModeFreshUs     = 500'000;    // SysModeCmd::kCycleMs*5
constexpr int64_t kSafetyFreshUs   = 700'000;    // rt::kSysSafetyStsTimeoutUs

void simulate(const char* label, bool with_sys_authority) {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);

    int64_t last_011_us = -1;   // SYS_SAFETY_STS (0x011) rx time
    int64_t last_110_us = -1;   // SYS_MODE_CMD   (0x110) rx time
    int64_t last_300_us = -1;   // HOST_DRIVE_CMD (0x300) rx time
    bool mode_valid = false;

    bool ever_ready = false;
    bool ready_at_2000ms = false;
    int64_t acquired_at = -1;

    for (int64_t t = 0; t <= 3000'000; t += 10'000) {  // 10 ms steps, 3 s
        // rm RT-mode cadence (always present):
        if (t % 10'000 == 0)         last_300_us = t;          // 0x300 @10ms
        if (t % 500'000 == 0)        { /* 0x7FC @500ms (host hb, not needed for mask) */ }
        if (t % 100'000 == 0)        { /* 0x111/0x112 @100ms (forwarded, irrelevant to mask) */ }

        // SYS authority source (present only if a SYS node / rm emits it):
        if (with_sys_authority) {
            if (t % 200'000 == 0)    last_011_us = t;          // 0x011 @200ms
            if (t % 100'000 == 0)    { last_110_us = t; mode_valid = true; }
        }

        // READY_BIT_HOST: 0x300 fresh
        bool host_ready = (t - last_300_us) <= kHostFreshUs;
        // READY_BIT_SAFETY: 0x011 stream acquired & fresh
        auto ssts = sup.update(t, last_011_us);
        bool safety_ready = ssts.motion_authorized;
        if (acquired_at < 0 && ssts.state == rt::SafetyStreamState::ACQUIRED)
            acquired_at = t;
        // READY_BIT_MODE: 0x110 fresh
        bool mode_ready = mode_valid && (t - last_110_us) <= kModeFreshUs;

        uint8_t mask = (safety_ready ? rt::READY_BIT_SAFETY : 0)
                     | (mode_ready    ? rt::READY_BIT_MODE    : 0)
                     | (host_ready    ? rt::READY_BIT_HOST    : 0);
        bool motion_ready = rt::is_motion_ready(mask);
        if (motion_ready) ever_ready = true;
        if (t >= 2000'000) ready_at_2000ms = motion_ready;
    }

    std::printf("  [%s] motion_ready(ever)=%s  ready@2s=%s  sys_safety_acquired_at=%s\n",
                label, ever_ready ? "YES" : "NO", ready_at_2000ms ? "YES" : "NO",
                acquired_at < 0 ? "never" : (std::to_string(acquired_at / 1000) + "ms").c_str());
    if (with_sys_authority && !ever_ready)
        std::printf("    >> FAIL: SYS authority present but motion never ready\n");
    if (!with_sys_authority && ever_ready)
        std::printf("    >> UNEXPECTED: motion ready without SYS authority\n");
}

}  // namespace

int main() {
    std::printf("=== Phase 2 (RT scenario) rt-esp32 motion authority vs rm RT mode ===\n");
    std::printf("-- rm RT mode ONLY (no 0x011 / 0x110) --\n");
    simulate("rm-RT-only", /*with_sys_authority=*/false);
    std::printf("-- rm RT mode + SYS authority frames (0x011 @200ms, 0x110 @100ms) --\n");
    simulate("rm-RT+sys", /*with_sys_authority=*/true);
    std::printf("NOTE: rm RT mode does NOT emit 0x011/0x110 (see src/can_emitter.h), so the\n");
    std::printf("      'rm-RT-only' result shows the real-world behavior unless rt-esp32 is\n");
    std::printf("      booted with g_bench_solo_mode (rt-esp32/src/main.cpp:1311).\n");
    return 0;
}
