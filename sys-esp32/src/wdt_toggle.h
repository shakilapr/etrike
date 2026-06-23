#pragma once
// External watchdog toggle. Architecture.md §8.6. Toggle GPIO23 at 20 Hz.
#include "driver/gpio.h"
#include "config.h"
namespace sys {
class WdtToggle {
public:
    void init() { m_state=false; gpio_set_level(static_cast<gpio_num_t>(sys::kWdtToggleGpio), 0); }
    bool tick() {
        m_state = !m_state;
        gpio_set_level(static_cast<gpio_num_t>(sys::kWdtToggleGpio), m_state ? 1 : 0);
        return m_state;
    }
private:
    bool m_state=false;
};
}
