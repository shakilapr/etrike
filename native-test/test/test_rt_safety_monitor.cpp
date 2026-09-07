#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "protocol/compat/can_protocol.hpp"
#include "rt_state.h"
#include "safety_monitor.h"

rt::Mcp2515Driver g_can_high;
rt::PhysicsModel g_physics;
rt::SpeedController g_speed_ctrl;
rt::SteeringControl g_steering;
rt::DualHeartbeat g_heartbeat;
rt::CmdWatchdog g_watchdog;

bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = false;
bool g_bypass_seb_sync = false;
bool g_bypass_mtr_absent = false;

namespace rt {
MtrHealthSupervisor g_mtr_health;
}  // namespace rt
std::atomic<int64_t> g_last_mtr_feedback_us{-1};
std::atomic<int64_t> g_last_nonzero_cmd_us{-1};

QueueHandle_t g_safety_evt_q = nullptr;

std::atomic<int32_t> g_brake_request_kpa{0};
std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
std::atomic<int32_t> g_ses_angle_0_1deg{INT16_MIN};
std::atomic<uint8_t> g_ses_angle_status{0};
std::atomic<int32_t> g_brake_kpa_to_send{0};
std::atomic<int32_t> g_mtr_applied_speed_command_mmps{0};
std::atomic<uint8_t> g_mode_current{0};
std::atomic<bool> g_seb_takeover{false};
std::atomic<int64_t> g_last_sys_hb_us{0};
std::atomic<int64_t> g_last_host_hb_us{0};
std::atomic<int64_t> g_last_estop_sent_us{0};
std::atomic<int16_t> g_last_cmd_angle_0_1deg{0};
std::atomic<int16_t> g_pid_output_mmps{0};
std::atomic<int32_t> g_last_speed_setpoint_mmps{0};
std::atomic<bool> g_reversing{false};
std::atomic<uint16_t> g_ses_motor_current{0};
std::atomic<uint16_t> g_ses_ecu_temp{0};
std::atomic<uint16_t> g_ses_pow_volt{0};
std::atomic<uint8_t> g_ses_error_status{0};
std::atomic<uint16_t> g_seb_pressure_raw{0};
std::atomic<uint8_t> g_seb_error_status{0};
std::atomic<uint16_t> g_seb_motor_current{0};
std::atomic<uint16_t> g_seb_ecu_temp_c{0};

QueueHandle_t g_can_rx_low_q = nullptr;
QueueHandle_t g_can_rx_high_q = nullptr;
QueueHandle_t g_cmd_q = nullptr;
QueueHandle_t g_setpoint_q = nullptr;
QueueHandle_t g_gw_tx_low_q = nullptr;
QueueHandle_t g_gw_tx_high_q = nullptr;

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if (cond) { pass++; } \
    else { fail++; std::fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); } \
} while (0)

static void reset_state() {
    g_bench_solo_mode = false;
    g_bypass_eps_sync = false;
    // These unit scenarios exercise SYS/Host/steering safety logic, not the
    // MTR-feedback health check (#8); with no MTR present, bypass it so a
    // stale-0x206 trip does not mask the scenario under test. Dedicated #8
    // scenarios set g_bypass_mtr_absent = false and feed real timestamps.
    g_bypass_mtr_absent = true;
    rt::g_mtr_health.reset();
    g_brake_request_kpa.store(0);
    g_obstacle_mm.store(UINT32_MAX);
    g_ses_angle_0_1deg.store(INT16_MIN);
    g_ses_angle_status.store(0);
    g_brake_kpa_to_send.store(0);
    g_mtr_applied_speed_command_mmps.store(0);
    g_mode_current.store(0);
    g_seb_takeover.store(false);
    g_last_sys_hb_us.store(0);
    g_last_host_hb_us.store(0);
    g_last_estop_sent_us.store(0);
    g_last_cmd_angle_0_1deg.store(0);
    g_last_mtr_feedback_us.store(-1);
    g_last_nonzero_cmd_us.store(-1);
    g_steering.init();

    bool estop_pending = false;
    bool seb_takeover = false;
    (void)run_safety_checks(5'000'000, false, UINT32_MAX,
                            estop_pending, uint8_t(can::Mode::Manual), seb_takeover);
}

static void boot_steering_to_active() {
    can::custom::ses::Command out;
    int64_t now_ms = 0;
    int ticks = (rt::kSteerBootWaitMs * rt::kSteerCmdRateHz) / 1000;
    int dt_ms = 1000 / rt::kSteerCmdRateHz;
    for (int i = 0; i < ticks; ++i) {
        g_steering.tick(INT16_MIN, 0, now_ms += dt_ms, out);
    }
    g_steering.tick(0, 1, now_ms += dt_ms, out);
    CHECK(g_steering.state() == rt::SteerState::STEER_ACTIVE);
}

int main() {
    std::printf("\n=== RT Safety Monitor ===\n\n");

    {
        reset_state();
        bool estop_pending = true;
        bool seb_takeover = false;
        auto r = run_safety_checks(5'000'000, false, UINT32_MAX,
                                   estop_pending, uint8_t(can::Mode::Manual), seb_takeover);

        CHECK(r.zero_setpoints);
        CHECK(r.brake_kpa == shared::kMaxBrakeKpa);
        CHECK(r.disable_steering);
        CHECK(estop_pending);  // latched until SYS sends a non-ESTOP mode change
    }

    {
        reset_state();
        bool estop_pending = false;
        bool seb_takeover = false;
        auto r = run_safety_checks(5'000'000, false, UINT32_MAX,
                                   estop_pending, uint8_t(can::Mode::Estop), seb_takeover);

        CHECK(r.zero_setpoints);
        CHECK(r.brake_kpa == shared::kMaxBrakeKpa);
        CHECK(r.disable_steering);
    }

    {
        reset_state();
        int64_t hb_us = 1'000'000;
        g_last_sys_hb_us.store(hb_us);
        bool estop_pending = false;
        bool seb_takeover = false;
        int64_t timeout_us = rt::kHeartbeatTimeoutMsSys * 1000LL;
        auto r = run_safety_checks(hb_us + timeout_us + 1, false, UINT32_MAX,
                                   estop_pending, uint8_t(can::Mode::Auto), seb_takeover);

        CHECK(r.zero_setpoints);
        // Issue #3: SYS-HB loss alone does NOT set seb_takeover (emergency 0x7B9)
        // ? that is owned by the brake-fallback machine, which additionally
        // requires the SYS 0x7B9 to have disappeared.
        CHECK(!seb_takeover);

        g_last_sys_hb_us.store(hb_us + timeout_us + 1);
        r = run_safety_checks(hb_us + timeout_us + 10'000, false, UINT32_MAX,
                              estop_pending, uint8_t(can::Mode::Auto), seb_takeover);

        CHECK(!r.zero_setpoints);
        CHECK(!seb_takeover);
    }

    {
        reset_state();
        int64_t hb_us = 1'000'000;
        g_last_host_hb_us.store(hb_us);
        bool estop_pending = false;
        bool seb_takeover = false;
        int64_t host_timeout_us = shared::kHeartbeatTimeoutMsHost * 1000LL;
        auto r = run_safety_checks(hb_us + host_timeout_us + 1, false, UINT32_MAX,
                                   estop_pending, uint8_t(can::Mode::Auto), seb_takeover);

        CHECK(r.zero_setpoints);
        CHECK(g_brake_request_kpa.load() == shared::kAssistStopKpa);
    }

    {
        reset_state();
        g_bench_solo_mode = true;
        g_bypass_eps_sync = true;
        g_last_sys_hb_us.store(1);
        g_last_host_hb_us.store(1);
        bool estop_pending = false;
        bool seb_takeover = false;
        auto r = run_safety_checks(20'000'000, false, UINT32_MAX,
                                   estop_pending, uint8_t(can::Mode::Manual),
                                   seb_takeover);

        CHECK(!r.zero_setpoints);
        CHECK(!r.disable_steering);
        CHECK(!seb_takeover);

        // Developer bypass never suppresses an actual ESTOP event.
        estop_pending = true;
        r = run_safety_checks(20'010'000, false, UINT32_MAX,
                              estop_pending, uint8_t(can::Mode::Manual),
                              seb_takeover);
        CHECK(r.zero_setpoints);
        CHECK(r.brake_kpa == shared::kMaxBrakeKpa);
        CHECK(r.disable_steering);
    }

    {
        reset_state();
        boot_steering_to_active();
        g_mtr_applied_speed_command_mmps.store(6944); // threshold floor: 2.0 deg = 20 in 0.1 deg
        g_last_cmd_angle_0_1deg.store(21);
        g_ses_angle_0_1deg.store(0);

        bool estop_pending = false;
        bool seb_takeover = false;
        rt::SafetyResult r{};
        constexpr int kTicks = rt::kSteerFollowingErrMs / (1000 / rt::kControlLoopHz);
        for (int i = 0; i < kTicks; ++i) {
            r = run_safety_checks(5'000'000 + i * 10'000, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
        }

        CHECK(r.zero_setpoints);
        CHECK(r.brake_kpa == shared::kMaxBrakeKpa);
        CHECK(r.disable_steering);
    }

    {
        reset_state();
        boot_steering_to_active();
        g_mtr_applied_speed_command_mmps.store(6944);
        g_last_cmd_angle_0_1deg.store(20);
        g_ses_angle_0_1deg.store(0);

        bool estop_pending = false;
        bool seb_takeover = false;
        rt::SafetyResult r{};
        for (int i = 0; i < 40; ++i) {
            r = run_safety_checks(6'000'000 + i * 10'000, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
        }

        CHECK(!r.zero_setpoints);
        CHECK(r.brake_kpa == 0);
        CHECK(!r.disable_steering);
    }

    // ?? Issue #8: MTR feedback health (MTR present, not bypassed) ?????
    {
        reset_state();
        g_bypass_mtr_absent = false;   // MTR is present ? exercise the health check
        bool estop_pending = false;
        bool seb_takeover = false;
        rt::SafetyResult r{};

        // Healthy MTR in AUTO: fresh 0x206 every 100 ms, no trip.
        int64_t now = 1'000'000;
        g_last_mtr_feedback_us.store(now);
        for (int i = 0; i < 10; ++i) {
            now += 100'000;   // 100 ms
            g_last_mtr_feedback_us.store(now);
            r = run_safety_checks(now, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
            CHECK(!r.zero_setpoints);
        }
        CHECK(!rt::g_mtr_health.mtr_unavailable);

        // MTR feedback goes stale past the AUTO-entry acquisition grace.
        // Drive command was NOT recently non-zero (at standstill): propulsion
        // must still be prohibited (actuator-health is decoupled from command),
        // but without max brake.
        for (int i = 0; i < 40; ++i) {
            now += 100'000;   // keep advancing, do NOT refresh MTR feedback
            r = run_safety_checks(now, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
        }
        CHECK(rt::g_mtr_health.mtr_unavailable);
        CHECK(r.zero_setpoints);
        CHECK(r.brake_kpa == 0);   // no recent non-zero command -> prohibition, not max brake

        // Confirmed recovery: kMtrFbkRecoverFrames consecutive fresh 0x206 frames.
        bool recovered = false;
        for (int i = 0; i < rt::kMtrFbkRecoverFrames; ++i) {
            now += 100'000;
            g_last_mtr_feedback_us.store(now);
            r = run_safety_checks(now, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
            if (!rt::g_mtr_health.mtr_unavailable) recovered = true;
        }
        CHECK(recovered);
        CHECK(!r.zero_setpoints);
    }

    // ?? Issue #8: brake escalation when motion had recently been commanded ??
    {
        reset_state();
        g_bypass_mtr_absent = false;
        bool estop_pending = false;
        bool seb_takeover = false;
        rt::SafetyResult r{};

        int64_t now = 1'000'000;
        g_last_mtr_feedback_us.store(now);
        for (int i = 0; i < 5; ++i) {
            now += 100'000;
            g_last_mtr_feedback_us.store(now);
            g_last_nonzero_cmd_us.store(now);   // continuously commanded to move
            r = run_safety_checks(now, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
        }
        // Now MTR feedback stops; the last non-zero command was ~100 ms ago (still
        // inside the 500 ms motion window) -> brake escalation to max.
        for (int i = 0; i < 3; ++i) {
            now += 100'000;   // 100..300 ms since the last non-zero command
            r = run_safety_checks(now, false, UINT32_MAX,
                                  estop_pending, uint8_t(can::Mode::Auto), seb_takeover);
        }
        CHECK(rt::g_mtr_health.mtr_unavailable);
        CHECK(r.zero_setpoints);
        CHECK(r.brake_kpa == shared::kMaxBrakeKpa);   // motion window still active
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
