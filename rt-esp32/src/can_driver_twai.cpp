// TWAI (built-in CAN controller) driver — low-level CAN bus.
// Architecture.md §7.2.

#include "can_driver_twai.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <new>

namespace rt {
namespace {

constexpr const char* kTag = "twai";

alignas(TwaiDriver) unsigned char g_can_low_storage[sizeof(TwaiDriver)];
TwaiDriver* g_can_low = nullptr;

// Serialize install / recover / send / receive — concurrent uninstall was
// crashing (spinlock assert) when recovery raced RX/TX tasks.
static SemaphoreHandle_t g_twai_mutex = nullptr;

static bool lock_twai(uint32_t ms = 100) {
    if (!g_twai_mutex) return true;
    return xSemaphoreTake(g_twai_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void unlock_twai() {
    if (g_twai_mutex) xSemaphoreGive(g_twai_mutex);
}

}  // anonymous namespace

TwaiDriver::~TwaiDriver() {
    if (m_initialized) {
        if (lock_twai(500)) {
            twai_stop();
            twai_driver_uninstall();
            m_initialized = false;
            unlock_twai();
        }
    }
}

bool TwaiDriver::init() {
    if (!g_twai_mutex) {
        g_twai_mutex = xSemaphoreCreateMutex();
    }
    if (!lock_twai(500)) return false;

    if (m_initialized) {
        twai_stop();
        twai_driver_uninstall();
        m_initialized = false;
    }

    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        0, static_cast<gpio_num_t>(m_config.tx_gpio),
        static_cast<gpio_num_t>(m_config.rx_gpio), TWAI_MODE_NORMAL);
    general.tx_queue_len = 16;
    general.rx_queue_len = 32;

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
    bool ok = false;
    if (twai_driver_install(&general, &timing, &filter) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            m_initialized = true;
            ok = true;
        } else {
            twai_driver_uninstall();
        }
    }
    unlock_twai();
    return ok;
}

bool TwaiDriver::recovery() {
    // Soft recovery first (official path). Full reinstall only if soft fails.
    // Never uninstall while other tasks call send/receive without the mutex.
    if (!m_initialized) return init();
    if (!lock_twai(200)) {
        ESP_LOGW(kTag, "recovery: mutex busy — skip");
        return false;
    }

    twai_status_info_t info{};
    if (twai_get_status_info(&info) == ESP_OK) {
        ESP_LOGW(kTag, "recovery: state=%d tec=%lu rec=%lu",
                 static_cast<int>(info.state),
                 static_cast<unsigned long>(info.tx_error_counter),
                 static_cast<unsigned long>(info.rx_error_counter));
    }

    bool ok = false;
    if (info.state == TWAI_STATE_BUS_OFF) {
        if (twai_initiate_recovery() == ESP_OK) {
            // Wait until controller leaves bus-off (STOPPED then start).
            for (int i = 0; i < 50; ++i) {
                unlock_twai();
                vTaskDelay(pdMS_TO_TICKS(10));
                if (!lock_twai(100)) return false;
                if (twai_get_status_info(&info) == ESP_OK
                    && info.state == TWAI_STATE_STOPPED) {
                    break;
                }
            }
            if (twai_start() == ESP_OK) {
                ok = true;
                ESP_LOGI(kTag, "soft recovery OK (bus-off → start)");
            }
        }
    } else if (info.state == TWAI_STATE_STOPPED) {
        ok = (twai_start() == ESP_OK);
    } else if (info.state == TWAI_STATE_RUNNING) {
        // Still running — clear transmit queue if API available; just report OK.
        ok = true;
    }

    if (!ok) {
        ESP_LOGW(kTag, "soft recovery failed — full reinstall");
        twai_stop();
        twai_driver_uninstall();
        m_initialized = false;
        unlock_twai();
        vTaskDelay(pdMS_TO_TICKS(50));
        return init();
    }

    unlock_twai();
    return true;
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
    if (!m_initialized) return false;
    // Non-blocking path for 0 timeout; short lock for actual receive.
    twai_message_t message{};
    // twai_receive blocks; do not hold mutex across full timeout — only guard
    // against concurrent uninstall. Use try-lock style: check init, receive,
    // if recovery races, receive fails cleanly.
    if (!lock_twai(timeout_ms == 0 ? 5 : timeout_ms + 20)) return false;
    if (!m_initialized) {
        unlock_twai();
        return false;
    }
    // Release mutex while waiting so recovery can run between frames.
    unlock_twai();
    if (twai_receive(&message, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) return false;
    out = can::Frame(message.identifier, message.extd != 0, message.data_length_code);
    for (uint8_t i = 0; i < message.data_length_code && i < out.data.size(); ++i)
        out.data[i] = message.data[i];
    return true;
}

bool TwaiDriver::send(const can::Frame& frame, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    twai_status_info_t info{};
    if (twai_get_status_info(&info) == ESP_OK) {
        // Do not hammer the bus while bus-off / recovering.
        if (info.state == TWAI_STATE_BUS_OFF || info.state == TWAI_STATE_RECOVERING) {
            return false;
        }
    }

    twai_message_t message{};
    message.identifier = frame.id;
    message.extd = frame.extended ? 1 : 0;
    message.data_length_code = frame.dlc;
    for (uint8_t i = 0; i < frame.dlc && i < frame.data.size(); ++i)
        message.data[i] = frame.data[i];

    if (!lock_twai(timeout_ms + 20)) return false;
    if (!m_initialized) {
        unlock_twai();
        return false;
    }
    unlock_twai();  // do not hold during transmit wait
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
