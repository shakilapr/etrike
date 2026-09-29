#include <unity.h>
#include <cstdint>
#include "system_ready.h"

using sys::SystemReadyInputs;
using sys::SystemReadyLevel;
using sys::evaluate_system_ready;

void setUp(void) {}
void tearDown(void) {}

static SystemReadyInputs healthy() {
    SystemReadyInputs in;
    in.estop_or_inhibit = false;
    in.rt_ok            = true;
    in.mtr_ok           = true;
    in.seb_ok           = true;
    in.host_ok          = true;
    in.mtr_required     = true;
    in.seb_required     = true;
    return in;
}

void test_ready_full_when_all_present(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::Full),
                      static_cast<int>(evaluate_system_ready(healthy())));
}

void test_ready_estop_inhibit_blocks_everything(void) {
    SystemReadyInputs in = healthy();
    in.estop_or_inhibit = true;
    // Even with a missing peer waived, a real fault is always Blocked.
    in.mtr_required = false;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::Blocked),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_rt_absent_is_blocked(void) {
    SystemReadyInputs in = healthy();
    in.rt_ok = false;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::Blocked),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_host_absent_reads_breathe_level(void) {
    SystemReadyInputs in = healthy();
    in.host_ok = false;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::HostAbsent),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_mtr_absent_required_is_blocked(void) {
    SystemReadyInputs in = healthy();
    in.mtr_ok = false;  // mtr_required == true
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::Blocked),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_mtr_absent_waived_is_mtr_level(void) {
    SystemReadyInputs in = healthy();
    in.mtr_ok = false;
    in.mtr_required = false;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::MtrAbsent),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_mtr_absent_takes_precedence_over_host_absent(void) {
    SystemReadyInputs in = healthy();
    in.mtr_ok = false;
    in.mtr_required = false;
    in.host_ok = false;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::MtrAbsent),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_seb_absent_required_is_blocked(void) {
    SystemReadyInputs in = healthy();
    in.seb_ok = false;  // seb_required == true
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::Blocked),
                      static_cast<int>(evaluate_system_ready(in)));
}

void test_ready_seb_absent_waived_does_not_block(void) {
    SystemReadyInputs in = healthy();
    in.seb_ok = false;
    in.seb_required = false;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemReadyLevel::Full),
                      static_cast<int>(evaluate_system_ready(in)));
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_ready_full_when_all_present);
    RUN_TEST(test_ready_estop_inhibit_blocks_everything);
    RUN_TEST(test_ready_rt_absent_is_blocked);
    RUN_TEST(test_ready_host_absent_reads_breathe_level);
    RUN_TEST(test_ready_mtr_absent_required_is_blocked);
    RUN_TEST(test_ready_mtr_absent_waived_is_mtr_level);
    RUN_TEST(test_ready_mtr_absent_takes_precedence_over_host_absent);
    RUN_TEST(test_ready_seb_absent_required_is_blocked);
    RUN_TEST(test_ready_seb_absent_waived_does_not_block);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    app_main();
    return 0;
}
#endif
