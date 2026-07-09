#include <unity.h>
#include <cstdint>
#include "can/can_protocol.h"
#include "heartbeat.h"

using namespace rt;

void setUp(void) {}
void tearDown(void) {}

void test_heartbeat_independent_counters_diverge(void) {
    DualHeartbeat hb;
    hb.init();
    can::Frame f_low, f_high;

    TEST_ASSERT_EQUAL(0, hb.ctr_low());
    TEST_ASSERT_EQUAL(0, hb.ctr_high());

    hb.tick_low(f_low);
    hb.tick_low(f_low);
    hb.tick_low(f_low);
    hb.tick_high(f_high);

    TEST_ASSERT_EQUAL(3, hb.ctr_low());
    TEST_ASSERT_EQUAL(1, hb.ctr_high());
}

void test_heartbeat_both_counters_increment(void) {
    DualHeartbeat hb;
    hb.init();
    can::Frame f;

    for (int i = 0; i < 10; ++i) hb.tick_low(f);
    for (int i = 0; i < 5;  ++i) hb.tick_high(f);

    TEST_ASSERT_EQUAL(10, hb.ctr_low());
    TEST_ASSERT_EQUAL(5, hb.ctr_high());
}

void test_heartbeat_counter_wrap(void) {
    DualHeartbeat hb;
    hb.init();
    can::Frame f;

    for (int i = 0; i < 255; ++i) hb.tick_low(f);
    TEST_ASSERT_EQUAL(255, hb.ctr_low());
    
    hb.tick_low(f);
    TEST_ASSERT_EQUAL(0, hb.ctr_low());
    TEST_ASSERT_EQUAL(0, hb.ctr_high());
}

void test_heartbeat_frame_encoding(void) {
    DualHeartbeat hb;
    hb.init();
    can::Frame f;

    hb.tick_low(f);
    TEST_ASSERT_EQUAL(can::kIdRtHeartbeatLow, f.id);
    TEST_ASSERT_EQUAL(2, f.dlc);
    TEST_ASSERT_EQUAL(1, f.u8_at(0));
    TEST_ASSERT_EQUAL(0, f.u8_at(1));

    hb.tick_high(f);
    TEST_ASSERT_EQUAL(can::kIdRtHeartbeatHigh, f.id);
    TEST_ASSERT_EQUAL(2, f.dlc);
    TEST_ASSERT_EQUAL(1, f.u8_at(0));
    TEST_ASSERT_EQUAL(0, f.u8_at(1));

    hb.tick_low(f);
    hb.tick_low(f);
    hb.tick_high(f);
    hb.tick_low(f);

    hb.tick_low(f);
    TEST_ASSERT_EQUAL(5, f.u8_at(0));

    hb.tick_high(f);
    TEST_ASSERT_EQUAL(3, f.u8_at(0));
}

void test_heartbeat_reinit_resets_counters(void) {
    DualHeartbeat hb;
    hb.init();
    can::Frame f;

    for (int i = 0; i < 100; ++i) hb.tick_low(f);
    for (int i = 0; i < 50;  ++i) hb.tick_high(f);
    TEST_ASSERT_EQUAL(100, hb.ctr_low());
    TEST_ASSERT_EQUAL(50, hb.ctr_high());

    hb.init();
    TEST_ASSERT_EQUAL(0, hb.ctr_low());
    TEST_ASSERT_EQUAL(0, hb.ctr_high());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_heartbeat_independent_counters_diverge);
    RUN_TEST(test_heartbeat_both_counters_increment);
    RUN_TEST(test_heartbeat_counter_wrap);
    RUN_TEST(test_heartbeat_frame_encoding);
    RUN_TEST(test_heartbeat_reinit_resets_counters);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
