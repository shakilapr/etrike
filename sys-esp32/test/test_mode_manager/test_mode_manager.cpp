#include <unity.h>
#include <cstdint>
#include "protocol/compat/can.hpp"
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

// N1 safety regression: a CAN SYS_MODE_CMD (0x110) must NEVER clear a latched
// ESTOP — only the physical START button or MODE 3s long-press may (Gap #11).
// set_from_can() is the 0x110 ingest path and must refuse to leave Estop.
void test_mode_manager_can_mode_cmd_does_not_clear_estop(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    mm.set_from_can(uint8_t(Mode::Manual));   // 0x110 = MANUAL
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    mm.set_from_can(uint8_t(Mode::Auto));     // 0x110 = AUTO
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    // Still unrecoverable via CAN even after repeated frames.
    for (int i = 0; i < 5; ++i) mm.set_from_can(uint8_t(Mode::Manual));
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

// Issue #4 safety invariant: the estop_active bit SYS publishes in 0x011 /
// 0x7FE must track the *system* ESTOP latch (ModeManager mode == ESTOP),
// not merely the hardware button. A software ESTOP (CAN 0x001, SEB L3,
// EGAS, bus-off, MTR-reported-ESTOP) latches the mode into ESTOP, so the
// published bit must be 1 even when the physical button is released —
// otherwise RT/MTR could two-frame-clear into a false all-clear.
void test_estop_latched_invariant(void) {
    // hw button active alone → latched
    TEST_ASSERT_TRUE(ModeManager::estop_latched(Mode::Manual, true));
    TEST_ASSERT_TRUE(ModeManager::estop_latched(Mode::Auto, true));
    // mode ESTOP alone (software source) → latched regardless of hw button
    TEST_ASSERT_TRUE(ModeManager::estop_latched(Mode::Estop, false));
    TEST_ASSERT_TRUE(ModeManager::estop_latched(Mode::Estop, true));
    // neither → not latched
    TEST_ASSERT_FALSE(ModeManager::estop_latched(Mode::Manual, false));
    TEST_ASSERT_FALSE(ModeManager::estop_latched(Mode::Auto, false));
}

void test_estop_latched_follows_mode_latch_lifecycle(void) {
    ModeManager mm;
    mm.init();
    // Software ESTOP (e.g. force_estop from CAN 0x001) with button released:
    // published estop_active must be 1 for the whole duration of the latch.
    mm.force_estop();
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
    TEST_ASSERT_TRUE(ModeManager::estop_latched(mm.mode(), /*hw_estop=*/false));

    // A CAN mode command must not clear it (existing N1 regression).
    mm.set_from_can(uint8_t(Mode::Manual));
    TEST_ASSERT_TRUE(ModeManager::estop_latched(mm.mode(), /*hw_estop=*/false));

    // Only the physical START-button reset path exits ESTOP; only then may the
    // published bit drop to 0 so RT/MTR can begin their confirmed clear.
    mm.tick(false, true);  // START press
    mm.tick(false, false); // debounce settle
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
    TEST_ASSERT_FALSE(ModeManager::estop_latched(mm.mode(), /*hw_estop=*/false));
}

// Issue #7: ESTOP exit is a validated transaction. The system must NOT leave
// ESTOP while a latched safety fault's underlying cause is still asserted —
// otherwise the published estop_active bit would drop to 0 and let RT/MTR
// two-frame-clear into a false all-clear.
void test_estop_exit_blocked_while_latched_cause_active(void) {
    // kLatchedBrakeFollowing latched + no fresh 0x721 (byte0 == 0xFF) means the
    // brake-following cause cannot be proven clear → reset must be refused.
    g_seb_error_status.store(0);
    g_seb_status_byte0.store(0xFF);
    sys::g_latched_fault_reasons.store(0);  // clean slate
    ModeManager mm;
    mm.init();
    mm.force_estop();
    sys::set_latched_fault(sys::kLatchedBrakeFollowing);

    // START-button reset attempt — refused.
    mm.tick(false, true);
    mm.tick(false, false);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    // MODE 3s long-press — also refused while the cause is still asserted.
    bool exited = false;
    for (int i = 0; i < 40; ++i) {
        if (mm.tick(true, false)) { exited = true; break; }
    }
    TEST_ASSERT_FALSE(exited);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    // Cause clears (fresh 0x721 frame arrives) → validated reset now allowed.
    g_seb_status_byte0.store(0x00);
    mm.tick(false, true);   // release MODE, press START
    mm.tick(false, false);  // START release → falling edge → reset
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
    TEST_ASSERT_FALSE(sys::latched_fault_present());
}

// Issue #7: SEB L3 latched with error_status still == 3 also blocks the reset.
void test_estop_exit_blocked_while_seb_l3_active(void) {
    g_seb_error_status.store(3);
    g_seb_status_byte0.store(0x00);
    sys::g_latched_fault_reasons.store(0);
    ModeManager mm;
    mm.init();
    mm.force_estop();
    sys::set_latched_fault(sys::kLatchedSebL3);

    mm.tick(false, true);
    mm.tick(false, false);
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());

    // SEB recovers to error_status < 3 → reset allowed.
    g_seb_error_status.store(0);
    mm.tick(false, true);
    mm.tick(false, false);
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
    TEST_ASSERT_FALSE(sys::latched_fault_present());
}

// BUG-10: Remote Host ESTOP Reset Request tests
void test_remote_reset_rejected_when_physical_estop_active(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    
    // Physical ESTOP active (e.g. button pressed)
    uint16_t blockers = sys::get_estop_reset_blockers(
        /*physical_estop=*/true,
        /*hb_ok=*/true,
        /*measured_speed_mmps=*/0,
        /*mtr_ack_confirmed=*/true,
        /*token=*/sys::kRemoteResetTokenMagic
    );
    TEST_ASSERT_TRUE(blockers & sys::kResetBlockPhysicalEstop);
    TEST_ASSERT_FALSE(mm.try_exit_estop_remote(blockers));
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_remote_reset_rejected_when_vehicle_moving(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    
    // Measured wheel speed > 50 mm/s (e.g. 100 mm/s)
    uint16_t blockers = sys::get_estop_reset_blockers(
        /*physical_estop=*/false,
        /*hb_ok=*/true,
        /*measured_speed_mmps=*/100,
        /*mtr_ack_confirmed=*/true,
        /*token=*/sys::kRemoteResetTokenMagic
    );
    TEST_ASSERT_TRUE(blockers & sys::kResetBlockMoving);
    TEST_ASSERT_FALSE(mm.try_exit_estop_remote(blockers));
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_remote_reset_rejected_when_token_invalid(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    
    uint16_t blockers = sys::get_estop_reset_blockers(
        /*physical_estop=*/false,
        /*hb_ok=*/true,
        /*measured_speed_mmps=*/0,
        /*mtr_ack_confirmed=*/true,
        /*token=*/0x1234  // Bad magic token
    );
    TEST_ASSERT_TRUE(blockers & sys::kResetBlockInvalidToken);
    TEST_ASSERT_FALSE(mm.try_exit_estop_remote(blockers));
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_remote_reset_rejected_when_latched_fault_asserted(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    sys::set_latched_fault(sys::kLatchedSebL3);
    g_seb_error_status.store(3); // active L3
    
    uint16_t blockers = sys::get_estop_reset_blockers(
        /*physical_estop=*/false,
        /*hb_ok=*/true,
        /*measured_speed_mmps=*/0,
        /*mtr_ack_confirmed=*/true,
        /*token=*/sys::kRemoteResetTokenMagic
    );
    TEST_ASSERT_TRUE(blockers & sys::kResetBlockLatchedFault);
    TEST_ASSERT_FALSE(mm.try_exit_estop_remote(blockers));
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_remote_reset_rejected_when_mtr_ack_missing(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    
    uint16_t blockers = sys::get_estop_reset_blockers(
        /*physical_estop=*/false,
        /*hb_ok=*/true,
        /*measured_speed_mmps=*/0,
        /*mtr_ack_confirmed=*/false,  // MTR has NOT acknowledged ESTOP yet
        /*token=*/sys::kRemoteResetTokenMagic
    );
    TEST_ASSERT_TRUE(blockers & sys::kResetBlockMtrEstopActive);
    TEST_ASSERT_FALSE(mm.try_exit_estop_remote(blockers));
    TEST_ASSERT_EQUAL(Mode::Estop, mm.mode());
}

void test_remote_reset_succeeds_when_clean(void) {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    sys::g_latched_fault_reasons.store(0);
    g_seb_error_status.store(0);
    g_seb_status_byte0.store(0x00);
    
    uint16_t blockers = sys::get_estop_reset_blockers(
        /*physical_estop=*/false,
        /*hb_ok=*/true,
        /*measured_speed_mmps=*/0,
        /*mtr_ack_confirmed=*/true,
        /*token=*/sys::kRemoteResetTokenMagic
    );
    TEST_ASSERT_EQUAL_UINT16(0, blockers);
    TEST_ASSERT_TRUE(mm.try_exit_estop_remote(blockers));
    TEST_ASSERT_EQUAL(Mode::Manual, mm.mode());
    TEST_ASSERT_FALSE(sys::latched_fault_present());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_estop_latched_invariant);
    RUN_TEST(test_estop_latched_follows_mode_latch_lifecycle);
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
    RUN_TEST(test_mode_manager_can_mode_cmd_does_not_clear_estop);
    RUN_TEST(test_estop_exit_blocked_while_latched_cause_active);
    RUN_TEST(test_estop_exit_blocked_while_seb_l3_active);
    RUN_TEST(test_remote_reset_rejected_when_physical_estop_active);
    RUN_TEST(test_remote_reset_rejected_when_vehicle_moving);
    RUN_TEST(test_remote_reset_rejected_when_token_invalid);
    RUN_TEST(test_remote_reset_rejected_when_latched_fault_asserted);
    RUN_TEST(test_remote_reset_rejected_when_mtr_ack_missing);
    RUN_TEST(test_remote_reset_succeeds_when_clean);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
