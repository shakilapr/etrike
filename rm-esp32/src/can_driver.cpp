#include "can_driver.h"

namespace can {

bool IRAM_ATTR CanDriver::on_rx_done_(twai_node_handle_t node,
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

    // Drop our own high-rate TX frames in ISR to prevent rx_queue_ starvation.
    // RM only needs to receive supervisor frames (e.g. 0x001 SAFETY_ESTOP, 0x110 SYS_MODE_CMD).
    const uint32_t id = frame.header.id;
    if (id == 0x169u || id == 0x7B9u || id == 0x204u || id == 0x111u || id == 0x112u) {
        return false;
    }

    item.id = id;
    item.dlc = static_cast<uint8_t>(frame.header.dlc);
    item.extended = frame.header.ide;
    BaseType_t wake = pdFALSE;
    xQueueSendFromISR(self->rx_queue_, &item, &wake);
    return wake == pdTRUE;
}

bool IRAM_ATTR CanDriver::on_tx_done_(twai_node_handle_t,
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

bool IRAM_ATTR CanDriver::on_state_change_(twai_node_handle_t,
                                           const twai_state_change_event_data_t* event,
                                           void* user_ctx) {
    auto* self = static_cast<CanDriver*>(user_ctx);
    self->state_.store(event->new_sta, std::memory_order_release);
    self->last_transition_tick_.store(xTaskGetTickCountFromISR(), std::memory_order_relaxed);
    if (event->new_sta == TWAI_ERROR_BUS_OFF) {
        self->bus_off_started_tick_.store(xTaskGetTickCountFromISR(), std::memory_order_relaxed);
        self->consecutive_bus_offs_.fetch_add(1, std::memory_order_relaxed);
        self->tx_resume_not_before_us_.store(INT64_MAX, std::memory_order_release);
    } else if (event->old_sta == TWAI_ERROR_BUS_OFF
               && event->new_sta == TWAI_ERROR_ACTIVE) {
        self->recovery_in_progress_.store(false, std::memory_order_release);
        self->recovery_completed_pending_.store(true, std::memory_order_release);
        self->first_rx_pending_.store(true, std::memory_order_release);
        self->first_tx_pending_.store(true, std::memory_order_release);
    }
    return false;
}

}  // namespace can
