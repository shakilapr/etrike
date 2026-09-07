#include <unity.h>
#include <cstdint>
#include "stub/stm32g4xx_hal.h"   // MTR HAL mock (defines hal_mock) — must precede MTR headers
#include "protocol/compat/can.hpp"
#include "physics_model.h"          // rt::PhysicsModel (real RT resolver)
#include "motor_manager.h"          // mtr::MotorManager (real MTR actuator)
#include "shared_config.h"

using namespace rt;
using namespace mtr;

// ── MTR authority helpers (mirror mtr-stm32/test/test_mtr_full_suite.cpp) ──
static uint8_t g_mode_ctr = 0;
static uint8_t g_pwr_ctr = 0;
static uint8_t g_safe_ctr = 0;

static void mtr_send_mode(MotorManager& mgr, can::Mode mode, uint32_t now) {
    bool auto_mode = (mode == can::Mode::Auto);
    can::gen::SysModeCmd c0{auto_mode, g_mode_ctr++};
    can::Frame f0; can::gen::encode_sys_mode_cmd(c0, f0); mgr.handle_frame(f0, now);
    can::gen::SysModeCmd c1{auto_mode, g_mode_ctr++};
    can::Frame f1; can::gen::encode_sys_mode_cmd(c1, f1); mgr.handle_frame(f1, now);
}
static void mtr_send_power(MotorManager& mgr, bool on, uint32_t now) {
    can::gen::SysPwrCmd c0{on, g_pwr_ctr++};
    can::Frame f0; can::gen::encode_sys_pwr_cmd(c0, f0); mgr.handle_frame(f0, now);
    can::gen::SysPwrCmd c1{on, g_pwr_ctr++};
    can::Frame f1; can::gen::encode_sys_pwr_cmd(c1, f1); mgr.handle_frame(f1, now);
}
static void mtr_send_safety(MotorManager& mgr, bool estop, uint32_t now) {
    for (int i = 0; i < 2; ++i) {
        can::gen::SysSafetySts msg{};
        msg.estop_active = estop;
        msg.heartbeat_ok = true;
        msg.rolling_counter = g_safe_ctr++;
        can::Frame tmp; can::gen::encode_sys_safety_sts(msg, tmp);
        msg.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
        can::Frame f; can::gen::encode_sys_safety_sts(msg, f);
        mgr.handle_frame(f, now);
    }
}
static void mtr_drive(MotorManager& mgr, int32_t speed, can::Gear gear, uint32_t now) {
    can::gen::RtDriveCmd cmd{speed, static_cast<uint8_t>(gear)};
    can::Frame f; can::gen::encode_rt_drive_cmd(cmd, f);
    mgr.handle_frame(f, now);
    mgr.tick(now);
}

// Feed a high-level HOST_DRIVE_CMD (0x300) through the REAL RT resolver and the
// REAL MTR actuator, and assert the resulting low-level DAC code (tuktuk math
// across both stages, using shared::kWheelbaseMM = 1.5 m on the RT side).
static uint16_t pipeline_dac(int32_t speed_mmps, int32_t yaw_mrad_s) {
    // 1. High-level command 0x300
    can::gen::HostDriveCmd host{};
    host.speed_mmps = speed_mmps;
    host.yaw_rate_mrad_s = yaw_mrad_s;
    host.gear = 0;
    can::Frame f300;
    TEST_ASSERT_TRUE(etrike::protocol::succeeded(can::gen::encode_host_drive_cmd(host, f300)));

    can::gen::HostDriveCmd decoded{};
    TEST_ASSERT_TRUE(etrike::protocol::succeeded(can::gen::decode_host_drive_cmd(f300.view(), decoded)));

    // 2. REAL RT resolver -> RT_DRIVE_CMD (0x204) setpoint
    PhysicsModel model;
    DriveCmd cmd{decoded.speed_mmps, decoded.yaw_rate_mrad_s};
    ResolvedSetpoint sp{};
    TEST_ASSERT_TRUE(model.resolve(cmd, sp));

    // RT-ESP32 derives gear from speed sign (mirrors t_can_tx_low)
    can::Gear gear = sp.motor_speed_mmps > 0 ? can::Gear::D
                   : sp.motor_speed_mmps < 0 ? can::Gear::R
                   : can::Gear::N;

    // 3. REAL MTR actuator: 0x204 -> DAC code
    hal_mock::reset();
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();
    mtr_send_mode(mgr, can::Mode::Manual, 100);
    mtr_send_power(mgr, true, 100);
    mtr_send_safety(mgr, false, 100);
    mtr_drive(mgr, sp.motor_speed_mmps, gear, 100);
    if (gear == can::Gear::R) {
        mgr.tick(160);  // complete 50 ms direction-shift dwell
    }
    return dac.current_code();
}

void setUp(void) {}
void tearDown(void) {}

void test_pipeline_forward_1500() {
    // 1.5 m/s forward -> RT passes 1500 mm/s -> MTR DAC midpoint 1333
    TEST_ASSERT_EQUAL(1333, pipeline_dac(1500, 0));
}

void test_pipeline_forward_2000() {
    // 2.0 m/s forward -> norm 2000/3000 = 0.6667 -> 700 + 0.6667*1266 = 1544
    TEST_ASSERT_EQUAL(1544, pipeline_dac(2000, 0));
}

void test_pipeline_forward_max() {
    // 3.0 m/s forward -> max DAC 1966 (2.404 V)
    TEST_ASSERT_EQUAL(1966, pipeline_dac(3000, 0));
}

void test_pipeline_reverse_300() {
    // -0.3 m/s reverse -> |speed| 300/500 = 0.6 -> 700 + 0.6*1266 = 1459.6 -> 1459 (truncated)
    TEST_ASSERT_EQUAL(1459, pipeline_dac(-300, 0));
}

void test_pipeline_reverse_max() {
    // -0.5 m/s reverse -> max DAC 1966
    TEST_ASSERT_EQUAL(1966, pipeline_dac(-500, 0));
}

void test_pipeline_voltage_check() {
    // Max DAC code must equal the documented 2.4 V safety limit (VCC = 5.0 V)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.404f,
        static_cast<float>(kDacMaxCode) / 4095.0f * 5.0f);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_pipeline_forward_1500);
    RUN_TEST(test_pipeline_forward_2000);
    RUN_TEST(test_pipeline_forward_max);
    RUN_TEST(test_pipeline_reverse_300);
    RUN_TEST(test_pipeline_reverse_max);
    RUN_TEST(test_pipeline_voltage_check);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
