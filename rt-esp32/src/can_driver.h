#pragma once
// CAN driver — RAII wrapper around ESP-IDF TWAI.
// Single ESP32-S3 operates two TWAI controllers (public + private CAN buses).

#include "can_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "esp_log.h"

namespace can {

class CanDriver {
public:
    struct Config {
        int tx_gpio     = 5;
        int rx_gpio     = 4;
        int bitrate_hz  = 500'000;
    };

    explicit CanDriver(const Config& cfg = {})
        : m_config(cfg) {}

    ~CanDriver() {
        twai_stop();
        twai_driver_uninstall();
    }

    // Non-copyable
    CanDriver(const CanDriver&) = delete;
    CanDriver& operator=(const CanDriver&) = delete;

    bool init() {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
            m_config.tx_gpio, m_config.rx_gpio, TWAI_MODE_NORMAL);
        twai_timing_config_t  t = (m_config.bitrate_hz == 500'000)
            ? TWAI_TIMING_CONFIG_500KBITS()
            : TWAI_TIMING_CONFIG_250KBITS();
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
