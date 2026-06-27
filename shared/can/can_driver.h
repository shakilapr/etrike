#pragma once
// CAN driver — RAII wrapper around ESP-IDF TWAI.
// Shared between RT and SYS ESP32-S3 (identical hardware interface).

#include "can_protocol.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "esp_log.h"
#else
// Host-side stubs for IntelliSense / unit tests
#include <cstdint>
using TickType_t = uint32_t;
using gpio_num_t = int;
inline constexpr int ESP_OK = 0;
inline constexpr int ESP_ERR_INVALID_ARG = -1;
struct twai_general_config_t { int controller_id, mode; gpio_num_t tx_io, rx_io, clkout_io, bus_off_io; uint32_t tx_queue_len, rx_queue_len, alerts_enabled, clkout_divider; int intr_flags; };
struct twai_timing_config_t { uint32_t quanta_resolution_hz; int tseg_1, tseg_2, sjw; };
struct twai_filter_config_t { int acceptance_code, acceptance_mask; bool single_filter; };
struct twai_message_t { uint32_t identifier; uint8_t data_length_code; uint8_t data[8]; bool extd, self, ss; };
struct twai_status_info_t { uint32_t tx_error_counter, rx_error_counter, msgs_to_tx, msgs_to_rx, tx_failed_count, rx_missed_count, rx_overrun_count, arb_lost_count, bus_error_count; int state; };
enum twai_mode_t : int { TWAI_MODE_NORMAL = 0 };
inline auto TWAI_FILTER_CONFIG_ACCEPT_ALL() -> twai_filter_config_t { return {}; }
inline int twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*, const twai_filter_config_t*) { return ESP_OK; }
inline int twai_start() { return ESP_OK; }
inline int twai_stop() { return ESP_OK; }
inline int twai_driver_uninstall() { return ESP_OK; }
inline int twai_transmit(const twai_message_t*, int) { return ESP_OK; }
inline int twai_receive(twai_message_t*, int) { return 0; }
inline int twai_get_status_info(twai_status_info_t*) { return ESP_OK; }
inline int pdMS_TO_TICKS(int ms) { return ms; }
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#endif

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
        // Re-init safety: uninstall previous driver if already initialized
        if (m_initialized) {
            twai_stop();
            twai_driver_uninstall();
            m_initialized = false;
        }
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
        tx.ss               = 0;
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
        tx.ss               = 0;
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
