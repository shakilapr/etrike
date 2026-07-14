/*
 * twai_stubs.cpp — Host implementation of ESP-IDF TWAI API.
 *
 * Routes all TWAI calls through the global VirtualCanBus singleton.
 * On real hardware, these map to the ESP32-S3 TWAI peripheral registers.
 */
#include "driver/twai.h"
#include "can/virtual_can_bus.h"
#include "protocol/core/frame.hpp"
#include <cstdio>
#include <mutex>

/* ── global CAN bus instances ──────────────────────────────────── */
static can::sim::VirtualCanBus* g_low_bus  = nullptr;
static can::sim::VirtualCanBus* g_high_bus = nullptr;
static int g_instance_count = 0;
static std::mutex g_bus_mutex;

void twai_set_low_bus(can::sim::VirtualCanBus* bus)  { g_low_bus  = bus; }
void twai_set_high_bus(can::sim::VirtualCanBus* bus) { g_high_bus = bus; }

static can::sim::VirtualCanBus* active_bus() {
    // First installed → low bus; second → high bus
    std::lock_guard<std::mutex> lock(g_bus_mutex);
    if (g_instance_count == 0) return g_low_bus;
    if (g_instance_count == 1) return g_high_bus;
    return g_low_bus;  // fallback
}

/* ── API implementations ────────────────────────────────────────── */

extern "C" {

int twai_driver_install(const twai_general_config_t* g,
                        const twai_timing_config_t*,
                        const twai_filter_config_t*) {
    if (!g) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(g_bus_mutex);
    g_instance_count++;
    printf("[twai] driver installed (controller %d, TX=%d, RX=%d)\n",
           g->controller_id, g->tx_io, g->rx_io);
    return ESP_OK;
}

int twai_driver_uninstall(void) {
    printf("[twai] driver uninstalled\n");
    return ESP_OK;
}

int twai_start(void) {
    printf("[twai] started\n");
    return ESP_OK;
}

int twai_stop(void) {
    printf("[twai] stopped\n");
    return ESP_OK;
}

int twai_transmit(const twai_message_t* msg, int timeout_ms) {
    if (!msg) return ESP_ERR_INVALID_ARG;

    etrike::protocol::Frame frame;
    frame.id       = msg->identifier;
    frame.extended = msg->extd;
    frame.dlc      = msg->data_length_code;
    for (int i = 0; i < (int)msg->data_length_code && i < 8; i++)
        frame.data[i] = msg->data[i];

    auto* bus = active_bus();
    if (!bus) return ESP_FAIL;

    bool ok = bus->send(frame, (uint32_t)timeout_ms);
    return ok ? ESP_OK : ESP_FAIL;
}

int twai_receive(twai_message_t* msg, int timeout_ms) {
    if (!msg) return ESP_ERR_INVALID_ARG;

    auto* bus = active_bus();
    if (!bus) return ESP_ERR_TIMEOUT;

    etrike::protocol::Frame frame;
    if (!bus->receive(frame, (uint32_t)timeout_ms))
        return ESP_ERR_TIMEOUT;

    msg->identifier       = frame.id;
    msg->extd             = frame.extended ? 1 : 0;
    msg->data_length_code = frame.dlc;
    for (int i = 0; i < frame.dlc && i < 8; i++)
        msg->data[i] = frame.data[i];
    msg->self = 0;
    msg->ss   = 0;
    return ESP_OK;
}

int twai_get_status_info(twai_status_info_t* info) {
    if (!info) return ESP_ERR_INVALID_ARG;

    auto* bus = active_bus();
    if (!bus) {
        info->tx_error_counter = 0;
        info->rx_error_counter = 0;
    } else {
        uint8_t tec, rec;
        bus->get_error_counters(tec, rec);
        info->tx_error_counter = tec;
        info->rx_error_counter = rec;
    }
    info->msgs_to_tx      = 0;
    info->msgs_to_rx      = 0;
    info->tx_failed_count = 0;
    info->rx_missed_count = 0;
    info->rx_overrun_count = 0;
    info->arb_lost_count  = 0;
    info->bus_error_count = 0;
    info->state           = TWAI_STATE_RUNNING;
    return ESP_OK;
}

} // extern "C"
