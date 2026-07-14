#include <unity.h>
#include <cstdint>
#include "shared_config.h"
#include "protocol/compat/can.hpp"
#include "brake_control.h"

using namespace sys;
using namespace can;

static constexpr uint8_t kNoStatus = 0xFF;
static constexpr uint16_t kDefaultStroke = 600;

static uint8_t pack_seb_status_b0(const can::custom::seb::Status& status) {
    return (status.alignment_status ? 1u : 0u) |
           ((status.control_enabled ? 1u : 0u) << 1) |
           ((status.control_mode & 3u) << 2) |
           ((status.auto_brake_status ? 1u : 0u) << 4) |
           ((status.error_status & 3u) << 6);
}

static constexpr uint16_t kExpectedEstopStroke = uint16_t((sys::kBrakeMaxStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
static constexpr uint16_t kExpectedManualStroke = uint16_t((sys::kBrakeManualStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);

static void bootstrap_active(BrakeControl& bc, uint8_t status_b0, uint16_t stroke) {
    can::custom::seb::Command unused;
    for (int i = 0; i < 110; ++i) {
        bc.tick(false, false, 0, Mode::Manual, kNoStatus, kDefaultStroke, unused);
    }
    can::custom::seb::Command dummy;
    bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, dummy);
}

void setUp(void) {}
void tearDown(void) {}

void test_brake_priority_ordering(void) {
    can::custom::seb::Status seb_ok{};
    seb_ok.alignment_status = true;
    seb_ok.stroke_value_raw = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value_raw;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);

    can::custom::seb::Command out;

    bc.tick(false, true, 5000, Mode::Estop, status_b0, stroke, out);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::custom::seb::ControlMode::Stroke), uint8_t(out.control_mode));
    TEST_ASSERT_EQUAL(kExpectedEstopStroke, out.stroke_request_raw);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_request_raw);

    bc.tick(true, false, 2000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::custom::seb::ControlMode::Stroke), uint8_t(out.control_mode));
    TEST_ASSERT_EQUAL(kExpectedManualStroke, out.stroke_request_raw);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_request_raw);

    bc.tick(false, false, 2000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::custom::seb::ControlMode::Pressure), uint8_t(out.control_mode));
    TEST_ASSERT_TRUE(out.pressure_request_raw > 0);
    TEST_ASSERT_EQUAL(1, out.auto_brake);
    TEST_ASSERT_EQUAL(600, out.stroke_request_raw);

    bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, out);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::custom::seb::ControlMode::Stroke), uint8_t(out.control_mode));
    TEST_ASSERT_EQUAL(600, out.stroke_request_raw);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(true, false, 0, Mode::Manual, status_b0, stroke, out);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::custom::seb::ControlMode::Stroke), uint8_t(out.control_mode));
    TEST_ASSERT_EQUAL(kExpectedManualStroke, out.stroke_request_raw);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
}

void test_brake_auto_brake_bit(void) {
    can::custom::seb::Status seb_ok{};
    seb_ok.alignment_status = true;
    seb_ok.stroke_value_raw = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value_raw;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);
    can::custom::seb::Command out;

    bc.tick(false, false, 4000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(1, out.auto_brake);

    bc.tick(true, false, 0, Mode::Manual, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(true, false, 2000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(false, true, 0, Mode::Estop, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
}

void test_brake_kpa_to_seb_raw_conversion(void) {
    can::custom::seb::Status seb_ok{};
    seb_ok.alignment_status = true;
    seb_ok.stroke_value_raw = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value_raw;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);
    can::custom::seb::Command out;

    bc.tick(false, false, 5000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(100, out.pressure_request_raw);

    bc.tick(false, false, 2500, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(50, out.pressure_request_raw);

    bc.tick(false, false, 100, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(2, out.pressure_request_raw);

    bc.tick(false, false, 25000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(100, out.pressure_request_raw);
}

void test_brake_rolling_counter(void) {
    can::custom::seb::Status seb_ok{};
    seb_ok.alignment_status = true;
    seb_ok.stroke_value_raw = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value_raw;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);
    can::custom::seb::Command out;

    bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, out);
    uint8_t prev = out.rolling_counter;
    for (int i = 0; i < 20; ++i) {
        bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, out);
        TEST_ASSERT_EQUAL((prev + 1) & 0x0F, out.rolling_counter);
        prev = out.rolling_counter;
    }
}

void test_brake_pressure_command_exact_bytes(void) {
    can::custom::seb::Command command{};
    command.alignment_enable = true;
    command.control_enable = true;
    command.control_mode = can::custom::seb::ControlMode::Pressure;
    command.auto_brake = true;
    command.stroke_request_raw = 600;
    command.pressure_request_raw = 40;
    command.rolling_counter = 10;

    can::Frame frame;
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::gen::CodecStatus::Ok),
                            uint8_t(can::custom::seb::encode_command(command, frame)));
    const uint8_t expected[] = {0x0F, 0x00, 0x58, 0x28, 0x00, 0x00, 0xA3, 0x23};
    TEST_ASSERT_EQUAL_HEX32(can::kIdVcuSebReq, frame.id);
    TEST_ASSERT_EQUAL_UINT8(8, frame.dlc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data.data(), sizeof(expected));
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_brake_priority_ordering);
    RUN_TEST(test_brake_auto_brake_bit);
    RUN_TEST(test_brake_kpa_to_seb_raw_conversion);
    RUN_TEST(test_brake_rolling_counter);
    RUN_TEST(test_brake_pressure_command_exact_bytes);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
