// TWAI (built-in CAN controller) driver — low-level CAN bus.
// Architecture.md §7.2. Uses shared/can/can_driver.h wrapper.
// This file is a thin instantiation — the real driver is in shared/.

#include "can_driver_twai.h"
#include "can/can_driver.h"
#include "esp_log.h"
#include <new>

namespace rt {
namespace {

constexpr const char* kTag = "twai";

// Global instance created by can_low_init() in static storage.
alignas(can::CanDriver) unsigned char g_can_low_storage[sizeof(can::CanDriver)];
can::CanDriver* g_can_low = nullptr;

}  // anonymous namespace

bool can_low_init(int tx_gpio, int rx_gpio, int bitrate_hz) {
    if (g_can_low) {
        ESP_LOGW(kTag, "already initialized");
        return true;
    }

    g_can_low = new (static_cast<void*>(g_can_low_storage)) can::CanDriver(
        can::CanDriver::Config{tx_gpio, rx_gpio, bitrate_hz});
    if (!g_can_low->init()) {
        ESP_LOGE(kTag, "TWAI init failed (TX=%d RX=%d)", tx_gpio, rx_gpio);
        g_can_low->~CanDriver();
        g_can_low = nullptr;
        return false;
    }

    ESP_LOGI(kTag, "TWAI ready: TX=%d RX=%d @ %d kbit/s",
             tx_gpio, rx_gpio, bitrate_hz / 1000);
    return true;
}

can::CanDriver* can_low_driver() {
    return g_can_low;
}

}  // namespace rt
