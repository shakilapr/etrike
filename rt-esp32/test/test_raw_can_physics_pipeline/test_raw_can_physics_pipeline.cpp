#include <unity.h>
#include <cstdint>
#include <cstring>

#include "stub/stm32g4xx_hal.h"     // MTR HAL mock
#include "protocol/compat/can.hpp"
#include "physics_model.h"          // Real RT physics resolver
#include "motor_manager.h"          // Real MTR motor manager
#include "shared_config.h"

using namespace rt;
using namespace mtr;

// ── Authority helpers for MTR state machine ───────────────────────
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

// Struct to hold complete result of raw CAN frame injection pipeline test
struct PipelineRawResult {
    // RT Physics outputs
    int32_t resolved_motor_speed_mmps;
    int32_t resolved_steer_angle_mdeg;
    bool steer_valid;
    bool steer_saturated;
    bool reversing;

    // Encoded low-level CAN 0x204 raw bytes
    uint8_t low_can_bytes[5];

    // MTR Actuator outputs
    uint16_t dac_code;
    float dac_volts;
    bool drive_relay;
    bool rev_relay;
};

// Process raw CAN bytes through RT physics and MTR actuator
static PipelineRawResult test_raw_can_frame(const uint8_t high_raw_bytes[8]) {
    PipelineRawResult res{};

    // 1. Construct raw High CAN Frame (0x300 HOST_DRIVE_CMD)
    can::Frame high_frame{};
    high_frame.id = can::kIdHostDriveCmd;
    high_frame.dlc = 8;
    high_frame.extended = false;
    std::memcpy(high_frame.data.data(), high_raw_bytes, 8);

    // Decode raw CAN frame
    can::gen::HostDriveCmd host_cmd{};
    TEST_ASSERT_TRUE(etrike::protocol::succeeded(
        can::gen::decode_host_drive_cmd(high_frame.view(), host_cmd)));

    // 2. Pass decoded values to REAL RT PhysicsModel
    PhysicsModel model;
    DriveCmd drive_cmd{host_cmd.speed_mmps, host_cmd.yaw_rate_mrad_s};
    ResolvedSetpoint sp{};
    TEST_ASSERT_TRUE(model.resolve(drive_cmd, sp));

    res.resolved_motor_speed_mmps = sp.motor_speed_mmps;
    res.resolved_steer_angle_mdeg = sp.steer_angle_mdeg;
    res.steer_valid = sp.steer_valid;
    res.steer_saturated = sp.steer_saturated;
    res.reversing = sp.reversing;

    // Derive CAN gear from speed sign
    can::Gear gear = sp.motor_speed_mmps > 0 ? can::Gear::D
                   : sp.motor_speed_mmps < 0 ? can::Gear::R
                   : can::Gear::N;

    // 3. Encode setpoint into low-level RT_DRIVE_CMD (0x204) raw CAN frame
    can::gen::RtDriveCmd rt_cmd{};
    rt_cmd.motor_speed_mmps = sp.motor_speed_mmps;
    rt_cmd.gear = static_cast<uint8_t>(gear);

    can::Frame low_frame{};
    TEST_ASSERT_TRUE(etrike::protocol::succeeded(
        can::gen::encode_rt_drive_cmd(rt_cmd, low_frame)));
    TEST_ASSERT_EQUAL(0x204, low_frame.id);
    TEST_ASSERT_EQUAL(5, low_frame.dlc);
    std::memcpy(res.low_can_bytes, low_frame.data.data(), 5);

    // 4. Feed low CAN frame into REAL MTR MotorManager
    hal_mock::reset();
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();

    uint32_t now = 100;
    mtr_send_mode(mgr, can::Mode::Manual, now);
    mtr_send_power(mgr, true, now);
    mtr_send_safety(mgr, false, now);

    // Feed 0x204 low CAN frame
    mgr.handle_frame(low_frame, now);
    mgr.tick(now);

    if (gear == can::Gear::R) {
        // Complete 50 ms direction shift dwell
        now += 60;
        mgr.tick(now);
    }

    res.dac_code = dac.current_code();
    res.dac_volts = static_cast<float>(res.dac_code) / 4095.0f * 5.0f;
    res.drive_relay = (relays.state() == RelayController::State::Drive);
    res.rev_relay = (relays.state() == RelayController::State::Reverse);

    return res;
}

void setUp(void) {}
void tearDown(void) {}

void test_raw_can_forward_2000_mmps_turning() {
    // Raw CAN 0x300 payload for speed=2000 mm/s (0x000007D0), yaw_rate=100 mrad/s (0x000064), gear=1 (D)
    const uint8_t raw_bytes[8] = { 0x00, 0x00, 0x07, 0xD0, 0x00, 0x00, 0x64, 0x01 };

    PipelineRawResult res = test_raw_can_frame(raw_bytes);

    // Verify RT Physics Kinematics
    TEST_ASSERT_EQUAL(2000, res.resolved_motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 4288, res.resolved_steer_angle_mdeg); // atan(1.5*0.1/2.0) ≈ 4.29° = 4288 mdeg
    TEST_ASSERT_TRUE(res.steer_valid);
    TEST_ASSERT_FALSE(res.steer_saturated);
    TEST_ASSERT_FALSE(res.reversing);

    // Verify Low CAN Frame Raw Bytes (0x204)
    // 2000 mm/s = 0x000007D0, gear=1
    TEST_ASSERT_EQUAL(0x00, res.low_can_bytes[0]);
    TEST_ASSERT_EQUAL(0x00, res.low_can_bytes[1]);
    TEST_ASSERT_EQUAL(0x07, res.low_can_bytes[2]);
    TEST_ASSERT_EQUAL(0xD0, res.low_can_bytes[3]);
    TEST_ASSERT_EQUAL(0x01, res.low_can_bytes[4]);

    // Verify MTR Hardware Execution
    TEST_ASSERT_EQUAL(1544, res.dac_code); // 700 + (2000/3000)*1266 = 1544
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.885f, res.dac_volts);
    TEST_ASSERT_TRUE(res.drive_relay);
    TEST_ASSERT_FALSE(res.rev_relay);
}

void test_raw_can_reverse_300_mmps_turning() {
    // Raw CAN 0x300 payload for speed=-300 mm/s (0xFFFFFED4), yaw_rate=50 mrad/s (0x000032), gear=3 (R)
    const uint8_t raw_bytes[8] = { 0xFF, 0xFF, 0xFE, 0xD4, 0x00, 0x00, 0x32, 0x03 };

    PipelineRawResult res = test_raw_can_frame(raw_bytes);

    // Verify RT Physics Kinematics (reverse steering sign preserves negative angle)
    TEST_ASSERT_EQUAL(-300, res.resolved_motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, -14036, res.resolved_steer_angle_mdeg); // atan(1.5*0.05/-0.3) ≈ -14.036°
    TEST_ASSERT_TRUE(res.steer_valid);
    TEST_ASSERT_TRUE(res.reversing);

    // Verify Low CAN Frame Raw Bytes (0x204)
    // -300 mm/s = 0xFFFFFFD4, gear=3
    TEST_ASSERT_EQUAL(0xFF, res.low_can_bytes[0]);
    TEST_ASSERT_EQUAL(0xFF, res.low_can_bytes[1]);
    TEST_ASSERT_EQUAL(0xFE, res.low_can_bytes[2]);
    TEST_ASSERT_EQUAL(0xD4, res.low_can_bytes[3]);
    TEST_ASSERT_EQUAL(0x03, res.low_can_bytes[4]);

    // Verify MTR Hardware Execution (Reverse mode scaling and relay state)
    TEST_ASSERT_EQUAL(1459, res.dac_code); // 700 + (300/500)*1266 = 1459
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.781f, res.dac_volts);
    TEST_ASSERT_TRUE(res.rev_relay);
}

void test_raw_can_max_forward_3000_mmps() {
    // Raw CAN 0x300 payload for speed=3000 mm/s (0x00000BB8), yaw_rate=0 mrad/s, gear=1 (D)
    const uint8_t raw_bytes[8] = { 0x00, 0x00, 0x0B, 0xB8, 0x00, 0x00, 0x00, 0x01 };

    PipelineRawResult res = test_raw_can_frame(raw_bytes);

    TEST_ASSERT_EQUAL(3000, res.resolved_motor_speed_mmps);
    TEST_ASSERT_EQUAL(0, res.resolved_steer_angle_mdeg);
    TEST_ASSERT_EQUAL(1966, res.dac_code); // Max DAC code limit
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.401f, res.dac_volts);
    TEST_ASSERT_TRUE(res.drive_relay);
}

void test_raw_can_standstill_pure_yaw_no_lurch() {
    // Raw CAN 0x300 payload for speed=0 mm/s, yaw_rate=500 mrad/s (0x0001F4), gear=0 (N)
    const uint8_t raw_bytes[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xF4, 0x00 };

    PipelineRawResult res = test_raw_can_frame(raw_bytes);

    // Standstill pure yaw pre-aligns steering to full lock 40000 mdeg, but motor speed stays 0
    TEST_ASSERT_EQUAL(0, res.resolved_motor_speed_mmps);
    TEST_ASSERT_EQUAL(40000, res.resolved_steer_angle_mdeg);
    TEST_ASSERT_TRUE(res.steer_valid);

    // MTR DAC output stays 0 (neutral / zero torque)
    TEST_ASSERT_EQUAL(0, res.dac_code);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.000f, res.dac_volts);
    TEST_ASSERT_FALSE(res.drive_relay);
    TEST_ASSERT_FALSE(res.rev_relay);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_raw_can_forward_2000_mmps_turning);
    RUN_TEST(test_raw_can_reverse_300_mmps_turning);
    RUN_TEST(test_raw_can_max_forward_3000_mmps);
    RUN_TEST(test_raw_can_standstill_pure_yaw_no_lurch);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
