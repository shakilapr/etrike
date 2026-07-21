#pragma once
// SYS ESP32-S3 CAN driver over the canonical protocol frame.
// Low-level CAN bus only (built-in TWAI, GPIO4/5, 500 kbit/s).
// CAN config is constructed inline in main.cpp via can::CanDriver::Config{}.

#include <cstdint>

#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol/compat/can.hpp"

namespace can {

class CanDriver {
public:
    struct Config {
        int tx_gpio;
        int rx_gpio;
        int bitrate_hz;
    };

    explicit CanDriver(const Config& config) : config_(config) {}
    ~CanDriver() {
        twai_stop();
        twai_driver_uninstall();
    }

    CanDriver(const CanDriver&) = delete;
    CanDriver& operator=(const CanDriver&) = delete;

    bool init() {
        if (initialized_) {
            twai_stop();
            twai_driver_uninstall();
            initialized_ = false;
        }

        twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT_V2(
            0, static_cast<gpio_num_t>(config_.tx_gpio),
            static_cast<gpio_num_t>(config_.rx_gpio), TWAI_MODE_NORMAL);
        twai_timing_config_t timing{};
        timing.quanta_resolution_hz = 8'000'000;
        if (config_.bitrate_hz == 500'000) {
            timing.tseg_1 = 11;
            timing.tseg_2 = 4;
            timing.sjw = 2;
        } else {
            timing.tseg_1 = 14;
            timing.tseg_2 = 7;
            timing.sjw = 3;
        }
        twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        if (twai_driver_install(&general, &timing, &filter) != ESP_OK) return false;
        if (twai_start() != ESP_OK) return false;

        initialized_ = true;
        ESP_LOGI("can", "TWAI TX=%d RX=%d @ %d kbit/s", config_.tx_gpio,
                 config_.rx_gpio, config_.bitrate_hz / 1000);
        return true;
    }

    bool receive(Frame& out, TickType_t timeout_ms = 100) {
        twai_message_t message{};
        if (twai_receive(&message, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) return false;
        if (message.data_length_code > out.data.size()) return false;

        out = Frame(message.identifier, message.extd != 0, message.data_length_code);
        for (std::size_t index = 0; index < out.dlc; ++index) out.data[index] = message.data[index];
        return true;
    }

    bool send(const Frame& frame, TickType_t timeout_ms = 10) {
        if (frame.dlc > frame.data.size()) return false;
        twai_message_t message{};
        message.identifier = frame.id;
        message.extd = frame.extended ? 1 : 0;
        message.data_length_code = frame.dlc;
        for (std::size_t index = 0; index < frame.dlc; ++index) message.data[index] = frame.data[index];
        return twai_transmit(&message, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    }

    void get_error_counters(std::uint8_t& tec, std::uint8_t& rec) const {
        twai_status_info_t info{};
        if (twai_get_status_info(&info) == ESP_OK) {
            tec = static_cast<std::uint8_t>(info.tx_error_counter);
            rec = static_cast<std::uint8_t>(info.rx_error_counter);
        } else {
            tec = 0;
            rec = 0;
        }
    }

    bool recovery() {
        // Full reinstall after bus-off / no-ACK is more reliable on bench.
        if (!initialized_) return init();
        twai_stop();
        twai_driver_uninstall();
        initialized_ = false;
        vTaskDelay(pdMS_TO_TICKS(20));
        return init();
    }

private:
    Config config_;
    bool initialized_{false};
};

}  // namespace can
