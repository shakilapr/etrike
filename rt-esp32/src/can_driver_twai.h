#pragma once
// TWAI CAN driver — low-level CAN bus (built-in ESP32-S3 controller).
// Architecture.md §7.2: TX=GPIO5, RX=GPIO4, 500 kbit/s.

#include <cstdint>
#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "protocol/compat/can.hpp"
#include "config.h"

namespace rt {

class TwaiDriver {
public:
    struct Config {
        int tx_gpio;
        int rx_gpio;
        int bitrate_hz;
    };

    explicit TwaiDriver(const Config& config) : m_config(config) {}
    ~TwaiDriver();

    TwaiDriver(const TwaiDriver&) = delete;
    TwaiDriver& operator=(const TwaiDriver&) = delete;

    bool init();
    bool recovery();
    bool receive(can::Frame& out, uint32_t timeout_ms = 100);
    // Longer default TX wait: bus with peers/noise can need more than 10 ms.
    bool send(const can::Frame& frame, uint32_t timeout_ms = 50);
    void get_error_counters(uint8_t& tec, uint8_t& rec) const;
    // state is twai_state_t cast to uint32_t (0=stopped … 3=bus-off).
    bool status(uint32_t& state, uint32_t& tec, uint32_t& rec) const;

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

    static bool on_rx_done(twai_node_handle_t node,
                           const twai_rx_done_event_data_t* event,
                           void* user_ctx);
    static bool on_tx_done(twai_node_handle_t node,
                           const twai_tx_done_event_data_t* event,
                           void* user_ctx);
    void reset_tx_slots();

    Config m_config;
    twai_node_handle_t m_node = nullptr;
    QueueHandle_t m_rx_queue = nullptr;
    QueueHandle_t m_free_tx_slots = nullptr;
    SemaphoreHandle_t m_control_mutex = nullptr;
    TxSlot m_tx_slots[kTxSlots]{};
    bool m_initialized = false;
};

// Initialize the low-level TWAI CAN bus. Returns true on success.
bool can_low_init(int tx_gpio = rt::kCanLowTxGpio,
                   int rx_gpio = rt::kCanLowRxGpio,
                   int bitrate_hz = rt::kCanLowBitrateHz);

// Get the driver instance (nullptr if not initialized).
TwaiDriver* can_low_driver();

}  // namespace rt
