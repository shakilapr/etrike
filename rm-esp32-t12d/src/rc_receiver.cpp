#include "rc_receiver.h"
#include "esp_log.h"

static const char* TAG = "rc_rx";

namespace rm {

RcReceiver::~RcReceiver() {
    uart_driver_delete(static_cast<uart_port_t>(kSbusUartPort));
}

bool RcReceiver::init() {
    uart_config_t uart_config = {};
    uart_config.baud_rate  = kSbusBaudRate;
    uart_config.data_bits  = UART_DATA_8_BITS;
    uart_config.parity     = UART_PARITY_EVEN;
    uart_config.stop_bits  = UART_STOP_BITS_2;
    uart_config.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_param_config(static_cast<uart_port_t>(kSbusUartPort), &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(static_cast<uart_port_t>(kSbusUartPort),
                       UART_PIN_NO_CHANGE, // TX (unused)
                       kSbusRxGpio,        // RX
                       UART_PIN_NO_CHANGE, // RTS
                       UART_PIN_NO_CHANGE);// CTS
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        return false;
    }

    // Install UART driver with 1024-byte RX buffer
    err = uart_driver_install(static_cast<uart_port_t>(kSbusUartPort), 1024, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        return false;
    }

    // Hardware signal inversion for SBUS active-low protocol
    if (kSbusInverted) {
        err = uart_set_line_inverse(static_cast<uart_port_t>(kSbusUartPort), UART_SIGNAL_RXD_INV);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART set line inverse failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        latest_frame_.channels[i] = kSbusRawCenter;
    }

    // Initialize snapshot to safe failsafe state
    snap_ = decode_sbus_frame(latest_frame_, 0, 0);

    ESP_LOGI(TAG, "Initialized SBUS UART%d on RX GPIO %d (100k, 8E2, inverted)", kSbusUartPort, kSbusRxGpio);
    return true;
}

void RcReceiver::sample(uint32_t now_ms) {
    uint8_t rx_buf[128];
    // Reactive 10ms timeout: wakes immediately when SBUS bytes arrive
    int len = uart_read_bytes(static_cast<uart_port_t>(kSbusUartPort), rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(10));
    bool new_frame_decoded = false;

    if (len > 0) {
        for (int i = 0; i < len; ++i) {
            if (parser_.parse_byte(rx_buf[i], latest_frame_)) {
                new_frame_decoded = true;
            }
        }
    }

    if (new_frame_decoded) {
        last_frame_time_ms_.store(now_ms, std::memory_order_release);
    }

    uint32_t last_frame_ms = last_frame_time_ms_.load(std::memory_order_acquire);
    RcSnapshot new_snap = decode_sbus_frame(latest_frame_, last_frame_ms, now_ms);

    // Seqlock atomic publication
    seq_.fetch_add(1, std::memory_order_release);
    snap_ = new_snap;
    seq_.fetch_add(1, std::memory_order_release);
}

RcSnapshot RcReceiver::snapshot() const {
    RcSnapshot copy;
    uint32_t s1 = 0, s2 = 0;
    do {
        s1 = seq_.load(std::memory_order_acquire);
        copy = snap_;
        s2 = seq_.load(std::memory_order_acquire);
    } while ((s1 & 1) != 0 || s1 != s2);
    return copy;
}

}  // namespace rm
