#include <unity.h>
#include <cstdint>
#include "config.h"
#include "bypass_modes.h"

// The four runtime bypass flags are defined by the firmware main.cpp (excluded
// from the native test build). The bypass tests exercise the SHARED pure
// decision function (bypass_modes.h) that main.cpp calls, so they verify the
// shipped logic rather than a copy.
static bool g_bench_solo_mode = false;
static bool g_bypass_eps_sync = false;
static bool g_bypass_seb_sync = false;
static bool g_bypass_mtr_absent = false;

static void evaluate_bypass_modes(int run_mode, bool override_pin_low) {
    const etrike::BypassState b = etrike::evaluate_run_mode_bypasses(run_mode, override_pin_low);
    g_bench_solo_mode = b.bench_solo_mode;
    g_bypass_eps_sync = b.bypass_eps_sync;
    g_bypass_seb_sync = b.bypass_seb_sync;
    g_bypass_mtr_absent = b.bypass_mtr_absent;
}

void setUp(void) {
    g_bench_solo_mode = false;
    g_bypass_eps_sync = false;
    g_bypass_seb_sync = false;
    g_bypass_mtr_absent = false;
}

void tearDown(void) {}

void test_rt_bypass_modes(void) {
    // 1. Vehicle Production Mode -> Safety strictly enforced
    evaluate_bypass_modes(0, false);
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 2. Hardware Bench without Override Jumper -> Safety strictly enforced
    evaluate_bypass_modes(1, false);
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 3. Hardware Bench WITH Active-Low Override Jumper -> Bypasses allowed
    evaluate_bypass_modes(1, true);
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);

    // 4. Bench Simulation Mode -> Bypasses allowed (jumper state irrelevant)
    evaluate_bypass_modes(2, false);
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);

    evaluate_bypass_modes(2, true);
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_rt_bypass_modes);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
