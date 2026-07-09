#include <unity.h>
#include <cstdint>
#include "shared_config.h"
#include "can/can_protocol.h"
#include "brake_control.h"

using namespace sys;
using namespace can;

static constexpr uint8_t kNoStatus = 0xFF;
static constexpr uint16_t kDefaultStroke = 600;

static uint8_t pack_seb_status_b0(const can::SebStatus& s) {
    return (s.alignment_status & 1) |
           ((s.control_enable_sts & 1) << 1) |
           ((s.control_mode_sts & 3) << 2) |
           ((s.auto_brake_sts & 1) << 4) |
           ((s.reserved_0 & 1) << 5) |
           ((s.error_status & 3) << 6);
}

static constexpr uint16_t kExpectedEstopStroke = uint16_t((sys::kBrakeMaxStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
static constexpr uint16_t kExpectedManualStroke = uint16_t((sys::kBrakeManualStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);

static void bootstrap_active(BrakeControl& bc, uint8_t status_b0, uint16_t stroke) {
    VcuSebReq unused;
    for (int i = 0; i < 110; ++i) {
        bc.tick(false, false, 0, Mode::Manual, kNoStatus, kDefaultStroke, unused);
    }
    VcuSebReq dummy;
    bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, dummy);
}

void setUp(void) {}
void tearDown(void) {}

void test_brake_priority_ordering(void) {
    can::SebStatus seb_ok{};
    seb_ok.alignment_status = 1;
    seb_ok.stroke_value = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);

    VcuSebReq out;

    bc.tick(false, true, 5000, Mode::Estop, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(kExpectedEstopStroke, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_req);

    bc.tick(true, false, 2000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(kExpectedManualStroke, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_req);

    bc.tick(false, false, 2000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(1, out.control_mode);
    TEST_ASSERT_TRUE(out.pressure_req > 0);
    TEST_ASSERT_EQUAL(1, out.auto_brake);
    TEST_ASSERT_EQUAL(600, out.stroke_req);

    bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(600, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(true, false, 0, Mode::Manual, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(kExpectedManualStroke, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
}

void test_brake_auto_brake_bit(void) {
    can::SebStatus seb_ok{};
    seb_ok.alignment_status = 1;
    seb_ok.stroke_value = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);
    VcuSebReq out;

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
    can::SebStatus seb_ok{};
    seb_ok.alignment_status = 1;
    seb_ok.stroke_value = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);
    VcuSebReq out;

    bc.tick(false, false, 5000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(100, out.pressure_req);

    bc.tick(false, false, 2500, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(50, out.pressure_req);

    bc.tick(false, false, 100, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(2, out.pressure_req);

    bc.tick(false, false, 25000, Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(100, out.pressure_req);
}

void test_brake_rolling_counter(void) {
    can::SebStatus seb_ok{};
    seb_ok.alignment_status = 1;
    seb_ok.stroke_value = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = seb_ok.stroke_value;

    BrakeControl bc;
    bc.init();
    bootstrap_active(bc, status_b0, stroke);
    VcuSebReq out;

    bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, out);
    uint8_t prev = out.rolling_counter;
    for (int i = 0; i < 20; ++i) {
        bc.tick(false, false, 0, Mode::Manual, status_b0, stroke, out);
        TEST_ASSERT_EQUAL((prev + 1) & 0x0F, out.rolling_counter);
        prev = out.rolling_counter;
    }
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_brake_priority_ordering);
    RUN_TEST(test_brake_auto_brake_bit);
    RUN_TEST(test_brake_kpa_to_seb_raw_conversion);
    RUN_TEST(test_brake_rolling_counter);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
