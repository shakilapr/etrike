#pragma once

#include <cstdint>
#include <cstring>
#include "driver/gpio.h"
#include "driver/twai.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

namespace pwr {

class CanDriver {
public:
    CanDriver() = default;
    ~CanDriver();

    bool init(int tx_gpio, int rx_gpio, int bitrate_hz, bool self_test = false);
    bool send(const can::Frame& frame, uint32_t timeout_ms = 10);
    void check_bus_recovery();

    bool is_initialized() const { return m_initialized; }

private:
    bool m_initialized{false};
    bool m_self_test{false};
};

} // namespace pwr

// Global driver instance and send helper
extern pwr::CanDriver g_can;
bool send_can_frame(const can::Frame& frame);
