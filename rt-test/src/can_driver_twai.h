#pragma once

#include <cstdint>

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol/compat/can.hpp"

namespace rt {

class TwaiDriver {
public:
    struct Config {
        int tx_gpio;
        int rx_gpio;
        int bitrate_hz;
    };

    explicit TwaiDriver(const Config& config) : m_config(config) {}

    ~TwaiDriver() {
        if (m_initialized) {
            twai_stop();
            twai_driver_uninstall();
        }
    }

    bool init() {
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

    bool send(const can::Frame& frame, uint32_t timeout_ms = 10) {
        twai_message_t message{};
        message.identifier = frame.id;
        message.extd = frame.extended ? 1 : 0;
        message.data_length_code = frame.dlc;
        for (uint8_t i = 0; i < frame.dlc && i < frame.data.size(); ++i)
            message.data[i] = frame.data[i];
        return twai_transmit(&message, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    }

    void get_error_counters(uint8_t& tec, uint8_t& rec) const {
        twai_status_info_t info{};
        if (twai_get_status_info(&info) == ESP_OK) {
            tec = static_cast<uint8_t>(info.tx_error_counter);
            rec = static_cast<uint8_t>(info.rx_error_counter);
        } else {
            tec = rec = 0;
        }
    }

private:
    Config m_config;
    bool m_initialized = false;
};

}  // namespace rt
