/*
 * gpio_stubs.cpp — Host implementation: virtual pin array.
 *
 * 256 virtual GPIO pins. Each pin stores:
 *   - direction (IN/OUT/DISABLED)
 *   - output_value (what firmware wrote)
 *   - input_value  (what the test harness injected)
 */
#include "driver/gpio.h"
#include "esp_err.h"
#include <array>
#include <cstdio>
#include <mutex>

static std::mutex g_pin_mutex;

struct VirtualPin {
    gpio_mode_t mode   = GPIO_MODE_DISABLE;
    int         level  = 0;  // output value written by firmware
    int         input  = 0;  // injected input value for gpio_get_level
};

static std::array<VirtualPin, 256> g_pins;

extern "C" {

int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode) {
    if (pin < 0 || pin >= 256) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    g_pins[pin].mode = mode;
    return ESP_OK;
}

int gpio_set_level(gpio_num_t pin, int level) {
    if (pin < 0 || pin >= 256) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    g_pins[pin].level = level ? 1 : 0;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t pin) {
    if (pin < 0 || pin >= 256) return 0;
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    auto& p = g_pins[pin];
    if (p.mode == GPIO_MODE_INPUT || p.mode == GPIO_MODE_INPUT_OUTPUT)
        return p.input;
    return p.level;  // output: read the last written value
}

int gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t) {
    if (pin < 0 || pin >= 256) return ESP_ERR_INVALID_ARG;
    return ESP_OK;  // no-op on host
}

/* ── test harness API ──────────────────────────────────────────── */

void gpio_test_set_input(gpio_num_t pin, int level) {
    if (pin < 0 || pin >= 256) return;
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    g_pins[pin].input = level ? 1 : 0;
}

int gpio_test_get_output(gpio_num_t pin) {
    if (pin < 0 || pin >= 256) return 0;
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    return g_pins[pin].level;
}

void gpio_test_reset() {
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    for (auto& p : g_pins) {
        p.mode  = GPIO_MODE_DISABLE;
        p.level = 0;
        p.input = 0;
    }
}

} // extern "C"
