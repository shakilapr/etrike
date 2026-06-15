#pragma once
// CAN driver — RAII wrapper around ESP-IDF TWAI.
// Shared between RT and SYS ESP32-S3 (identical hardware interface).

#include "can_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "esp_log.h"

namespace can {

class CanDriver {
public:
    struct Config {
        int tx_gpio;
        int rx_gpio;
        int bitrate_hz;
    };

    static Config default_config() {
        return { 5, 4, 500'000 };
    }

    CanDriver() : m_config(default_config()) {}
    explicit CanDriver(const Config& cfg) : m_config(cfg) {}

    ~CanDriver() {
        twai_stop();
        twai_driver_uninstall();
    }

    // Non-copyable
    CanDriver(const CanDriver&) = delete;
    CanDriver& operator=(const CanDriver&) = delete;

    bool init() {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(0,
            static_cast<gpio_num_t>(m_config.tx_gpio),
            static_cast<gpio_num_t>(m_config.rx_gpio),
            TWAI_MODE_NORMAL);
        twai_timing_config_t t = {};
        t.quanta_resolution_hz = 8'000'000;
        if (m_config.bitrate_hz == 500'000) {
            t.tseg_1 = 11; t.tseg_2 = 4; t.sjw = 2;
        } else {
            t.tseg_1 = 14; t.tseg_2 = 7; t.sjw = 3;
        }
        twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
        if (twai_start() != ESP_OK) return false;

        ESP_LOGI("can", "TWAI TX=%d RX=%d @ %d kbit/s",
                 m_config.tx_gpio, m_config.rx_gpio,
                 m_config.bitrate_hz / 1000);
        m_initialized = true;
        return true;
    }

    bool is_initialized() const { return m_initialized; }

    // Receive — non-blocking.  Returns true if a frame was available.
    bool receive(Frame& out, TickType_t timeout_ms = 100) {
        twai_message_t rx;
        if (twai_receive(&rx, pdMS_TO_TICKS(timeout_ms)) != ESP_OK)
            return false;

        out.id       = rx.identifier;
        out.extended = rx.extd;
        out.dlc      = rx.data_length_code;
        for (int i = 0; i < rx.data_length_code && i < 8; ++i)
            out.data[i] = rx.data[i];
        return true;
    }

    // Send — returns true on success.
    bool send(const Frame& frame, TickType_t timeout_ms = 10) {
        twai_message_t tx = {};
        tx.identifier       = frame.id;
        tx.extd             = frame.extended ? 1 : 0;
        tx.data_length_code = frame.dlc;
        tx.self             = 0;
        tx.ss               = 1;
        for (int i = 0; i < frame.dlc && i < 8; ++i)
            tx.data[i] = frame.data[i];
        return twai_transmit(&tx, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    }

    // Send with blocking until success
    bool send_blocking(const Frame& frame, TickType_t timeout_ms = 100) {
        twai_message_t tx = {};
        tx.identifier       = frame.id;
        tx.extd             = frame.extended ? 1 : 0;
        tx.data_length_code = frame.dlc;
        tx.self             = 0;
        tx.ss               = 1;
        for (int i = 0; i < frame.dlc && i < 8; ++i)
            tx.data[i] = frame.data[i];
        return twai_transmit(&tx, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    }

    // Get TWAI error counters
    void get_error_counters(uint8_t& tec, uint8_t& rec) const {
        twai_status_info_t info;
        if (twai_get_status_info(&info) == ESP_OK) {
            tec = info.tx_error_counter;
            rec = info.rx_error_counter;
        } else {
            tec = rec = 0;
        }
    }

private:
    Config m_config;
    bool   m_initialized = false;
};

} // namespace can
