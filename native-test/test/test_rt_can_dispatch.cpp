#include <atomic>
#include <cstdio>
#include <cstdint>

#include "esp_timer.h"
#include "protocol/compat/can_protocol.hpp"
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
std::atomic<bool> g_steering_estop_request{false};
std::atomic<bool> g_steering_exit_request{false};
std::atomic<int32_t> g_encoder_speed_mmps{0};

std::atomic<int32_t> g_brake_request_kpa{0};
std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
std::atomic<int32_t> g_ses_angle_0_1deg{INT16_MIN};
std::atomic<uint8_t> g_ses_angle_status{0};
std::atomic<int32_t> g_brake_kpa_to_send{0};
std::atomic<int32_t> g_mtr_actual_speed_mmps{0};
std::atomic<uint8_t> g_mtr_gear_state{uint8_t(can::Gear::N)};
std::atomic<int32_t> g_direct_steer_angle_0_1deg{0};
std::atomic<bool> g_direct_steer_valid{false};
std::atomic<int64_t> g_last_direct_steer_us{-1};
std::atomic<int64_t> g_last_mtr_feedback_us{-1};
std::atomic<int64_t> g_last_ses_feedback_us{-1};
std::atomic<uint8_t> g_mode_current{0};
std::atomic<bool> g_seb_takeover{false};
std::atomic<int64_t> g_last_sys_hb_us{0};
std::atomic<int64_t> g_last_host_hb_us{0};
std::atomic<int64_t> g_last_low_peer_us{0};
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
    g_last_low_peer_us.store(0);
    g_last_direct_steer_us.store(-1);
    g_last_mtr_feedback_us.store(-1);
    g_last_ses_feedback_us.store(-1);
    esp_timer_test_reset();
}

int main() {
    std::printf("\n=== RT CAN Dispatch ===\n\n");

    {
        reset_state();
        can::Frame fr{};
        fr.id = can::kIdSysHeartbeat;
        fr.dlc = 2;
        fr.data[0] = 1;
        fr.data[1] = 1;

        DispatchContext ctx{};
        esp_timer_test_advance(1000);
        process_frame(fr, true, ctx);
        CHECK(g_last_sys_hb_us.load() == 0);
        CHECK(g_last_low_peer_us.load() == 0);

        esp_timer_test_advance(1000);
        process_frame(fr, false, ctx);
        CHECK(g_last_sys_hb_us.load() == 2000);
        CHECK(g_last_low_peer_us.load() == 2000);
    }

    {
        reset_state();
        can::Frame unknown{};
        unknown.id = 0x7FF;
        unknown.dlc = 0;

        DispatchContext ctx{};
        esp_timer_test_advance(1000);
        process_frame(unknown, false, ctx);
        CHECK(g_last_low_peer_us.load() == 0);
    }

    {
        reset_state();
        can::Frame fr{};
        fr.id = can::kIdHostHeartbeat;
        fr.dlc = 2;
        fr.data[0] = 1;
        fr.data[1] = 1;

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
        can::gen::HostDriveCmd val_drive{500, 100, uint8_t(can::Gear::D)};
        can::encode_frame(val_drive, fr);
 
         DispatchContext low_ctx{};
         process_frame(fr, false, low_ctx);
         CHECK(!low_ctx.has_cmd);
 
         DispatchContext high_ctx{};
         process_frame(fr, true, high_ctx);
         CHECK(high_ctx.has_cmd);
         CHECK(high_ctx.cmd.speed_mmps == 500);
     }

    {
        reset_state();
        can::Frame fr{};
        can::gen::HostSteerCmd steer{100, true, 0, 10};
        can::encode_frame(steer, fr);

        DispatchContext ctx{};
        esp_timer_test_advance(1000);
        process_frame(fr, true, ctx);
        CHECK(g_direct_steer_angle_0_1deg.load() == 100);
        CHECK(g_direct_steer_valid.load());
        CHECK(g_last_direct_steer_us.load() == 1000);

        steer.steer_angle_0_1deg = 200;
        can::encode_frame(steer, fr);
        esp_timer_test_advance(1000);
        process_frame(fr, true, ctx);
        CHECK(g_direct_steer_angle_0_1deg.load() == 100);
        CHECK(g_last_direct_steer_us.load() == 1000);

        steer.rolling_counter = 11;
        can::encode_frame(steer, fr);
        process_frame(fr, true, ctx);
        CHECK(g_direct_steer_angle_0_1deg.load() == 200);
        CHECK(g_last_direct_steer_us.load() == 2000);
    }

    {
        reset_state();
        can::Frame fr{};
        can::gen::MtrMotorFbk feedback{-250, uint8_t(can::Gear::R), 0};
        can::encode_frame(feedback, fr);

        DispatchContext ctx{};
        esp_timer_test_advance(3000);
        process_frame(fr, false, ctx);
        CHECK(g_mtr_actual_speed_mmps.load() == -250);
        CHECK(g_mtr_gear_state.load() == uint8_t(can::Gear::R));
        CHECK(g_last_mtr_feedback_us.load() == 3000);
    }
 
     {
         can::Frame fr{};
         can::gen::SysModeCmd val_mode{uint8_t(can::Mode::Auto), 0};
         can::encode_frame(val_mode, fr);

         DispatchContext high_ctx{};
         process_frame(fr, true, high_ctx);
         CHECK(!high_ctx.has_mode);

         // RT supervises the 0x110 rolling counter + freshness: a baseline
         // frame must be followed by an advancing frame before mode authority
         // is granted (has_mode only set when the stream is valid).
         DispatchContext low_ctx_base{};
         process_frame(fr, false, low_ctx_base);
         CHECK(!low_ctx_base.has_mode);  // baseline frame not yet authoritative

         can::gen::SysModeCmd adv_mode{uint8_t(can::Mode::Auto), 1};
         can::encode_frame(adv_mode, fr);
         DispatchContext low_ctx{};
         process_frame(fr, false, low_ctx);
         CHECK(low_ctx.has_mode);
         CHECK(low_ctx.mode_from_sys == uint8_t(can::Mode::Auto));
     }

     {
         // Counter sequence fault (>2 jump) must invalidate the 0x110 stream:
         // a faulted frame must not be accepted as mode authority.
         can::Frame fr{};
         can::gen::SysModeCmd fault_mode{uint8_t(can::Mode::Manual), 5};  // jump from last ctr (1) -> fault
         can::encode_frame(fault_mode, fr);
         DispatchContext low_ctx{};
         process_frame(fr, false, low_ctx);
         CHECK(!low_ctx.has_mode);
     }
 
     {
         can::Frame fr{};
         can::gen::HostBrakeReq val_brake{1234};
         can::encode_frame(val_brake, fr);

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
