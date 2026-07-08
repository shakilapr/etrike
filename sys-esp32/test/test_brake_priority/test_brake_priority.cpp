#include <unity.h>
#include <cstdint>
#include "shared_config.h"
#include "can/can_protocol.h"
#include "brake_control.h"

using namespace sys;
using namespace can;

static uint8_t seb_ok[8] = {0x01, 0, 0x58, 0x02, 0, 0, 0, 0};
static constexpr uint8_t kNoStatus = 0xFF;
static constexpr uint16_t kDefaultStroke = 600;

static inline uint16_t seb_stroke(const uint8_t* d) { return d[2] | (uint16_t(d[3]) << 8); }

static void bootstrap_active(BrakeControl& bc) {
    VcuSebReq unused;
    for (int i = 0; i < 110; ++i) {
        bc.tick(false, false, 0, Mode::Manual, kNoStatus, kDefaultStroke, unused);
    }
    VcuSebReq dummy;
    bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), dummy);
}

void setUp(void) {}
void tearDown(void) {}

void test_brake_priority_ordering(void) {
    BrakeControl bc;
    bc.init();
    bootstrap_active(bc);

    VcuSebReq out;

    bc.tick(false, true, 5000, Mode::Estop, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(1140, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_req);

    bc.tick(true, false, 2000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(900, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_req);

    bc.tick(false, false, 2000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(1, out.control_mode);
    TEST_ASSERT_TRUE(out.pressure_req > 0);
    TEST_ASSERT_EQUAL(1, out.auto_brake);
    TEST_ASSERT_EQUAL(600, out.stroke_req);

    bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(600, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(true, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.control_mode);
    TEST_ASSERT_EQUAL(900, out.stroke_req);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
}

void test_brake_auto_brake_bit(void) {
    BrakeControl bc;
    bc.init();
    bootstrap_active(bc);
    VcuSebReq out;

    bc.tick(false, false, 4000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(1, out.auto_brake);

    bc.tick(true, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(true, false, 2000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.auto_brake);

    bc.tick(false, true, 0, Mode::Estop, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
}

void test_brake_kpa_to_seb_raw_conversion(void) {
    BrakeControl bc;
    bc.init();
    bootstrap_active(bc);
    VcuSebReq out;

    bc.tick(false, false, 5000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(100, out.pressure_req);

    bc.tick(false, false, 2500, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(50, out.pressure_req);

    bc.tick(false, false, 100, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(2, out.pressure_req);

    bc.tick(false, false, 25000, Mode::Auto, seb_ok[0], seb_stroke(seb_ok), out);
    TEST_ASSERT_EQUAL(100, out.pressure_req);
}

void test_brake_rolling_counter(void) {
    BrakeControl bc;
    bc.init();
    bootstrap_active(bc);
    VcuSebReq out;

    bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
    uint8_t prev = out.rolling_counter;
    for (int i = 0; i < 20; ++i) {
        bc.tick(false, false, 0, Mode::Manual, seb_ok[0], seb_stroke(seb_ok), out);
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
