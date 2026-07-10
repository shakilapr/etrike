#include <unity.h>
#include <cstdint>
#include "can/can_protocol.h"
#include "mode_manager.h"

using namespace sys;
using namespace can;

static bool run_ticks(ModeManager& mm, int n, bool mb, bool sb) {
    bool any = false;
    for (int i = 0; i < n; ++i)
        if (mm.tick(mb, sb)) any = true;
    return any;
}

static void wait_debounce(ModeManager& mm) {
    run_ticks(mm, 6, false, false);
}

void setUp(void) {}
void tearDown(void) {}

void test_mode_manager_manual_to_auto(void) {
    ModeManager mm;
    mm.init();
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());

    bool changed = mm.tick(true, false);
    TEST_ASSERT_FALSE(changed);
    changed = mm.tick(false, false);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());
}

void test_mode_manager_auto_to_manual(void) {
    ModeManager mm;
    mm.init();
    mm.tick(true, false);
    mm.tick(false, false);
    wait_debounce(mm);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());

    mm.tick(true, false);
    bool changed = mm.tick(false, false);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
}

void test_mode_manager_estop_exit_via_start_button(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    mm.tick(true, false);
    bool changed = mm.tick(false, false);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    mm.tick(false, true);
    changed = mm.tick(false, false);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
}

void test_mode_manager_estop_exit_via_mode_long_press(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();

    bool exited = false;
    for (int i = 0; i < 31; ++i) {
        bool ch = mm.tick(true, false);
        if (ch) { exited = true; break; }
    }
    TEST_ASSERT_TRUE(exited);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
}

void test_mode_manager_mode_long_press_early_release(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();

    run_ticks(mm, 20, true, false);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    mm.tick(false, false);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_mode_manager_debounce_blocks_rapid_retrigger(void) {
    ModeManager mm;
    mm.init();

    mm.tick(true, false);
    mm.tick(false, false);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());

    bool changed = mm.tick(true, false);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());

    wait_debounce(mm);
    mm.tick(true, false);
    changed = mm.tick(false, false);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
}

void test_mode_manager_start_ignored_in_non_estop(void) {
    ModeManager mm;
    mm.init();
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());

    mm.tick(false, true);
    bool changed = mm.tick(false, false);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());

    mm.tick(true, false);
    mm.tick(false, false);
    wait_debounce(mm);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());

    mm.tick(false, true);
    changed = mm.tick(false, false);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());
}

void test_parse_hmi_mode_changes_mode(void) {
    ModeManager mm;
    mm.init();
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
    
    bool changed = mm.parse_hmi_mode(uint8_t(Mode::Auto));
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL(Mode::Auto, mm.mode());

    changed = mm.parse_hmi_mode(uint8_t(Mode::Auto));
    TEST_ASSERT_FALSE(changed);
}

void test_parse_hmi_mode_ignored_in_estop(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    
    bool changed = mm.parse_hmi_mode(uint8_t(Mode::Auto));
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_parse_hmi_mode_rejects_invalid(void) {
    ModeManager mm;
    mm.init();
    
    bool changed = mm.parse_hmi_mode(2); // PURE_SIM or invalid
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_mode_manager_manual_to_auto);
    RUN_TEST(test_mode_manager_auto_to_manual);
    RUN_TEST(test_mode_manager_estop_exit_via_start_button);
    RUN_TEST(test_mode_manager_estop_exit_via_mode_long_press);
    RUN_TEST(test_mode_manager_mode_long_press_early_release);
    RUN_TEST(test_mode_manager_debounce_blocks_rapid_retrigger);
    RUN_TEST(test_mode_manager_start_ignored_in_non_estop);
    RUN_TEST(test_parse_hmi_mode_changes_mode);
    RUN_TEST(test_parse_hmi_mode_ignored_in_estop);
    RUN_TEST(test_parse_hmi_mode_rejects_invalid);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
