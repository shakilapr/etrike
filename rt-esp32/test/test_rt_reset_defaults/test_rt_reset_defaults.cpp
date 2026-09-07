#include <unity.h>
#include <cstdint>
#include "config.h"
#include "shared_config.h"

void setUp(void) {}
void tearDown(void) {}

void test_rt_reset_defaults(void) {
    // 1. Vehicle geometry and limits
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, shared::kWheelbaseMM);
    TEST_ASSERT_EQUAL_INT(3000, shared::kMaxSpeedFwdMmps);
    TEST_ASSERT_EQUAL_INT(500, shared::kMaxSpeedRevMmps);
    TEST_ASSERT_EQUAL_INT(50, shared::kLowSpeedThreshMmps);

    // 2. Steering defaults and safety clamps
    TEST_ASSERT_EQUAL_FLOAT(40.0f, rt::kSteerLimitDeg);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, rt::kSteerFollowingErrMinDeg);
    TEST_ASSERT_EQUAL_INT(300, rt::kSteerFollowingErrMs);
    TEST_ASSERT_EQUAL_INT(50, rt::kSteerCmdRateHz);
    TEST_ASSERT_EQUAL_INT(30000, rt::kSbwAngleOffset);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, rt::kSteerEstopRampDegS);
    TEST_ASSERT_EQUAL_INT(500, rt::kSteerEstopHoldMs);

    // 3. Timing and CAN defaults
    TEST_ASSERT_EQUAL_INT(100, rt::kControlLoopHz);
    TEST_ASSERT_EQUAL_INT(500000, rt::kCanLowBitrateHz);
    TEST_ASSERT_EQUAL_INT(500000, rt::kCanHighBitrateHz);
    TEST_ASSERT_EQUAL_INT(1500, rt::kLowCanPeerTimeoutMs);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_rt_reset_defaults);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
