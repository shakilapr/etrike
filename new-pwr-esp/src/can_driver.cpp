#include "can_driver.h"
#include "esp_log.h"

pwr::CanDriver g_can;

bool send_can_frame(const can::Frame& frame) {
    return g_can.send(frame);
}

namespace pwr {

CanDriver::~CanDriver() {
    if (m_initialized) {
        twai_stop();
        twai_driver_uninstall();
        m_initialized = false;
    }
}

bool CanDriver::init(int tx_gpio, int rx_gpio, int bitrate_hz, bool self_test) {
    m_self_test = self_test;

    // If using UART0 pins (GPIO 1 / GPIO 3), release them from UART0 driver
    if (tx_gpio == 1 || rx_gpio == 3 || tx_gpio == 3 || rx_gpio == 1) {
        uart_driver_delete(UART_NUM_0);
        gpio_reset_pin(static_cast<gpio_num_t>(tx_gpio));
        gpio_reset_pin(static_cast<gpio_num_t>(rx_gpio));
    }

    twai_mode_t mode = self_test ? TWAI_MODE_NO_ACK : TWAI_MODE_NORMAL;
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(tx_gpio),
        static_cast<gpio_num_t>(rx_gpio),
        mode
    );
    g_config.tx_queue_len = 5;
    g_config.rx_queue_len = 5;

    twai_timing_config_t t_config;
    if (bitrate_hz == 500000) {
        t_config = TWAI_TIMING_CONFIG_500KBITS();
    } else {
        // Canonical 250 kbps
        t_config = TWAI_TIMING_CONFIG_250KBITS();
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }

    m_initialized = true;
    return true;
}

void CanDriver::check_bus_recovery() {
    if (!m_initialized) return;

    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        if (status.state == TWAI_STATE_BUS_OFF) {
            twai_initiate_recovery();
        } else if (status.state == TWAI_STATE_STOPPED) {
            twai_start();
        }
    }
}

bool CanDriver::send(const can::Frame& frame, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    check_bus_recovery();

    twai_message_t msg{};
    msg.identifier = frame.id;
    msg.extd = frame.extended ? 1 : 0;
    msg.rtr = 0;
    msg.data_length_code = frame.dlc;
    msg.ss = m_self_test ? 1 : 0;

    for (size_t i = 0; i < frame.dlc && i < 8; ++i) {
        msg.data[i] = frame.data[i];
    }

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(timeout_ms));
    return (err == ESP_OK);
}

} // namespace pwr
