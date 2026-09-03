#pragma once
// RM-ESP32 handle-based TWAI CAN driver interface & health monitoring.
// Ported directly from sys-esp32 for wire-level compatibility and DLC 0 preservation.

#include <cstdint>
#include <cstring>
#include <atomic>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "protocol/compat/can.hpp"

namespace can {

class CanDriver {
public:
    enum class HealthState : uint8_t { Active, Warning, Passive, BusOff };
    struct HealthSnapshot {
        HealthState state;
        uint16_t tec;
        uint16_t rec;
        bool recovery_in_progress;
        uint32_t recovery_attempts;
        uint32_t last_transition_tick;
    };

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
        config.fail_retry_cnt = 0;
        config.tx_queue_depth = 1;

        esp_err_t result = twai_new_node_onchip(&config, &node_);
        if (result == ESP_OK) {
            twai_event_callbacks_t callbacks{};
            callbacks.on_rx_done = &CanDriver::on_rx_done_;
            callbacks.on_tx_done = &CanDriver::on_tx_done_;
            callbacks.on_state_change = &CanDriver::on_state_change_;
            result = twai_node_register_event_callbacks(node_, &callbacks, this);
        }
        if (result == ESP_OK) result = twai_node_enable(node_);
        if (result == ESP_OK) {
            initialized_ = true;
            state_.store(TWAI_ERROR_ACTIVE, std::memory_order_release);
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
        log_first_io_after_recovery_(true);
        return true;
    }

    bool send(const Frame& source, TickType_t timeout_ms = 20) {
        if (!initialized_ || !node_ || source.dlc > 8) return false;
        if (state_.load(std::memory_order_acquire) == TWAI_ERROR_BUS_OFF
            || esp_timer_get_time()
                < tx_resume_not_before_us_.load(std::memory_order_acquire)) {
            return false;
        }
        uint8_t index = 0;
        if (xQueueReceive(free_tx_slots_, &index, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return false;
        }
        // SAFETY_ESTOP (0x001) is classic DLC 0.
        const uint8_t dlc = (source.id == 0x001u) ? 0 : source.dlc;
        TxSlot& slot = tx_slots_[index];
        slot.frame = {};
        slot.frame.header.id = source.id;
        slot.frame.header.ide = source.extended;
        slot.frame.header.dlc = dlc;
        slot.frame.buffer = slot.data;
        slot.frame.buffer_len = dlc;
        if (dlc) std::memcpy(slot.data, source.data.data(), dlc);

        if (twai_node_transmit(node_, &slot.frame, timeout_ms) != ESP_OK) {
            xQueueSend(free_tx_slots_, &index, 0);
            return false;
        }
        log_first_io_after_recovery_(false);
        return true;
    }

    void get_error_counters(std::uint8_t& tec, std::uint8_t& rec) const {
        twai_node_status_t info{};
        if (node_ && twai_node_get_info(node_, &info, nullptr) == ESP_OK) {
            tec = info.state == TWAI_ERROR_BUS_OFF
                ? UINT8_MAX : static_cast<uint8_t>(info.tx_error_count);
            rec = static_cast<uint8_t>(info.rx_error_count);
        } else {
            tec = rec = 0;
        }
    }

    bool recovery() {
        if (!initialized_ || !node_) return false;
        if (xSemaphoreTake(control_mutex_, pdMS_TO_TICKS(200)) != pdTRUE) return false;
        twai_node_status_t info{};
        esp_err_t result = twai_node_get_info(node_, &info, nullptr);
        if (result == ESP_OK && info.state == TWAI_ERROR_BUS_OFF) {
            const uint32_t attempt = recovery_attempts_.fetch_add(1, std::memory_order_relaxed) + 1;
            last_recovery_attempt_us_.store(esp_timer_get_time(), std::memory_order_relaxed);
            ESP_LOGW("can", "state=bus_off recovery=start attempt=%lu tec=%u rec=%u",
                     static_cast<unsigned long>(attempt), info.tx_error_count, info.rx_error_count);
            result = twai_node_recover(node_);
            recovery_in_progress_.store(result == ESP_OK, std::memory_order_release);
        }
        xSemaphoreGive(control_mutex_);
        return result == ESP_OK;
    }

    bool service_recovery(int64_t now_us) {
        if (recovery_completed_pending_.exchange(false, std::memory_order_acq_rel)) {
            const TickType_t elapsed = xTaskGetTickCount()
                - bus_off_started_tick_.load(std::memory_order_relaxed);
            reset_tx_slots_();
            const uint32_t streak =
                consecutive_bus_offs_.load(std::memory_order_relaxed);
            const int64_t backoff_us = recovery_backoff_us_(streak);
            tx_resume_not_before_us_.store(now_us + backoff_us,
                                           std::memory_order_release);
            ESP_LOGI("can",
                     "state=active recovery=complete elapsed_ms=%lu "
                     "tx_backoff_ms=%lld streak=%lu",
                     static_cast<unsigned long>(elapsed * portTICK_PERIOD_MS),
                     static_cast<long long>(backoff_us / 1000),
                     static_cast<unsigned long>(streak));
        }
        const TickType_t last_bus_off =
            bus_off_started_tick_.load(std::memory_order_relaxed);
        if (state_.load(std::memory_order_acquire) == TWAI_ERROR_ACTIVE
            && last_bus_off != 0
            && xTaskGetTickCount() - last_bus_off > pdMS_TO_TICKS(10'000)) {
            consecutive_bus_offs_.store(0, std::memory_order_relaxed);
        }
        if (state_.load(std::memory_order_acquire) != TWAI_ERROR_BUS_OFF) return false;
        const int64_t last = last_recovery_attempt_us_.load(std::memory_order_relaxed);
        if (recovery_in_progress_.load(std::memory_order_acquire)
            && now_us - last < 3'000'000) return false;
        return recovery();
    }

    bool recovery_needed() const {
        return state_.load(std::memory_order_acquire) == TWAI_ERROR_BUS_OFF;
    }

    HealthSnapshot health_snapshot() const {
        twai_node_status_t info{};
        if (node_) (void)twai_node_get_info(node_, &info, nullptr);
        const auto state = state_.load(std::memory_order_acquire);
        return {map_state_(state),
                static_cast<uint16_t>(state == TWAI_ERROR_BUS_OFF ? 255 : info.tx_error_count),
                info.rx_error_count,
                recovery_in_progress_.load(std::memory_order_acquire),
                recovery_attempts_.load(std::memory_order_relaxed),
                last_transition_tick_.load(std::memory_order_relaxed)};
    }

private:
    static constexpr uint8_t kTxSlots = 1;
    static int64_t recovery_backoff_us_(uint32_t streak) {
        const uint32_t shift = streak > 4 ? 4 : (streak > 0 ? streak - 1 : 0);
        const int64_t delay = 500'000LL << shift;
        return delay > 5'000'000LL ? 5'000'000LL : delay;
    }
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

    static bool IRAM_ATTR on_rx_done_(twai_node_handle_t node,
                                      const twai_rx_done_event_data_t* event,
                                      void* user_ctx);
    static bool IRAM_ATTR on_tx_done_(twai_node_handle_t node,
                                      const twai_tx_done_event_data_t* event,
                                      void* user_ctx);
    static bool IRAM_ATTR on_state_change_(twai_node_handle_t node,
                                            const twai_state_change_event_data_t* event,
                                            void* user_ctx);

    static HealthState map_state_(twai_error_state_t state) {
        switch (state) {
        case TWAI_ERROR_WARNING: return HealthState::Warning;
        case TWAI_ERROR_PASSIVE: return HealthState::Passive;
        case TWAI_ERROR_BUS_OFF: return HealthState::BusOff;
        default: return HealthState::Active;
        }
    }

    void log_first_io_after_recovery_(bool rx) {
        auto& pending = rx ? first_rx_pending_ : first_tx_pending_;
        if (!pending.exchange(false, std::memory_order_acq_rel)) return;
        const TickType_t elapsed = xTaskGetTickCount()
            - bus_off_started_tick_.load(std::memory_order_relaxed);
        ESP_LOGI("can", "post_recovery first_%s elapsed_ms=%lu",
                 rx ? "rx" : "tx", static_cast<unsigned long>(elapsed * portTICK_PERIOD_MS));
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
    std::atomic<twai_error_state_t> state_{TWAI_ERROR_ACTIVE};
    std::atomic<bool> recovery_in_progress_{false};
    std::atomic<bool> recovery_completed_pending_{false};
    std::atomic<uint32_t> recovery_attempts_{0};
    std::atomic<uint32_t> last_transition_tick_{0};
    std::atomic<int64_t> last_recovery_attempt_us_{0};
    std::atomic<uint32_t> bus_off_started_tick_{0};
    std::atomic<bool> first_rx_pending_{false};
    std::atomic<bool> first_tx_pending_{false};
    std::atomic<uint32_t> consecutive_bus_offs_{0};
    std::atomic<int64_t> tx_resume_not_before_us_{0};
};

}  // namespace can
