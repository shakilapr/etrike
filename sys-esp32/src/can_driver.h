#pragma once
// SYS ESP32-S3 CAN driver over the canonical protocol frame.
// Low-level CAN bus only (built-in TWAI, GPIO4/5, 500 kbit/s).

#include <cstdint>

#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
        if (initialized_) {
            if (lock_(200)) {
                twai_stop();
                twai_driver_uninstall();
                initialized_ = false;
                unlock_();
            }
        }
        if (mutex_) {
            vSemaphoreDelete(mutex_);
            mutex_ = nullptr;
        }
    }

    CanDriver(const CanDriver&) = delete;
    CanDriver& operator=(const CanDriver&) = delete;

    bool init() {
        if (!mutex_) mutex_ = xSemaphoreCreateMutex();
        if (!lock_(500)) return false;

        if (initialized_) {
            twai_stop();
            twai_driver_uninstall();
            initialized_ = false;
        }

        twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT_V2(
            0, static_cast<gpio_num_t>(config_.tx_gpio),
            static_cast<gpio_num_t>(config_.rx_gpio), TWAI_MODE_NORMAL);
        general.tx_queue_len = 16;
        general.rx_queue_len = 32;

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
        bool ok = false;
        if (twai_driver_install(&general, &timing, &filter) == ESP_OK) {
            if (twai_start() == ESP_OK) {
                initialized_ = true;
                ok = true;
                ESP_LOGI("can", "TWAI TX=%d RX=%d @ %d kbit/s", config_.tx_gpio,
                         config_.rx_gpio, config_.bitrate_hz / 1000);
            } else {
                twai_driver_uninstall();
            }
        }
        unlock_();
        return ok;
    }

    bool receive(Frame& out, TickType_t timeout_ms = 100) {
        if (!initialized_) return false;
        twai_message_t message{};
        // Do not hold mutex across long receive waits.
        if (twai_receive(&message, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) return false;
        if (message.data_length_code > out.data.size()) return false;

        out = Frame(message.identifier, message.extd != 0, message.data_length_code);
        for (std::size_t index = 0; index < out.dlc; ++index) out.data[index] = message.data[index];
        return true;
    }

    bool send(const Frame& frame, TickType_t timeout_ms = 20) {
        if (!initialized_ || frame.dlc > frame.data.size()) return false;

        twai_status_info_t info{};
        if (twai_get_status_info(&info) == ESP_OK) {
            if (info.state == TWAI_STATE_BUS_OFF || info.state == TWAI_STATE_RECOVERING) {
                return false;  // wait for recovery — do not thrash TX
            }
        }

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
        // Soft bus-off recovery; full reinstall only as fallback.
        if (!initialized_) return init();
        if (!lock_(200)) {
            ESP_LOGW("can", "recovery: mutex busy");
            return false;
        }

        twai_status_info_t info{};
        twai_get_status_info(&info);
        ESP_LOGW("can", "recovery: state=%d tec=%lu rec=%lu",
                 static_cast<int>(info.state),
                 static_cast<unsigned long>(info.tx_error_counter),
                 static_cast<unsigned long>(info.rx_error_counter));

        bool ok = false;
        if (info.state == TWAI_STATE_BUS_OFF) {
            if (twai_initiate_recovery() == ESP_OK) {
                for (int i = 0; i < 50; ++i) {
                    unlock_();
                    vTaskDelay(pdMS_TO_TICKS(10));
                    if (!lock_(100)) return false;
                    if (twai_get_status_info(&info) == ESP_OK
                        && info.state == TWAI_STATE_STOPPED) {
                        break;
                    }
                }
                ok = (twai_start() == ESP_OK);
            }
        } else if (info.state == TWAI_STATE_STOPPED) {
            ok = (twai_start() == ESP_OK);
        } else if (info.state == TWAI_STATE_RUNNING) {
            ok = true;
        }

        if (!ok) {
            ESP_LOGW("can", "soft recovery failed — reinstall");
            twai_stop();
            twai_driver_uninstall();
            initialized_ = false;
            unlock_();
            vTaskDelay(pdMS_TO_TICKS(50));
            return init();
        }
        unlock_();
        return true;
    }

private:
    bool lock_(uint32_t ms) {
        if (!mutex_) return true;
        return xSemaphoreTake(mutex_, pdMS_TO_TICKS(ms)) == pdTRUE;
    }
    void unlock_() {
        if (mutex_) xSemaphoreGive(mutex_);
    }

    Config config_;
    bool initialized_{false};
    SemaphoreHandle_t mutex_{nullptr};
};

}  // namespace can
