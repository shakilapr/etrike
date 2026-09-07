// Verifies the SYS_SAFETY_STS (0x011) stream-loss fail-safe trigger.
//
// Invariant (architecture §8.6): if SYS stops publishing 0x011 for > 700 ms,
// RT must keep (or set) the E-stop latch. This test pins the *trigger
// condition* (rt::sys_safety_sts_lost) so the 700 ms threshold and the
// "never received == not a loss" start-up rule can't regress.

#include "safety_stream_loss.h"
#include "unity.h"

static void test_never_received_is_not_a_loss() {
    // last_rx_us == 0 means "0x011 never seen" — must NOT trip during startup.
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(0, 10'000'000));
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(0, 0));
}

static void test_fresh_stream_is_not_a_loss() {
    const int64_t last = 5'000'000;
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(last, last + 600'000));  // 600 ms
    TEST_ASSERT_FALSE(rt::sys_safety_sts_lost(last, last + 700'000));  // boundary, strictly >
}

static void test_loss_trips_after_700ms() {
    const int64_t last = 5'000'000;
    TEST_ASSERT_TRUE(rt::sys_safety_sts_lost(last, last + 700'001));   // 700.001 ms
    TEST_ASSERT_TRUE(rt::sys_safety_sts_lost(last, last + 5'000'000));  // 5 s
}

void setUp(void) {}
void tearDown(void) {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_never_received_is_not_a_loss);
    RUN_TEST(test_fresh_stream_is_not_a_loss);
    RUN_TEST(test_loss_trips_after_700ms);
    return UNITY_END();
}
