// RT safety-monitor checks (run_safety_checks) — fail-safe reaction tests.
//
// Exercises the control-loop safety decisions WITHOUT the FreeRTOS control task:
//   #1 ESTOP latch (estop_pending) zeros setpoints + brakes
//   #2 Mode == Estop zeros setpoints
//   #4 SYS heartbeat timeout -> zero setpoints + RT SEB takeover (0x7B9)
//   #5 Host heartbeat timeout -> zero setpoints + assisted-stop brake (2000 kPa)
//   #7 Obstacle within stop distance at speed -> freeze steering + obstacle ESTOP
// (#6 steering following-error and #8 SYS->MTR estop_active need the full loop /
//  other node and are covered elsewhere.) g_bypass_eps_sync=true skips the
//  steering-follow path so this stays link-light (no SteeringControl object).

#include <atomic>
#include <cstdio>
#include <cstdint>

#include "esp_timer.h"
#include "protocol/compat/can_protocol.hpp"
#include "rt_state.h"
#include "system_mode.h"
#include "safety_monitor.h"

// Globals referenced by run_safety_checks (for the scenarios above).
std::atomic<int64_t> g_last_sys_hb_us{0};
std::atomic<int64_t> g_last_host_hb_us{0};
std::atomic<int32_t> g_mtr_actual_speed_mmps{0};
bool                 g_bypass_eps_sync = true;   // skip steering-follow path
std::atomic<int16_t> g_last_cmd_angle_0_1deg{INT16_MIN};
std::atomic<int32_t> g_ses_angle_0_1deg{0};
std::atomic<int32_t> g_brake_request_kpa{0};
bool g_bench_solo_mode = false;
rt::SteeringControl g_steering{};  // header-only; steering-follow path skipped via g_bypass_eps_sync

static int pass = 0;
static int fail = 0;
#define CHECK(cond) do { if (cond) pass++; else { fail++; std::fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)
#define CHECK_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (_a == _e) pass++; else { fail++; std::fprintf(stderr, "  FAIL %s:%d (%lld != %lld)\n", __FILE__, __LINE__, (long long)_a, (long long)_e); } \
} while (0)

int main() {
    std::printf("\n=== RT Safety Checks (run_safety_checks) ===\n");

    const int64_t now = 10'000'000;

    // #1 ESTOP latch
    {
        bool estop = false; uint8_t mode = uint8_t(can::Mode::Auto); bool seb = false;
        auto r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
        CHECK(!r.zero_setpoints);
        estop = true;
        r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
        CHECK(r.zero_setpoints);
        CHECK(r.disable_steering);
        CHECK_EQ(r.estop_reason, rt::kEstopReasonCanEstop);
    }

    // #2 Mode == Estop
    {
        bool estop = false; uint8_t mode = uint8_t(can::Mode::Estop); bool seb = false;
        auto r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
        CHECK(r.zero_setpoints);
    }

    // #4 SYS heartbeat timeout -> zero setpoints + SEB takeover
    {
        bool estop = false; uint8_t mode = uint8_t(can::Mode::Auto); bool seb = false;
        g_bench_solo_mode = false;
        g_last_sys_hb_us.store(now - int64_t(rt::kHeartbeatTimeoutMsSys) * 1000 - 1000);
        auto r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
        CHECK(r.zero_setpoints);
        CHECK_EQ(r.estop_reason, rt::kEstopReasonHeartbeat);
        CHECK(seb);
    }

    // #5 Host heartbeat timeout -> assisted stop brake
    {
        bool estop = false; uint8_t mode = uint8_t(can::Mode::Auto); bool seb = false;
        g_bench_solo_mode = false;
        g_brake_request_kpa.store(0);
        g_last_host_hb_us.store(now - int64_t(shared::kHeartbeatTimeoutMsHost) * 1000 - 1000);
        auto r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
        CHECK(r.zero_setpoints);
        CHECK_EQ(r.estop_reason, rt::kEstopReasonHeartbeat);
        CHECK_EQ(g_brake_request_kpa.load(), shared::kAssistStopKpa);
    }

    // #7 Obstacle within stop distance at speed
    {
        bool estop = false; uint8_t mode = uint8_t(can::Mode::Auto); bool seb = false;
        g_mtr_actual_speed_mmps.store(2000);  // 2.0 m/s, clearly above low-speed threshold
        auto r = run_safety_checks(now, false,
                                       shared::kObstacleStopMM - 10, estop, mode, seb);
        CHECK(r.disable_steering);
        CHECK(r.obstacle_triggered);
        CHECK_EQ(r.estop_reason, rt::kEstopReasonObstacle);
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
