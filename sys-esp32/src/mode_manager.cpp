// Mode state machine — implementation.

#include "mode_manager.h"
#include "config.h"
#include <atomic>
#include "driver/gpio.h"
#include "esp_log.h"

namespace sys {
namespace {

constexpr const char* kTag = "mode";
std::atomic<int> g_mode{static_cast<int>(can::Mode::Manual)};

}

void ModeManager::init() {
    g_mode.store(static_cast<int>(can::Mode::Manual), std::memory_order_relaxed);
    m_prev_switch = false;
    gpio_set_direction(kModeSwitchGpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kModeSwitchGpio, GPIO_PULLUP_ONLY);
    ESP_LOGI(kTag, "switch GPIO=%d", kModeSwitchGpio);
}

can::Mode ModeManager::current() const {
    return static_cast<can::Mode>(g_mode.load(std::memory_order_relaxed));
}

void ModeManager::set(can::Mode m) {
    // ESTOP cannot be cleared except by explicit reset
    auto cur = static_cast<can::Mode>(g_mode.load(std::memory_order_relaxed));
    if (cur == can::Mode::Estop && m != can::Mode::Estop) return;
    auto old = static_cast<can::Mode>(
        g_mode.exchange(static_cast<int>(m), std::memory_order_relaxed));
    if (old != m)
        ESP_LOGI(kTag, "%s → %s", can::mode_name(old), can::mode_name(m));
}

void ModeManager::poll() {
    auto cur = current();
    if (cur == can::Mode::Estop) return;  // switch ignored in ESTOP

    bool sw = (gpio_get_level(kModeSwitchGpio) == 0);  // LOW = Auto (switch closed)
    if (sw != m_prev_switch) {
        set(sw ? can::Mode::Auto : can::Mode::Manual);
        m_prev_switch = sw;
    }
}

}  // namespace sys
