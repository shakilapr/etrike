#include <unity.h>
#include <cstdint>
#include "config.h"

// Variables replicated from main.cpp
static bool g_bench_solo_mode = false;
static bool g_bypass_eps_sync = false;
static bool g_bypass_seb_sync = false;
static bool g_bypass_mtr_absent = false;

static void evaluate_bypass_modes(int run_mode, int override_pin_level) {
    if (run_mode == 2) {
        // Pure software simulation mode
        g_bench_solo_mode = true;
        g_bypass_eps_sync = true;
        g_bypass_seb_sync = true;
        g_bypass_mtr_absent = true;
    } else if (run_mode == 1) {
        // Hardware bench / prototype mode: checks jumper pin
        if (override_pin_level == 0) { // Active-low pull-up jumper
            g_bench_solo_mode = true;
            g_bypass_eps_sync = true;
            g_bypass_seb_sync = true;
            g_bypass_mtr_absent = true;
        } else {
            g_bench_solo_mode = false;
            g_bypass_eps_sync = false;
            g_bypass_seb_sync = false;
            g_bypass_mtr_absent = false;
        }
    } else {
        // Vehicle production mode: strictly no bypasses allowed
        g_bench_solo_mode = false;
        g_bypass_eps_sync = false;
        g_bypass_seb_sync = false;
        g_bypass_mtr_absent = false;
    }
}

void setUp(void) {
    g_bench_solo_mode = false;
    g_bypass_eps_sync = false;
    g_bypass_seb_sync = false;
    g_bypass_mtr_absent = false;
}

void tearDown(void) {}

void test_sys_bypass_modes(void) {
    // 1. Vehicle Production Mode (run_mode = 0) -> All bypasses FALSE
    evaluate_bypass_modes(0, 0);
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 2. Hardware Prototype without Jumper (run_mode = 1, pin = 1) -> Safety Enforced
    evaluate_bypass_modes(1, 1);
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 3. Hardware Prototype WITH Jumper (run_mode = 1, pin = 0) -> Bypasses Active
    evaluate_bypass_modes(1, 0);
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);

    // 4. Bench Simulation Mode (run_mode = 2) -> Software Bypasses Active
    evaluate_bypass_modes(2, 1);
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_bypass_modes);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
