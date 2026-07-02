#pragma once
// Standalone TWAI CAN driver for PWT — extended frame support.
// Does NOT depend on shared/ — self-contained like a third-party ECU.

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <cstring>

namespace pwt {

// ── CAN frame (extended-capable) ────────────────────────────────────

struct PwtFrame {
    uint32_t id       = 0;
    bool     extended = false;
    uint8_t  dlc      = 0;
    uint8_t  data[8]  = {};

    static PwtFrame make_ext(uint32_t id, uint8_t dlc) {
        PwtFrame f;
        f.id       = id;
        f.extended = true;
        f.dlc      = dlc;
        return f;
    }

    void set_u8(int offset, uint8_t v)  { if (offset < 8) data[offset] = v; }
    uint8_t u8_at(int offset) const     { return (offset < 8) ? data[offset] : 0; }
};

// ── CAN driver ──────────────────────────────────────────────────────

class CanDriver {
public:
    bool init(int tx_gpio, int rx_gpio, int bitrate_hz) {
        m_tx_gpio = tx_gpio;
        m_rx_gpio = rx_gpio;

        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(
            0,  // controller_num — single-controller node
            static_cast<gpio_num_t>(tx_gpio),
            static_cast<gpio_num_t>(rx_gpio),
            TWAI_MODE_NORMAL);

        twai_timing_config_t t = {};
        t.quanta_resolution_hz = 8'000'000;  // 80 MHz APB / 10 BRP

        if (bitrate_hz == 500'000) {
            // 8M / 500k = 16 TQ: 1 + 11 + 4 = 16
            t.tseg_1 = 11; t.tseg_2 = 4; t.sjw = 2;
        } else {
            // 250 kbit/s default
            // 8M / 250k = 32 TQ: 1 + 22 + 9 = 32 (sync=1, tseg1=22, tseg2=9)
            t.tseg_1 = 22; t.tseg_2 = 9; t.sjw = 3;
        }

        twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
        if (twai_start() != ESP_OK) return false;

        m_initialized = true;
        return true;
    }

    bool send(const PwtFrame& frame, TickType_t timeout_ms = 10) {
        if (!m_initialized) return false;

        twai_message_t msg = {};
        if (frame.extended) {
            msg.identifier          = frame.id;
            msg.extd                = 1;
            msg.data_length_code    = frame.dlc;
        } else {
            msg.identifier          = frame.id;
            msg.extd                = 0;
            msg.data_length_code    = frame.dlc;
        }
        msg.ss = 0;  // normal mode — hardware auto-retransmit on arbitration loss
        std::memcpy(msg.data, frame.data, frame.dlc);

        esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(timeout_ms));
        return err == ESP_OK;
    }

    bool receive(PwtFrame& out, TickType_t timeout_ms = 100) {
        if (!m_initialized) return false;

        twai_message_t msg = {};
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(timeout_ms));
        if (err != ESP_OK) return false;

        out.id       = msg.identifier;
        out.extended = msg.extd != 0;
        out.dlc      = msg.data_length_code;
        std::memcpy(out.data, msg.data, out.dlc);
        return true;
    }

    void get_error_counters(uint8_t& tec, uint8_t& rec) const {
        if (!m_initialized) { tec = 0; rec = 0; return; }
        twai_status_info_t info;
        twai_get_status_info(&info);
        tec = info.tx_error_counter;
        rec = info.rx_error_counter;
    }

    bool initialized() const { return m_initialized; }

private:
    bool m_initialized = false;
    int  m_tx_gpio = 7;
    int  m_rx_gpio = 6;
};

} // namespace pwt
