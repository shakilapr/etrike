#include <atomic>
#include <cstdio>
#include <cstdint>

#include "esp_timer.h"
#include "can/can_protocol.h"
#include "rt_state.h"

static std::atomic<uint32_t> g_alive_dispatch{0};

#include "can_dispatch.h"

rt::Mcp2515Driver g_can_high;
rt::PhysicsModel g_physics;
rt::SpeedController g_speed_ctrl;
rt::SteeringControl g_steering;
rt::DualHeartbeat g_heartbeat;
rt::CmdWatchdog g_watchdog;

QueueHandle_t g_safety_evt_q = nullptr;
std::atomic<bool> g_pending_estop_event{false};
std::atomic<int16_t> g_pending_mode_event{-1};
std::atomic<uint32_t> g_safety_event_drops{0};

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
std::atomic<uint8_t> g_estop_reason{0};
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
    g_last_sys_hb_us.store(0);
    g_last_host_hb_us.store(0);
    esp_timer_test_reset();
}

int main() {
    std::printf("\n=== RT CAN Dispatch ===\n\n");

    {
        reset_state();
        can::Frame fr{};
        fr.id = can::kIdSysHeartbeat;
        fr.dlc = 1;
        fr.put_u8(0, 1);

        DispatchContext ctx{};
        esp_timer_test_advance(1000);
        process_frame(fr, true, ctx);
        CHECK(g_last_sys_hb_us.load() == 0);

        esp_timer_test_advance(1000);
        process_frame(fr, false, ctx);
        CHECK(g_last_sys_hb_us.load() == 2000);
    }

    {
        reset_state();
        can::Frame fr{};
        fr.id = can::kIdHostHeartbeat;
        fr.dlc = 1;
        fr.put_u8(0, 1);

        DispatchContext ctx{};
        esp_timer_test_advance(1000);
        process_frame(fr, false, ctx);
        CHECK(g_last_host_hb_us.load() == 0);

        esp_timer_test_advance(1000);
        process_frame(fr, true, ctx);
        CHECK(g_last_host_hb_us.load() == 2000);
    }

    {
        can::Frame fr{};
        can::HostDriveCmd{500, 100, uint8_t(can::Gear::D)}.to_frame(fr);

        DispatchContext low_ctx{};
        process_frame(fr, false, low_ctx);
        CHECK(!low_ctx.has_cmd);

        DispatchContext high_ctx{};
        process_frame(fr, true, high_ctx);
        CHECK(high_ctx.has_cmd);
        CHECK(high_ctx.cmd.speed_mmps == 500);
    }

    {
        can::Frame fr{};
        can::SysModeCmd{uint8_t(can::Mode::Auto)}.to_frame(fr);

        DispatchContext high_ctx{};
        process_frame(fr, true, high_ctx);
        CHECK(!high_ctx.has_mode);

        DispatchContext low_ctx{};
        process_frame(fr, false, low_ctx);
        CHECK(low_ctx.has_mode);
        CHECK(low_ctx.mode_from_sys == uint8_t(can::Mode::Auto));
    }

    {
        can::Frame fr{};
        can::HostBrakeReq{1234}.to_frame(fr);

        DispatchContext low_ctx{};
        process_frame(fr, false, low_ctx);
        CHECK(!low_ctx.has_brake);

        DispatchContext high_ctx{};
        process_frame(fr, true, high_ctx);
        CHECK(high_ctx.has_brake);
        CHECK(high_ctx.brake_req_kpa == 1234);
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
