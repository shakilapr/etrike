// Safety monitor — E-stop, brake lever, heartbeat watchdog.

#include "safety_monitor.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace sys {
namespace {
constexpr const char* kTag = "safety";
using namespace cfg;
}

void SafetyMonitor::init() {
    m_last_hb_us = 0;
    gpio_set_direction(kEstopGpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kEstopGpio, GPIO_PULLUP_ONLY);
    gpio_set_direction(kBrakeLeverGpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kBrakeLeverGpio, GPIO_PULLUP_ONLY);
    ESP_LOGI(kTag, "estop=%d brake_lever=%d", kEstopGpio, kBrakeLeverGpio);
}

bool SafetyMonitor::estop_active() const {
    return gpio_get_level(kEstopGpio) == 0;  // active-low
}

bool SafetyMonitor::brake_lever_pressed() const {
    return gpio_get_level(kBrakeLeverGpio) == 0;  // active-low
}

void SafetyMonitor::feed_heartbeat() {
    m_last_hb_us = esp_timer_get_time();
}

bool SafetyMonitor::heartbeat_ok() const {
    int64_t now = esp_timer_get_time();
    int64_t elapsed = (now - m_last_hb_us) / 1000;
    return elapsed < kJetsonHbTimeoutMs;
}

}  // namespace sys
