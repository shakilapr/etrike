// Command staleness watchdog — zero setpoint if Jetson goes silent.

#include "watchdog.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace rt {
namespace {
constexpr const char* kTag = "wd";
}

void Watchdog::init() {
    m_last_feed_us = esp_timer_get_time();
    m_tripped = false;
    ESP_LOGI(kTag, "timeout=%d ms", kCmdStaleTimeoutMs);
}

void Watchdog::feed() {
    m_last_feed_us = esp_timer_get_time();
    if (m_tripped) {
        m_tripped = false;
        ESP_LOGW(kTag, "resumed — commands received");
    }
}

bool Watchdog::is_stale() const {
    int64_t elapsed_ms = (esp_timer_get_time() - m_last_feed_us) / 1000;
    return elapsed_ms > kCmdStaleTimeoutMs;
}

}  // namespace rt
