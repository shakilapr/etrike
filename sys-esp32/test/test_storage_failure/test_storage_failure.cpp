#include <unity.h>
#include <cstdint>

// Emulate ESP-IDF NVS flash error codes and recovery logic from main.cpp
enum MockEspErr {
    MOCK_ESP_OK = 0,
    MOCK_ESP_FAIL = -1,
    MOCK_ESP_ERR_NVS_NO_FREE_PAGES = 0x110d,
    MOCK_ESP_ERR_NVS_NEW_VERSION_FOUND = 0x1110,
};

struct MockNvsFlash {
    bool has_free_pages = true;
    bool new_version_found = false;
    bool erased = false;
    bool initialized = false;

    MockEspErr init() {
        if (!has_free_pages) return MOCK_ESP_ERR_NVS_NO_FREE_PAGES;
        if (new_version_found) return MOCK_ESP_ERR_NVS_NEW_VERSION_FOUND;
        initialized = true;
        return MOCK_ESP_OK;
    }

    MockEspErr erase() {
        erased = true;
        has_free_pages = true;
        new_version_found = false;
        return MOCK_ESP_OK;
    }
};

static bool boot_nvs_storage(MockNvsFlash& nvs) {
    MockEspErr ret = nvs.init();
    if (ret == MOCK_ESP_ERR_NVS_NO_FREE_PAGES || ret == MOCK_ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs.erase();
        ret = nvs.init();
    }
    return ret == MOCK_ESP_OK;
}

void setUp(void) {}
void tearDown(void) {}

void test_storage_nvs_clean_boot(void) {
    MockNvsFlash nvs{};
    TEST_ASSERT_TRUE(boot_nvs_storage(nvs));
    TEST_ASSERT_TRUE(nvs.initialized);
    TEST_ASSERT_FALSE(nvs.erased);
}

void test_storage_nvs_no_free_pages_recovery(void) {
    MockNvsFlash nvs{};
    nvs.has_free_pages = false; // Corrupt/exhausted partition

    TEST_ASSERT_TRUE(boot_nvs_storage(nvs));
    TEST_ASSERT_TRUE(nvs.erased);      // Erase triggered
    TEST_ASSERT_TRUE(nvs.initialized); // Successfully recovered
}

void test_storage_nvs_new_version_recovery(void) {
    MockNvsFlash nvs{};
    nvs.new_version_found = true; // Firmware upgrade version bump

    TEST_ASSERT_TRUE(boot_nvs_storage(nvs));
    TEST_ASSERT_TRUE(nvs.erased);
    TEST_ASSERT_TRUE(nvs.initialized);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_storage_nvs_clean_boot);
    RUN_TEST(test_storage_nvs_no_free_pages_recovery);
    RUN_TEST(test_storage_nvs_new_version_recovery);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
