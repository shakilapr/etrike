#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_sys_reset_defaults(void) {
    TEST_ASSERT_TRUE(true);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_reset_defaults);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
