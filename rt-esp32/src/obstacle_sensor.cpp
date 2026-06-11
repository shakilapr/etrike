// HC-SR04 ultrasonic sensor — obstacle distance measurement.

#include "obstacle_sensor.h"
#include "config.h"
#include <atomic>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace rt {
namespace {
using namespace cfg;

constexpr const char* kTag = "obs";
std::atomic<unsigned> g_distance_mm{UINT32_MAX};

}

void ObstacleSensor::init() {
    gpio_set_direction(kObstacleTrigGpio, GPIO_MODE_OUTPUT);
    gpio_set_direction(kObstacleEchoGpio, GPIO_MODE_INPUT);
    gpio_set_level(kObstacleTrigGpio, 0);
    ESP_LOGI(kTag, "TRIG=%d ECHO=%d", kObstacleTrigGpio, kObstacleEchoGpio);
}

unsigned ObstacleSensor::distance_mm() const {
    return g_distance_mm.load(std::memory_order_relaxed);
}

void ObstacleSensor::poll() {
    // 10 µs trigger pulse
    gpio_set_level(kObstacleTrigGpio, 1);
    esp_rom_delay_us(10);
    gpio_set_level(kObstacleTrigGpio, 0);

    // Wait for echo to go HIGH (with timeout)
    int64_t deadline = esp_timer_get_time() + kTimeoutUs;
    while (!gpio_get_level(kObstacleEchoGpio))
        if (esp_timer_get_time() > deadline) return;  // timeout

    // Measure HIGH pulse width
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(kObstacleEchoGpio))
        if (esp_timer_get_time() > deadline) return;  // timeout
    int64_t t1 = esp_timer_get_time();

    // HC-SR04: distance_mm = pulse_us * 343 / 2000 ≈ pulse_us * 1715 / 10000
    unsigned d = static_cast<unsigned>((t1 - t0) * 1715 / 10000);
    g_distance_mm.store(d, std::memory_order_relaxed);

    ESP_LOGD(kTag, "%u mm", d);
}

}  // namespace rt
