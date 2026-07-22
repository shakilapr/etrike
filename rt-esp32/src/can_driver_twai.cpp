// Handle-based TWAI driver for the ESP32-S3 low CAN bus.
// Unlike the deprecated driver, this API preserves classic CAN DLC=0 by
// carrying buffer_len=0 through to the HAL.

#include "can_driver_twai.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_twai_onchip.h"
#include <cstring>
#include <new>

namespace rt {
namespace {

constexpr const char* kTag = "twai";
alignas(TwaiDriver) unsigned char g_can_low_storage[sizeof(TwaiDriver)];
TwaiDriver* g_can_low = nullptr;

}  // namespace

TwaiDriver::~TwaiDriver() {
    if (m_node) {
        twai_node_disable(m_node);
        twai_node_delete(m_node);
        m_node = nullptr;
    }
    if (m_rx_queue) vQueueDelete(m_rx_queue);
    if (m_free_tx_slots) vQueueDelete(m_free_tx_slots);
    if (m_control_mutex) vSemaphoreDelete(m_control_mutex);
}

void TwaiDriver::reset_tx_slots() {
    xQueueReset(m_free_tx_slots);
    for (uint8_t index = 0; index < kTxSlots; ++index) {
        xQueueSend(m_free_tx_slots, &index, 0);
    }
}

bool IRAM_ATTR TwaiDriver::on_rx_done(twai_node_handle_t node,
                                      const twai_rx_done_event_data_t*,
                                      void* user_ctx) {
    auto* self = static_cast<TwaiDriver*>(user_ctx);
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
    xQueueSendFromISR(self->m_rx_queue, &item, &wake);
    return wake == pdTRUE;
}

bool IRAM_ATTR TwaiDriver::on_tx_done(twai_node_handle_t,
                                      const twai_tx_done_event_data_t* event,
                                      void* user_ctx) {
    auto* self = static_cast<TwaiDriver*>(user_ctx);
    BaseType_t wake = pdFALSE;
    for (uint8_t index = 0; index < kTxSlots; ++index) {
        if (event->done_tx_frame == &self->m_tx_slots[index].frame) {
            xQueueSendFromISR(self->m_free_tx_slots, &index, &wake);
            break;
        }
    }
    return wake == pdTRUE;
}

bool TwaiDriver::init() {
    if (!m_rx_queue) m_rx_queue = xQueueCreate(32, sizeof(RxItem));
    if (!m_free_tx_slots) m_free_tx_slots = xQueueCreate(kTxSlots, sizeof(uint8_t));
    if (!m_control_mutex) m_control_mutex = xSemaphoreCreateMutex();
    if (!m_rx_queue || !m_free_tx_slots || !m_control_mutex) return false;
    if (xSemaphoreTake(m_control_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    if (m_node) {
        twai_node_disable(m_node);
        twai_node_delete(m_node);
        m_node = nullptr;
    }
    m_initialized = false;
    xQueueReset(m_rx_queue);
    reset_tx_slots();

    twai_onchip_node_config_t config{};
    config.io_cfg.tx = static_cast<gpio_num_t>(m_config.tx_gpio);
    config.io_cfg.rx = static_cast<gpio_num_t>(m_config.rx_gpio);
    config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    config.bit_timing.bitrate = m_config.bitrate_hz;
    config.fail_retry_cnt = 3;
    config.tx_queue_depth = kTxSlots;

    esp_err_t result = twai_new_node_onchip(&config, &m_node);
    if (result == ESP_OK) {
        twai_event_callbacks_t callbacks{};
        callbacks.on_rx_done = &TwaiDriver::on_rx_done;
        callbacks.on_tx_done = &TwaiDriver::on_tx_done;
        result = twai_node_register_event_callbacks(m_node, &callbacks, this);
    }
    if (result == ESP_OK) result = twai_node_enable(m_node);
    if (result == ESP_OK) {
        m_initialized = true;
    } else if (m_node) {
        twai_node_delete(m_node);
        m_node = nullptr;
    }
    xSemaphoreGive(m_control_mutex);
    return m_initialized;
}

bool TwaiDriver::recovery() {
    if (!m_initialized || !m_node) return init();
    if (xSemaphoreTake(m_control_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    twai_node_status_t info{};
    esp_err_t result = twai_node_get_info(m_node, &info, nullptr);
    if (result == ESP_OK && info.state == TWAI_ERROR_BUS_OFF) {
        ESP_LOGW(kTag, "recovery: bus-off tec=%u rec=%u", info.tx_error_count,
                 info.rx_error_count);
        result = twai_node_recover(m_node);
    }
    xSemaphoreGive(m_control_mutex);
    return result == ESP_OK;
}

bool TwaiDriver::status(uint32_t& state, uint32_t& tec, uint32_t& rec) const {
    twai_node_status_t info{};
    if (!m_node || twai_node_get_info(m_node, &info, nullptr) != ESP_OK) {
        state = tec = rec = 0;
        return false;
    }
    // Preserve the legacy wrapper contract used by main.cpp: 1=running, 2=bus-off.
    state = info.state == TWAI_ERROR_BUS_OFF ? 2U : 1U;
    tec = info.tx_error_count;
    rec = info.rx_error_count;
    return true;
}

bool TwaiDriver::receive(can::Frame& out, uint32_t timeout_ms) {
    if (!m_initialized) return false;
    RxItem item{};
    if (xQueueReceive(m_rx_queue, &item, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    out = can::Frame(item.id, item.extended, item.dlc);
    std::memcpy(out.data.data(), item.data, item.dlc);
    return true;
}

bool TwaiDriver::send(const can::Frame& source, uint32_t timeout_ms) {
    if (!m_initialized || !m_node || source.dlc > 8) return false;
    uint8_t index = 0;
    if (xQueueReceive(m_free_tx_slots, &index, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;

    TxSlot& slot = m_tx_slots[index];
    slot.frame = {};
    slot.frame.header.id = source.id;
    slot.frame.header.ide = source.extended;
    slot.frame.header.dlc = source.dlc;
    slot.frame.buffer = slot.data;
    slot.frame.buffer_len = source.dlc;  // Must remain zero for a DLC-0 frame.
    if (source.dlc) std::memcpy(slot.data, source.data.data(), source.dlc);

    if (twai_node_transmit(m_node, &slot.frame, timeout_ms) != ESP_OK) {
        xQueueSend(m_free_tx_slots, &index, 0);
        return false;
    }
    return true;
}

void TwaiDriver::get_error_counters(uint8_t& tec, uint8_t& rec) const {
    twai_node_status_t info{};
    if (m_node && twai_node_get_info(m_node, &info, nullptr) == ESP_OK) {
        tec = static_cast<uint8_t>(info.tx_error_count);
        rec = static_cast<uint8_t>(info.rx_error_count);
    } else {
        tec = rec = 0;
    }
}

bool can_low_init(int tx_gpio, int rx_gpio, int bitrate_hz) {
    if (g_can_low) return true;
    g_can_low = new (static_cast<void*>(g_can_low_storage)) TwaiDriver(
        TwaiDriver::Config{tx_gpio, rx_gpio, bitrate_hz});
    if (!g_can_low->init()) {
        ESP_LOGE(kTag, "TWAI init failed (TX=%d RX=%d)", tx_gpio, rx_gpio);
        g_can_low->~TwaiDriver();
        g_can_low = nullptr;
        return false;
    }
    ESP_LOGI(kTag, "TWAI ready: TX=%d RX=%d @ %d kbit/s", tx_gpio, rx_gpio,
             bitrate_hz / 1000);
    return true;
}

TwaiDriver* can_low_driver() { return g_can_low; }

}  // namespace rt
