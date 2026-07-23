// Handle-based TWAI driver for the ESP32-S3 low CAN bus.
// Unlike the deprecated driver, this API preserves classic CAN DLC=0 by
// carrying buffer_len=0 through to the HAL.
//
// Instrumentation build: define ETRIKE_RT_TWAI_INSTRUMENT=1 to enable
// per-event atomic counters and diagnostic log dumps. This is active in
// [env:bench] via platformio.ini and lets us verify:
//   (a) whether on_tx_done fires for all queued frames after Bus-Off recovery
//   (b) whether m_free_tx_slots count tracks correctly against callbacks
//   (c) whether the driver resumes pending frames automatically after recovery

#include "can_driver_twai.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
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
    bool slot_found = false;
    for (uint8_t index = 0; index < kTxSlots; ++index) {
        if (event->done_tx_frame == &self->m_tx_slots[index].frame) {
            xQueueSendFromISR(self->m_free_tx_slots, &index, &wake);
            slot_found = true;
            break;
        }
    }
#if ETRIKE_RT_TWAI_INSTRUMENT
    if (event->done_tx_frame->header.id != 0) {  // ignore null-frame sentinels
        if (event->tx_success) {
            self->m_instr_tx_done_ok.fetch_add(1, std::memory_order_relaxed);
        } else {
            self->m_instr_tx_done_fail.fetch_add(1, std::memory_order_relaxed);
        }
        if (!slot_found) {
            self->m_instr_tx_done_no_slot.fetch_add(1, std::memory_order_relaxed);
        }
    }
#else
    (void)slot_found;
#endif
    return wake == pdTRUE;
}

bool IRAM_ATTR TwaiDriver::on_state_change(twai_node_handle_t,
                                           const twai_state_change_event_data_t* event,
                                           void* user_ctx) {
    auto* self = static_cast<TwaiDriver*>(user_ctx);
    self->m_state.store(event->new_sta, std::memory_order_release);
    self->m_last_transition_tick.store(xTaskGetTickCountFromISR(), std::memory_order_relaxed);
    if (event->new_sta == TWAI_ERROR_BUS_OFF) {
        self->m_bus_off_started_tick.store(xTaskGetTickCountFromISR(), std::memory_order_relaxed);
#if ETRIKE_RT_TWAI_INSTRUMENT
        // Snapshot the free-slot queue depth at the exact Bus-Off moment.
        // If slots leak, this count will diverge from (kTxSlots - tx_submitted + tx_done).
        UBaseType_t free_at_busoff = uxQueueMessagesWaitingFromISR(self->m_free_tx_slots);
        self->m_instr_free_slots_at_busoff.store(
            static_cast<uint8_t>(free_at_busoff), std::memory_order_relaxed);
        self->m_instr_busoff_count.fetch_add(1, std::memory_order_relaxed);
#endif
    } else if (event->old_sta == TWAI_ERROR_BUS_OFF && event->new_sta == TWAI_ERROR_ACTIVE) {
        self->m_recovery_in_progress.store(false, std::memory_order_release);
        self->m_recovery_completed_pending.store(true, std::memory_order_release);
        self->m_first_rx_pending.store(true, std::memory_order_release);
        self->m_first_tx_pending.store(true, std::memory_order_release);
#if ETRIKE_RT_TWAI_INSTRUMENT
        // Snapshot free-slot count immediately after recovery transition.
        // Per ESP-IDF docs: "pending transmissions resume right away" after recovery.
        // If that is true, on_tx_done will fire for any still-queued frames and
        // free_slots_at_recovery will climb back toward kTxSlots within milliseconds.
        UBaseType_t free_at_recovery = uxQueueMessagesWaitingFromISR(self->m_free_tx_slots);
        self->m_instr_free_slots_at_recovery.store(
            static_cast<uint8_t>(free_at_recovery), std::memory_order_relaxed);
#endif
    }
    return false;
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
    config.fail_retry_cnt = 3;  // 3 HW retries per frame; see testing notes in can_driver_twai.cpp
    config.tx_queue_depth = kTxSlots;

#if ETRIKE_RT_TWAI_SELF_TEST
    // Self-test mode: ACK is not checked during TX. Used on bench when no peer
    // node or CANalyst-II is in passive (listen-only) mode and cannot supply ACK.
    // This prevents TEC accumulation from missing ACKs, so Bus-Off never fires
    // and the slot/callback behaviour under normal operation can be measured.
    // DO NOT use in vehicle — self-test masks real bus failures.
    config.flags.enable_self_test = 1;
    ESP_LOGW(kTag, "TWAI SELF-TEST MODE ENABLED — ACK not required (bench only)");
#endif

    esp_err_t result = twai_new_node_onchip(&config, &m_node);
    if (result == ESP_OK) {
        twai_event_callbacks_t callbacks{};
        callbacks.on_rx_done = &TwaiDriver::on_rx_done;
        callbacks.on_tx_done = &TwaiDriver::on_tx_done;
        callbacks.on_state_change = &TwaiDriver::on_state_change;
        result = twai_node_register_event_callbacks(m_node, &callbacks, this);
    }
    if (result == ESP_OK) result = twai_node_enable(m_node);
    if (result == ESP_OK) {
        m_initialized = true;
        m_state.store(TWAI_ERROR_ACTIVE, std::memory_order_release);
    } else if (m_node) {
        twai_node_delete(m_node);
        m_node = nullptr;
    }
    xSemaphoreGive(m_control_mutex);
    return m_initialized;
}

bool TwaiDriver::recovery() {
    if (!m_initialized || !m_node) return false;
    if (xSemaphoreTake(m_control_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    twai_node_status_t info{};
    esp_err_t result = twai_node_get_info(m_node, &info, nullptr);
    if (result == ESP_OK && info.state == TWAI_ERROR_BUS_OFF) {
        const uint32_t attempt = m_recovery_attempts.fetch_add(1, std::memory_order_relaxed) + 1;
        m_last_recovery_attempt_us.store(esp_timer_get_time(), std::memory_order_relaxed);
        ESP_LOGW(kTag, "state=bus_off recovery=start attempt=%lu tec=%u rec=%u",
                 static_cast<unsigned long>(attempt), info.tx_error_count, info.rx_error_count);
        result = twai_node_recover(m_node);
        m_recovery_in_progress.store(result == ESP_OK, std::memory_order_release);
    }
    xSemaphoreGive(m_control_mutex);
    return result == ESP_OK;
}

bool TwaiDriver::service_recovery(int64_t now_us) {
    if (m_recovery_completed_pending.exchange(false, std::memory_order_acq_rel)) {
        const TickType_t elapsed = xTaskGetTickCount()
            - m_bus_off_started_tick.load(std::memory_order_relaxed);
        UBaseType_t free_now = uxQueueMessagesWaiting(m_free_tx_slots);
        ESP_LOGI(kTag, "state=active recovery=complete elapsed_ms=%lu free_slots=%u/%u",
                 static_cast<unsigned long>(elapsed * portTICK_PERIOD_MS),
                 static_cast<unsigned>(free_now), static_cast<unsigned>(kTxSlots));
#if ETRIKE_RT_TWAI_INSTRUMENT
        // Verify the ESP-IDF claim: "pending transmissions resume right away."
        // After a short settle window, free_slots should equal kTxSlots again
        // if the driver fired on_tx_done for all frames queued before Bus-Off.
        // If free_slots < kTxSlots here, slots are genuinely leaked.
        // We log both the ISR-time snapshot and the post-recovery task-context count.
        ESP_LOGI(kTag,
            "[INSTR] busoff#=%lu free@busoff=%u free@recovery_isr=%u free@recovery_task=%u"
            " tx_submitted=%lu tx_done_ok=%lu tx_done_fail=%lu tx_done_no_slot=%lu",
            static_cast<unsigned long>(m_instr_busoff_count.load(std::memory_order_relaxed)),
            static_cast<unsigned>(m_instr_free_slots_at_busoff.load(std::memory_order_relaxed)),
            static_cast<unsigned>(m_instr_free_slots_at_recovery.load(std::memory_order_relaxed)),
            static_cast<unsigned>(free_now),
            static_cast<unsigned long>(m_instr_tx_submitted.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(m_instr_tx_done_ok.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(m_instr_tx_done_fail.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(m_instr_tx_done_no_slot.load(std::memory_order_relaxed)));
        // Decision rule printed directly to serial:
        if (free_now < kTxSlots) {
            ESP_LOGE(kTag,
                "[INSTR] SLOT LEAK CONFIRMED: %u of %u slots not returned after recovery",
                static_cast<unsigned>(kTxSlots - free_now), static_cast<unsigned>(kTxSlots));
        } else {
            ESP_LOGI(kTag,
                "[INSTR] No slot leak: all %u slots returned after recovery",
                static_cast<unsigned>(kTxSlots));
        }
#endif
    }
    if (!recovery_needed()) return false;
    const int64_t last = m_last_recovery_attempt_us.load(std::memory_order_relaxed);
    if (m_recovery_in_progress.load(std::memory_order_acquire)
        && now_us - last < 3'000'000) return false;
    return recovery();
}

bool TwaiDriver::recovery_needed() const {
    return m_state.load(std::memory_order_acquire) == TWAI_ERROR_BUS_OFF;
}

TwaiDriver::HealthState TwaiDriver::map_state(twai_error_state_t state) {
    switch (state) {
    case TWAI_ERROR_WARNING: return HealthState::Warning;
    case TWAI_ERROR_PASSIVE: return HealthState::Passive;
    case TWAI_ERROR_BUS_OFF: return HealthState::BusOff;
    default: return HealthState::Active;
    }
}

TwaiDriver::HealthSnapshot TwaiDriver::health_snapshot() const {
    twai_node_status_t info{};
    if (m_node) (void)twai_node_get_info(m_node, &info, nullptr);
    const auto state = m_state.load(std::memory_order_acquire);
    return {map_state(state),
            static_cast<uint16_t>(state == TWAI_ERROR_BUS_OFF ? 255 : info.tx_error_count),
            info.rx_error_count,
            m_recovery_in_progress.load(std::memory_order_acquire),
            m_recovery_attempts.load(std::memory_order_relaxed),
            m_last_transition_tick.load(std::memory_order_relaxed)};
}

bool TwaiDriver::status(uint32_t& state, uint32_t& tec, uint32_t& rec) const {
    twai_node_status_t info{};
    if (!m_node || twai_node_get_info(m_node, &info, nullptr) != ESP_OK) {
        state = tec = rec = 0;
        return false;
    }
    // Preserve the legacy wrapper contract used by main.cpp: 1=running, 2=bus-off.
    state = m_state.load(std::memory_order_acquire) == TWAI_ERROR_BUS_OFF ? 2U : 1U;
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
    log_first_io_after_recovery(true);
    return true;
}

bool TwaiDriver::send(const can::Frame& source, uint32_t timeout_ms) {
    if (!m_initialized || !m_node || source.dlc > 8) return false;
    uint8_t index = 0;
    if (xQueueReceive(m_free_tx_slots, &index, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;

    // SAFETY_ESTOP (0x001) is classic DLC 0. Never retransmit padded DLC-8 zeros.
    const uint8_t dlc = (source.id == 0x001u) ? 0 : source.dlc;
    TxSlot& slot = m_tx_slots[index];
    slot.frame = {};
    slot.frame.header.id = source.id;
    slot.frame.header.ide = source.extended;
    slot.frame.header.dlc = dlc;
    slot.frame.buffer = slot.data;
    slot.frame.buffer_len = dlc;  // Must remain zero for a DLC-0 frame.
    if (dlc) std::memcpy(slot.data, source.data.data(), dlc);

    if (twai_node_transmit(m_node, &slot.frame, timeout_ms) != ESP_OK) {
        xQueueSend(m_free_tx_slots, &index, 0);
        return false;
    }
#if ETRIKE_RT_TWAI_INSTRUMENT
    m_instr_tx_submitted.fetch_add(1, std::memory_order_relaxed);
#endif
    log_first_io_after_recovery(false);
    return true;
}

void TwaiDriver::log_first_io_after_recovery(bool rx) {
    auto& pending = rx ? m_first_rx_pending : m_first_tx_pending;
    if (!pending.exchange(false, std::memory_order_acq_rel)) return;
    const TickType_t elapsed = xTaskGetTickCount()
        - m_bus_off_started_tick.load(std::memory_order_relaxed);
    ESP_LOGI(kTag, "post_recovery first_%s elapsed_ms=%lu", rx ? "rx" : "tx",
             static_cast<unsigned long>(elapsed * portTICK_PERIOD_MS));
}

void TwaiDriver::get_error_counters(uint8_t& tec, uint8_t& rec) const {
    twai_node_status_t info{};
    if (m_node && twai_node_get_info(m_node, &info, nullptr) == ESP_OK) {
        tec = m_state.load(std::memory_order_acquire) == TWAI_ERROR_BUS_OFF
            ? UINT8_MAX : static_cast<uint8_t>(info.tx_error_count);
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
