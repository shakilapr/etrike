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
        raw_sbus_[i].store(kSbusRawCenter, std::memory_order_relaxed);
        pulse_us_[i].store(kPulseCenterUs, std::memory_order_relaxed);
        latest_frame_.channels[i] = kSbusRawCenter;
    }

    ESP_LOGI(TAG, "Initialized SBUS UART%d on RX GPIO %d (100k, 8E2, inverted)", kSbusUartPort, kSbusRxGpio);
    return true;
}

void RcReceiver::sample(uint32_t now_ms) {
    uint8_t rx_buf[128];
    int len = uart_read_bytes(static_cast<uart_port_t>(kSbusUartPort), rx_buf, sizeof(rx_buf), 0);
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
        for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
            raw_sbus_[i].store(latest_frame_.channels[i], std::memory_order_relaxed);
            pulse_us_[i].store(sbus_to_pulse_us(latest_frame_.channels[i]), std::memory_order_relaxed);
        }
    }

    uint32_t last_frame_ms = last_frame_time_ms_.load(std::memory_order_acquire);
    RcSnapshot snap = decode_sbus_frame(latest_frame_, last_frame_ms, now_ms);

    snap_steering_.store(snap.steering_deg, std::memory_order_relaxed);
    snap_brake_.store(snap.brake_stroke_mm, std::memory_order_relaxed);
    snap_throttle_norm_.store(snap.throttle_norm, std::memory_order_relaxed);
    snap_yaw_spare_.store(snap.yaw_spare, std::memory_order_relaxed);
    snap_ignition_.store(snap.ignition, std::memory_order_relaxed);
    snap_gear_.store(snap.gear, std::memory_order_relaxed);
    snap_switch_a_.store(snap.switch_a, std::memory_order_relaxed);
    snap_switch_d_.store(snap.switch_d, std::memory_order_relaxed);
    snap_dial_vra_.store(snap.dial_vra, std::memory_order_relaxed);
    snap_dial_vrb_.store(snap.dial_vrb, std::memory_order_relaxed);
    snap_frame_lost_.store(snap.frame_lost, std::memory_order_relaxed);
    snap_failsafe_.store(snap.failsafe, std::memory_order_relaxed);
    snap_valid_.store(snap.signal_valid, std::memory_order_relaxed);
    snap_last_update_ms_.store(snap.last_update_ms, std::memory_order_relaxed);
}

RcSnapshot RcReceiver::snapshot() const {
    RcSnapshot snap;
    snap.steering_deg    = snap_steering_.load(std::memory_order_relaxed);
    snap.brake_stroke_mm = snap_brake_.load(std::memory_order_relaxed);
    snap.throttle_norm   = snap_throttle_norm_.load(std::memory_order_relaxed);
    snap.yaw_spare       = snap_yaw_spare_.load(std::memory_order_relaxed);
    snap.ignition        = snap_ignition_.load(std::memory_order_relaxed);
    snap.gear            = snap_gear_.load(std::memory_order_relaxed);
    snap.switch_a        = snap_switch_a_.load(std::memory_order_relaxed);
    snap.switch_d        = snap_switch_d_.load(std::memory_order_relaxed);
    snap.dial_vra        = snap_dial_vra_.load(std::memory_order_relaxed);
    snap.dial_vrb        = snap_dial_vrb_.load(std::memory_order_relaxed);
    snap.frame_lost      = snap_frame_lost_.load(std::memory_order_relaxed);
    snap.failsafe        = snap_failsafe_.load(std::memory_order_relaxed);
    snap.signal_valid    = snap_valid_.load(std::memory_order_relaxed);
    snap.last_update_ms  = snap_last_update_ms_.load(std::memory_order_relaxed);

    for (uint8_t i = 0; i < kNumSbusChannels; ++i) {
        snap.raw_channels[i] = raw_sbus_[i].load(std::memory_order_relaxed);
        snap.pulse_us[i]     = pulse_us_[i].load(std::memory_order_relaxed);
    }
    return snap;
}

}  // namespace rm
