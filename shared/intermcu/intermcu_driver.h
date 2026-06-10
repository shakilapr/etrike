#pragma once
// UART-backed transport for the direct RT<->SYS inter-MCU protocol.

#include "intermcu_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <cstddef>

namespace inter_mcu {

class InterMcuDriver {
public:
    struct Config {
        int uart_port = 1;
        int tx_gpio = 12;
        int rx_gpio = 13;
        int baud_rate = 2'000'000;
    };

    explicit InterMcuDriver(const Config& cfg = {})
        : m_config(cfg) {}

    InterMcuDriver(const InterMcuDriver&) = delete;
    InterMcuDriver& operator=(const InterMcuDriver&) = delete;

    bool init() {
        uart_config_t uart_config = {};
        uart_config.baud_rate = m_config.baud_rate;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.source_clk = UART_SCLK_DEFAULT;

        const auto port = static_cast<uart_port_t>(m_config.uart_port);
        if (uart_param_config(port, &uart_config) != ESP_OK) return false;
        if (uart_set_pin(port, m_config.tx_gpio, m_config.rx_gpio,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
            return false;
        }
        if (uart_driver_install(port, 2048, 2048, 0, nullptr, 0) != ESP_OK) {
            return false;
        }

        ESP_LOGI("intermcu", "UART%d TX=%d RX=%d @ %d baud",
                 m_config.uart_port, m_config.tx_gpio, m_config.rx_gpio,
                 m_config.baud_rate);
        m_initialized = true;
        return true;
    }

    bool is_initialized() const { return m_initialized; }

    bool send(Frame frame, TickType_t timeout_ms = 10) {
        if (frame.dlc > kMaxPayload) return false;
        frame.seq = m_next_seq++;

        uint8_t packet[6 + kMaxPayload] = {};
        packet[0] = kSof0;
        packet[1] = kSof1;
        packet[2] = static_cast<uint8_t>(frame.type);
        packet[3] = frame.seq;
        packet[4] = frame.dlc;
        for (int i = 0; i < frame.dlc; ++i) {
            packet[5 + i] = frame.data[i];
        }
        packet[5 + frame.dlc] = crc8(&packet[2], 3 + frame.dlc);

        const int len = 6 + frame.dlc;
        const auto port = static_cast<uart_port_t>(m_config.uart_port);
        const int written = uart_write_bytes(
            port, reinterpret_cast<const char*>(packet), len);
        if (written != len) return false;
        return uart_wait_tx_done(port, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    }

    bool receive(Frame& out, TickType_t timeout_ms = 10) {
        const auto port = static_cast<uart_port_t>(m_config.uart_port);
        const TickType_t ticks = pdMS_TO_TICKS(timeout_ms);

        uint8_t b = 0;
        while (read_exact(port, &b, 1, ticks)) {
            if (b != kSof0) continue;
            if (!read_exact(port, &b, 1, ticks)) return false;
            if (b != kSof1) continue;

            uint8_t header[3] = {};
            if (!read_exact(port, header, sizeof(header), ticks)) return false;
            const uint8_t dlc = header[2];
            if (dlc > kMaxPayload) return false;

            uint8_t payload_and_crc[kMaxPayload + 1] = {};
            if (!read_exact(port, payload_and_crc, dlc + 1, ticks)) return false;

            uint8_t crc_data[3 + kMaxPayload] = {};
            crc_data[0] = header[0];
            crc_data[1] = header[1];
            crc_data[2] = header[2];
            for (int i = 0; i < dlc; ++i) {
                crc_data[3 + i] = payload_and_crc[i];
            }
            if (crc8(crc_data, 3 + dlc) != payload_and_crc[dlc]) return false;

            out.type = static_cast<MessageType>(header[0]);
            out.seq = header[1];
            out.dlc = dlc;
            for (int i = 0; i < dlc; ++i) {
                out.data[i] = payload_and_crc[i];
            }
            return true;
        }

        return false;
    }

private:
    static bool read_exact(uart_port_t port, uint8_t* data, size_t len, TickType_t ticks) {
        size_t offset = 0;
        while (offset < len) {
            const int got = uart_read_bytes(port, data + offset, len - offset, ticks);
            if (got <= 0) return false;
            offset += static_cast<size_t>(got);
        }
        return true;
    }

    Config m_config;
    bool m_initialized = false;
    uint8_t m_next_seq = 0;
};

}  // namespace inter_mcu
