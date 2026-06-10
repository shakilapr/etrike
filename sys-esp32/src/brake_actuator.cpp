// Brake actuator — implementation.

#include "brake_actuator.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace sys {
namespace {
constexpr const char* kTag = "brake";
}

void BrakeActuator::init() {
    gpio_set_direction(kBrakeGpio, GPIO_MODE_OUTPUT);
    release();
    ESP_LOGI(kTag, "GPIO=%d", kBrakeGpio);
}

void BrakeActuator::engage() {
    gpio_set_level(kBrakeGpio, 1);
    m_engaged = true;
}

void BrakeActuator::release() {
    gpio_set_level(kBrakeGpio, 0);
    m_engaged = false;
}

}  // namespace sys
