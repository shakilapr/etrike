#pragma once
// External watchdog toggle — toggles a GPIO at a fixed rate.
// If this task stops, the external watchdog IC (TPS3850) resets the MCU.
// Pattern reused from SYS ESP32-S3.

#include "driver/gpio.h"
#include "config.h"

namespace pwt {

class WdtToggle {
public:
    void init() {
        m_state = false;
        gpio_set_direction(static_cast<gpio_num_t>(kWdtToggleGpio), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(kWdtToggleGpio), 0);
    }

    bool tick() {
        m_state = !m_state;
        gpio_set_level(static_cast<gpio_num_t>(kWdtToggleGpio), m_state ? 1 : 0);
        return m_state;
    }

private:
    bool m_state = false;
};

} // namespace pwt
