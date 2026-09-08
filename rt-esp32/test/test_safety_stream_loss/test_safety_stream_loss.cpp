// Verifies the SYS_SAFETY_STS (0x011) authority-acquisition + stream-loss
// fail-safe (issue #10).
//
// Invariants:
//   * Booting grants NO authority: UNACQUIRED until N consecutive valid frames.
//   * ACQUIRED -> stream lost > kSysSafetyStsTimeoutUs -> LOST -> estop latch.
//   * A stream that NEVER arrives within kSysSafetyAcquireTimeoutUs is a
//     SYS-absent fail-safe (inhibit + fault), NOT a global ESTOP latch.

#include "safety_stream_loss.h"
#include "unity.h"

static void test_legacy_never_received_is_not_a_loss() {
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(0, 10'000'000));
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(0, 0));
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(-1, 10'000'000));
}

static void test_legacy_fresh_stream_is_not_a_loss() {
    const int64_t last = 5'000'000;
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(last, last + 600'000));
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(last, last + 700'000));
}

static void test_legacy_loss_trips_after_700ms() {
    const int64_t last = 5'000'000;
    TEST_ASSERT_TRUE(rt::sys_safety_sts_lost(last, last + 700'001));
    TEST_ASSERT_TRUE(rt::sys_safety_sts_lost(last, last + 5'000'000));
}

static void test_unacquired_grants_no_authority() {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);
    // Just after boot, stream not yet seen: no authority, no fault yet, no estop.
    auto s = sup.update(1'000'000, -1);
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::UNACQUIRED),
                      uint8_t(s.state));
    TEST_ASSERT_FALSE(s.motion_authorized);
    TEST_ASSERT_FALSE(s.estop_latch_required);
    TEST_ASSERT_FALSE(s.sys_absent_fault);
}

static void test_acquired_after_n_valid_frames() {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);
    int64_t now = 1'000'000;
    // Two consecutive valid 0x011 frames at the stream cadence.
    auto s = sup.update(now, now);           // frame 1
    now += 100'000;
    s = sup.update(now, now);                // frame 2 -> ACQUIRED
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::ACQUIRED),
                      uint8_t(s.state));
    TEST_ASSERT_TRUE(s.motion_authorized);
    TEST_ASSERT_FALSE(s.estop_latch_required);
}

static void test_acquired_stream_loss_latches_estop() {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);
    int64_t now = 1'000'000;
    int64_t last_valid = now;
    auto s = sup.update(now, last_valid);            // frame 1
    now += 100'000;
    last_valid = now;
    s = sup.update(now, last_valid);                 // frame 2 -> ACQUIRED
    TEST_ASSERT_TRUE(s.motion_authorized);
    // Stream stops: no new frames (last_valid stays put). Just inside timeout.
    now += rt::kSysSafetyStsTimeoutUs - 1;
    s = sup.update(now, last_valid);
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::ACQUIRED),
                      uint8_t(s.state));
    // Beyond the timeout -> LOST -> estop latch required.
    now += 2;
    s = sup.update(now, last_valid);
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::LOST),
                      uint8_t(s.state));
    TEST_ASSERT_TRUE(s.estop_latch_required);
    TEST_ASSERT_FALSE(s.motion_authorized);
}

static void test_acquired_stream_resume_reauthorizes() {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);
    int64_t now = 1'000'000;
    int64_t last_valid = now;
    auto s = sup.update(now, last_valid);            // frame 1
    now += 100'000;
    last_valid = now;
    s = sup.update(now, last_valid);                 // frame 2 -> ACQUIRED
    now += 800'000;                                  // lost > 700 ms
    s = sup.update(now, last_valid);
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::LOST),
                      uint8_t(s.state));
    // Frames flow again (estop clear itself is SYS's two-frame 0x011 sequence).
    now += 100'000;
    last_valid = now;
    s = sup.update(now, last_valid);                 // new frame
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::ACQUIRED),
                      uint8_t(s.state));
    TEST_ASSERT_TRUE(s.motion_authorized);
    TEST_ASSERT_FALSE(s.estop_latch_required);
}

static void test_never_received_deadline_is_sys_absent_fault_not_estop() {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);
    // Past the acquisition deadline with still no valid frame: SYS-absent
    // fail-safe — fault + inhibit, but NOT a global estop latch.
    auto s = sup.update(rt::kSysSafetyAcquireTimeoutUs + 1, -1);
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::UNACQUIRED),
                      uint8_t(s.state));
    TEST_ASSERT_TRUE(s.sys_absent_fault);
    TEST_ASSERT_FALSE(s.motion_authorized);
    TEST_ASSERT_FALSE(s.estop_latch_required);
}

static void test_stray_frames_do_not_grant_authority() {
    rt::SafetyStreamSupervisor sup;
    sup.reset(0);
    int64_t now = 1'000'000;
    // A single valid frame then a long silence must NOT accumulate to ACQUIRED
    // (acquisition credit resets on a > stream-cycle gap).
    auto s = sup.update(now, now);           // frame 1
    now += 800'000;                          // gap > 600 ms
    s = sup.update(now, -1);                 // no frame, credit should reset
    TEST_ASSERT_EQUAL(uint8_t(rt::SafetyStreamState::UNACQUIRED),
                      uint8_t(s.state));
    TEST_ASSERT_FALSE(s.motion_authorized);
}

void setUp(void) {}
void tearDown(void) {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_legacy_never_received_is_not_a_loss);
    RUN_TEST(test_legacy_fresh_stream_is_not_a_loss);
    RUN_TEST(test_legacy_loss_trips_after_700ms);
    RUN_TEST(test_unacquired_grants_no_authority);
    RUN_TEST(test_acquired_after_n_valid_frames);
    RUN_TEST(test_acquired_stream_loss_latches_estop);
    RUN_TEST(test_acquired_stream_resume_reauthorizes);
    RUN_TEST(test_never_received_deadline_is_sys_absent_fault_not_estop);
    RUN_TEST(test_stray_frames_do_not_grant_authority);
    return UNITY_END();
}
