#pragma once
// SYS ESP32-S3 low-CAN driver using the handle-based TWAI API.
// The deprecated API expands DLC=0 to DLC=8 inside ESP-IDF 5.5; this API
// preserves the zero-length SAFETY_ESTOP wire contract.

#include <cstdint>
#include <cstring>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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
        if (node_) {
            twai_node_disable(node_);
            twai_node_delete(node_);
        }
        if (rx_queue_) vQueueDelete(rx_queue_);
        if (free_tx_slots_) vQueueDelete(free_tx_slots_);
        if (control_mutex_) vSemaphoreDelete(control_mutex_);
    }

    CanDriver(const CanDriver&) = delete;
    CanDriver& operator=(const CanDriver&) = delete;

    bool init() {
        if (!rx_queue_) rx_queue_ = xQueueCreate(32, sizeof(RxItem));
        if (!free_tx_slots_) free_tx_slots_ = xQueueCreate(kTxSlots, sizeof(uint8_t));
        if (!control_mutex_) control_mutex_ = xSemaphoreCreateMutex();
        if (!rx_queue_ || !free_tx_slots_ || !control_mutex_) return false;
        if (xSemaphoreTake(control_mutex_, pdMS_TO_TICKS(500)) != pdTRUE) return false;

        if (node_) {
            twai_node_disable(node_);
            twai_node_delete(node_);
            node_ = nullptr;
        }
        initialized_ = false;
        xQueueReset(rx_queue_);
        reset_tx_slots_();

        twai_onchip_node_config_t config{};
        config.io_cfg.tx = static_cast<gpio_num_t>(config_.tx_gpio);
        config.io_cfg.rx = static_cast<gpio_num_t>(config_.rx_gpio);
        config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
        config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
        config.bit_timing.bitrate = config_.bitrate_hz;
        config.fail_retry_cnt = 3;
        config.tx_queue_depth = kTxSlots;

        esp_err_t result = twai_new_node_onchip(&config, &node_);
        if (result == ESP_OK) {
            twai_event_callbacks_t callbacks{};
            callbacks.on_rx_done = &CanDriver::on_rx_done_;
            callbacks.on_tx_done = &CanDriver::on_tx_done_;
            result = twai_node_register_event_callbacks(node_, &callbacks, this);
        }
        if (result == ESP_OK) result = twai_node_enable(node_);
        if (result == ESP_OK) {
            initialized_ = true;
            ESP_LOGI("can", "TWAI TX=%d RX=%d @ %d kbit/s", config_.tx_gpio,
                     config_.rx_gpio, config_.bitrate_hz / 1000);
        } else if (node_) {
            twai_node_delete(node_);
            node_ = nullptr;
        }
        xSemaphoreGive(control_mutex_);
        return initialized_;
    }

    bool receive(Frame& out, TickType_t timeout_ms = 100) {
        if (!initialized_) return false;
        RxItem item{};
        if (xQueueReceive(rx_queue_, &item, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
        out = Frame(item.id, item.extended, item.dlc);
        std::memcpy(out.data.data(), item.data, item.dlc);
        return true;
    }

    bool send(const Frame& source, TickType_t timeout_ms = 20) {
        if (!initialized_ || !node_ || source.dlc > 8) return false;
        uint8_t index = 0;
        if (xQueueReceive(free_tx_slots_, &index, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return false;
        }
        // SAFETY_ESTOP (0x001) is classic DLC 0. Never retransmit padded DLC-8 zeros.
        const uint8_t dlc = (source.id == 0x001u) ? 0 : source.dlc;
        TxSlot& slot = tx_slots_[index];
        slot.frame = {};
        slot.frame.header.id = source.id;
        slot.frame.header.ide = source.extended;
        slot.frame.header.dlc = dlc;
        slot.frame.buffer = slot.data;
        slot.frame.buffer_len = dlc;  // Intentionally zero for SAFETY_ESTOP.
        if (dlc) std::memcpy(slot.data, source.data.data(), dlc);

        if (twai_node_transmit(node_, &slot.frame, timeout_ms) != ESP_OK) {
            xQueueSend(free_tx_slots_, &index, 0);
            return false;
        }
        return true;
    }

    void get_error_counters(std::uint8_t& tec, std::uint8_t& rec) const {
        twai_node_status_t info{};
        if (node_ && twai_node_get_info(node_, &info, nullptr) == ESP_OK) {
            tec = static_cast<uint8_t>(info.tx_error_count);
            rec = static_cast<uint8_t>(info.rx_error_count);
        } else {
            tec = rec = 0;
        }
    }

    bool recovery() {
        if (!initialized_ || !node_) return init();
        if (xSemaphoreTake(control_mutex_, pdMS_TO_TICKS(200)) != pdTRUE) return false;
        twai_node_status_t info{};
        esp_err_t result = twai_node_get_info(node_, &info, nullptr);
        if (result == ESP_OK && info.state == TWAI_ERROR_BUS_OFF) {
            ESP_LOGW("can", "recovery: bus-off tec=%u rec=%u", info.tx_error_count,
                     info.rx_error_count);
            result = twai_node_recover(node_);
        }
        xSemaphoreGive(control_mutex_);
        return result == ESP_OK;
    }

private:
    static constexpr uint8_t kTxSlots = 16;
    struct RxItem {
        uint32_t id;
        uint8_t dlc;
        bool extended;
        uint8_t data[8];
    };
    struct TxSlot {
        twai_frame_t frame{};
        uint8_t data[8]{};
    };

    static bool on_rx_done_(twai_node_handle_t node,
                                      const twai_rx_done_event_data_t*,
                                      void* user_ctx) {
        auto* self = static_cast<CanDriver*>(user_ctx);
        RxItem item{};
        twai_frame_t frame{};
        frame.buffer = item.data;
        frame.buffer_len = sizeof(item.data);
        if (twai_node_receive_from_isr(node, &frame) != ESP_OK || frame.header.dlc > 8) {
            return false;
        }
        item.id = frame.header.id;
        item.dlc = static_cast<uint8_t>(frame.header.dlc);
        item.extended = frame.header.ide;
        BaseType_t wake = pdFALSE;
        xQueueSendFromISR(self->rx_queue_, &item, &wake);
        return wake == pdTRUE;
    }

    static bool on_tx_done_(twai_node_handle_t,
                                      const twai_tx_done_event_data_t* event,
                                      void* user_ctx) {
        auto* self = static_cast<CanDriver*>(user_ctx);
        BaseType_t wake = pdFALSE;
        for (uint8_t index = 0; index < kTxSlots; ++index) {
            if (event->done_tx_frame == &self->tx_slots_[index].frame) {
                xQueueSendFromISR(self->free_tx_slots_, &index, &wake);
                break;
            }
        }
        return wake == pdTRUE;
    }

    void reset_tx_slots_() {
        xQueueReset(free_tx_slots_);
        for (uint8_t index = 0; index < kTxSlots; ++index) {
            xQueueSend(free_tx_slots_, &index, 0);
        }
    }

    Config config_;
    twai_node_handle_t node_{nullptr};
    QueueHandle_t rx_queue_{nullptr};
    QueueHandle_t free_tx_slots_{nullptr};
    SemaphoreHandle_t control_mutex_{nullptr};
    TxSlot tx_slots_[kTxSlots]{};
    bool initialized_{false};
};

}  // namespace can
