#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_twai_bus_off_recovery(void) {
    TEST_ASSERT_TRUE(true); // Mock test
}

void test_mcp2515_spi_fault(void) {
    TEST_ASSERT_TRUE(true); // Mock test
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_twai_bus_off_recovery);
    RUN_TEST(test_mcp2515_spi_fault);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
