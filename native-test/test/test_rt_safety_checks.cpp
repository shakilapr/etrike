// RT safety-monitor checks (run_safety_checks) ? fail-safe reaction tests.
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
#include "protocol/codecs/ses.hpp"
#include "rt_state.h"
#include "system_mode.h"
#include "safety_monitor.h"

// Globals referenced by run_safety_checks (for the scenarios above).
std::atomic<int64_t> g_last_sys_hb_us{0};
std::atomic<int64_t> g_last_host_hb_us{0};
std::atomic<int32_t> g_mtr_applied_speed_command_mmps{0};
bool                 g_bypass_eps_sync = true;   // skip steering-follow path
bool                 g_bypass_mtr_absent = true; // these scenarios don't exercise MTR health (#8)
namespace rt {
MtrHealthSupervisor g_mtr_health;
}  // namespace rt
std::atomic<int64_t> g_last_mtr_feedback_us{-1};
std::atomic<int64_t> g_last_nonzero_cmd_us{-1};
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

    // #4 SYS heartbeat timeout -> zero setpoints (motion prohibited). Issue #3:
    // run_safety_checks no longer grants RT brake ownership on SYS-HB loss alone;
    // seb_takeover (emergency 0x7B9) is owned by the brake-fallback machine and
    // only becomes true once SYS 0x7B9 has ALSO disappeared.
    {
        bool estop = false; uint8_t mode = uint8_t(can::Mode::Auto); bool seb = false;
        g_bench_solo_mode = false;
        g_last_sys_hb_us.store(now - int64_t(rt::kHeartbeatTimeoutMsSys) * 1000 - 1000);
        auto r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
        CHECK(r.zero_setpoints);
        CHECK_EQ(r.estop_reason, rt::kEstopReasonHeartbeat);
        CHECK(!seb);   // takeover is NOT set here ? the fallback machine decides
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
        g_mtr_applied_speed_command_mmps.store(2000);  // 2.0 m/s, clearly above low-speed threshold
        auto r = run_safety_checks(now, false,
                                       shared::kObstacleStopMM - 10, estop, mode, seb);
        CHECK(r.disable_steering);
        CHECK(r.obstacle_triggered);
        CHECK_EQ(r.estop_reason, rt::kEstopReasonObstacle);
    }

    // #6 Steering following-error ESTOP (full steering path enabled)
    {
        g_bypass_eps_sync = false;   // enable the steering-follow check
        g_bench_solo_mode = false;
        g_last_sys_hb_us.store(now);    // fresh heartbeats so they don't mask the steering check
        g_last_host_hb_us.store(now);
        g_steering.init();
        etrike::protocol::codecs::ses::Command out{};
        // Drive SteeringControl to STEER_ACTIVE (valid 0x201 feedback, 20 ms/tick).
        for (int i = 0; i < 60; ++i) g_steering.tick(0, 1, uint32_t(i) * 20, out);
        CHECK(g_steering.state() == rt::SteerState::STEER_ACTIVE);

        bool estop = false; uint8_t mode = uint8_t(can::Mode::Auto); bool seb = false;
        g_last_cmd_angle_0_1deg.store(1000);   // commanded 100.0 deg
        g_ses_angle_0_1deg.store(0);           // actual 0 deg -> 1000 (0.1deg) error
        g_mtr_applied_speed_command_mmps.store(2000);   // speed shrinks the follow threshold
        bool triggered = false;
        for (int i = 0; i < 40; ++i) {
            auto r = run_safety_checks(now, false, UINT32_MAX, estop, mode, seb);
            if (r.zero_setpoints && r.estop_reason == rt::kEstopReasonFollowingError)
                triggered = true;
        }
        CHECK(triggered);
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
