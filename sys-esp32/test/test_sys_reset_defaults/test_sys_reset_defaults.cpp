#include <unity.h>
#include <cstdint>
#include "config.h"
#include "shared_config.h"
#include "mode_manager.h"
#include "brake_control.h"
#include "inhibit_state.h"

// g_inhibit_reasons / g_latched_fault_reasons are defined in inhibit_state.cpp
// (linked by the native test build); we only reset them between tests.

void setUp(void) {
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
}

void tearDown(void) {}

void test_sys_config_defaults(void) {
    // Architectural timing and safety constants
    TEST_ASSERT_EQUAL_INT(500000, sys::kCanBitrateHz);
    TEST_ASSERT_EQUAL_INT(100, sys::kControlLoopHz);
    TEST_ASSERT_EQUAL_INT(20, sys::kSafetyCheckHz);
    TEST_ASSERT_EQUAL_INT(50, sys::kGearCheckHz);
    TEST_ASSERT_EQUAL_INT(500, sys::kDebounceMs);

    // ESTOP, Brake & Staleness policies
    TEST_ASSERT_EQUAL_FLOAT(15.0f, sys::kBrakeManualStroke);
    TEST_ASSERT_EQUAL_FLOAT(27.0f, sys::kBrakeMaxStroke);
    TEST_ASSERT_EQUAL_INT(100, sys::kMtrEstopAckTimeoutMs);
    TEST_ASSERT_EQUAL_INT(200, sys::kMtrFbkStaleMs);
    TEST_ASSERT_EQUAL_INT(3, sys::kMtrFbkRecoverFrames);
    TEST_ASSERT_EQUAL_INT(500, sys::kEstopRateLimitWindowMs);
    TEST_ASSERT_EQUAL_INT(2, sys::kEstopRateLimitMax);
}

void test_sys_inhibit_state_machine(void) {
    TEST_ASSERT_FALSE(sys::transient_inhibited());
    TEST_ASSERT_FALSE(sys::latched_fault_present());

    // 1. Set MTR feedback loss inhibit
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_EQUAL_HEX32(sys::kInhibitMtrFbkLoss, sys::g_inhibit_reasons.load());

    // 2. Set SEB comms loss inhibit (multi-owner)
    sys::set_inhibit(sys::kInhibitSebCommsLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_EQUAL_HEX32(sys::kInhibitMtrFbkLoss | sys::kInhibitSebCommsLoss, sys::g_inhibit_reasons.load());

    // 3. Clear one owner independently
    sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_EQUAL_HEX32(sys::kInhibitSebCommsLoss, sys::g_inhibit_reasons.load());

    sys::clear_inhibit(sys::kInhibitSebCommsLoss);
    TEST_ASSERT_FALSE(sys::transient_inhibited());

    // 4. Latched fault path
    sys::set_latched_fault(sys::kLatchedSebL3);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    TEST_ASSERT_EQUAL_HEX32(sys::kLatchedSebL3, sys::g_latched_fault_reasons.load());
}

void test_sys_subsystem_reset_initialization(void) {
    sys::ModeManager mm;
    mm.init();
    TEST_ASSERT_EQUAL(can::Mode::Manual, mm.mode());

    sys::BrakeControl bc;
    bc.init();
    TEST_ASSERT_EQUAL(sys::BrakeState::BOOT_WAIT, bc.state());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_config_defaults);
    RUN_TEST(test_sys_inhibit_state_machine);
    RUN_TEST(test_sys_subsystem_reset_initialization);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
