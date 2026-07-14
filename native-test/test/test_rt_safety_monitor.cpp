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

QueueHandle_t g_safety_evt_q = nullptr;

std::atomic<int32_t> g_brake_request_kpa{0};
std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
std::atomic<int32_t> g_ses_angle_0_1deg{INT16_MIN};
std::atomic<uint8_t> g_ses_angle_status{0};
std::atomic<int32_t> g_brake_kpa_to_send{0};
std::atomic<int32_t> g_mtr_actual_speed_mmps{0};
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
    g_brake_request_kpa.store(0);
    g_obstacle_mm.store(UINT32_MAX);
    g_ses_angle_0_1deg.store(INT16_MIN);
    g_ses_angle_status.store(0);
    g_brake_kpa_to_send.store(0);
    g_mtr_actual_speed_mmps.store(0);
    g_mode_current.store(0);
    g_seb_takeover.store(false);
    g_last_sys_hb_us.store(0);
    g_last_host_hb_us.store(0);
    g_last_estop_sent_us.store(0);
    g_last_cmd_angle_0_1deg.store(0);
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
        CHECK(seb_takeover);

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
        boot_steering_to_active();
        g_mtr_actual_speed_mmps.store(6944); // threshold floor: 2.0 deg = 20 in 0.1 deg
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
        g_mtr_actual_speed_mmps.store(6944);
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

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
