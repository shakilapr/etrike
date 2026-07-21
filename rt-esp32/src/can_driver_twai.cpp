// TWAI (built-in CAN controller) driver — low-level CAN bus.
// Architecture.md §7.2.

#include "can_driver_twai.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <new>

namespace rt {
namespace {

constexpr const char* kTag = "twai";

// Global instance created by can_low_init() in static storage.
alignas(TwaiDriver) unsigned char g_can_low_storage[sizeof(TwaiDriver)];
TwaiDriver* g_can_low = nullptr;

}  // anonymous namespace

TwaiDriver::~TwaiDriver() {
    if (m_initialized) {
        twai_stop();
        twai_driver_uninstall();
    }
}

bool TwaiDriver::init() {
    if (m_initialized) {
        twai_stop();
        twai_driver_uninstall();
        m_initialized = false;
    }

    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        0, static_cast<gpio_num_t>(m_config.tx_gpio),
        static_cast<gpio_num_t>(m_config.rx_gpio), TWAI_MODE_NORMAL);
    twai_timing_config_t timing{};
    timing.quanta_resolution_hz = 8'000'000;
    if (m_config.bitrate_hz == 500'000) {
        timing.tseg_1 = 11;
        timing.tseg_2 = 4;
        timing.sjw = 2;
    } else {
        timing.tseg_1 = 14;
        timing.tseg_2 = 7;
        timing.sjw = 3;
    }
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&general, &timing, &filter) != ESP_OK) return false;
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }
    m_initialized = true;
    return true;
}

bool TwaiDriver::recovery() {
    // Full reinstall is more reliable than initiate_recovery alone after
    // solo-node bus-off (no ACK) or flaky bench wiring.
    if (!m_initialized) return init();
    twai_status_info_t info{};
    if (twai_get_status_info(&info) == ESP_OK) {
        ESP_LOGW(kTag, "TWAI recovery: state=%d tec=%lu rec=%lu",
                 static_cast<int>(info.state),
                 static_cast<unsigned long>(info.tx_error_counter),
                 static_cast<unsigned long>(info.rx_error_counter));
    }
    twai_stop();
    twai_driver_uninstall();
    m_initialized = false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return init();
}

bool TwaiDriver::status(uint32_t& state, uint32_t& tec, uint32_t& rec) const {
    twai_status_info_t info{};
    if (twai_get_status_info(&info) != ESP_OK) {
        state = tec = rec = 0;
        return false;
    }
    state = static_cast<uint32_t>(info.state);
    tec = info.tx_error_counter;
    rec = info.rx_error_counter;
    return true;
}

bool TwaiDriver::receive(can::Frame& out, uint32_t timeout_ms) {
    twai_message_t message{};
    if (twai_receive(&message, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) return false;
    out = can::Frame(message.identifier, message.extd != 0, message.data_length_code);
    for (uint8_t i = 0; i < message.data_length_code && i < out.data.size(); ++i)
        out.data[i] = message.data[i];
    return true;
}

bool TwaiDriver::send(const can::Frame& frame, uint32_t timeout_ms) {
    twai_message_t message{};
    message.identifier = frame.id;
    message.extd = frame.extended ? 1 : 0;
    message.data_length_code = frame.dlc;
    for (uint8_t i = 0; i < frame.dlc && i < frame.data.size(); ++i)
        message.data[i] = frame.data[i];
    return twai_transmit(&message, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}

void TwaiDriver::get_error_counters(uint8_t& tec, uint8_t& rec) const {
    twai_status_info_t info{};
    if (twai_get_status_info(&info) == ESP_OK) {
        tec = static_cast<uint8_t>(info.tx_error_counter);
        rec = static_cast<uint8_t>(info.rx_error_counter);
    } else {
        tec = rec = 0;
    }
}

bool can_low_init(int tx_gpio, int rx_gpio, int bitrate_hz) {
    if (g_can_low) {
        ESP_LOGW(kTag, "already initialized");
        return true;
    }

    g_can_low = new (static_cast<void*>(g_can_low_storage)) TwaiDriver(
        TwaiDriver::Config{tx_gpio, rx_gpio, bitrate_hz});
    if (!g_can_low->init()) {
        ESP_LOGE(kTag, "TWAI init failed (TX=%d RX=%d)", tx_gpio, rx_gpio);
        g_can_low->~TwaiDriver();
        g_can_low = nullptr;
        return false;
    }

    ESP_LOGI(kTag, "TWAI ready: TX=%d RX=%d @ %d kbit/s",
             tx_gpio, rx_gpio, bitrate_hz / 1000);
    return true;
}

TwaiDriver* can_low_driver() {
    return g_can_low;
}

}  // namespace rt
