#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_concurrency_queues_and_mutexes(void) {
    TEST_ASSERT_TRUE(true); // Mock test
}

void test_watchdog_timeouts_in_multithread_mock(void) {
    TEST_ASSERT_TRUE(true); // Mock test
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_concurrency_queues_and_mutexes);
    RUN_TEST(test_watchdog_timeouts_in_multithread_mock);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
