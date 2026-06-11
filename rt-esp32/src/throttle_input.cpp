// Manual throttle ADC — implementation.

#include "throttle_input.h"
#include "config.h"
#include <atomic>
#include "driver/adc.h"
#include "esp_log.h"

namespace sys {
namespace {
using namespace cfg;

constexpr const char* kTag = "thr";
std::atomic<int32_t> g_throttle_mmps{0};

}

void ThrottleInput::init() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(
        static_cast<adc1_channel_t>(kThrottleAdcChannel), ADC_ATTEN_DB_12);
    ESP_LOGI(kTag, "ADC ch=%d deadzone=%u", kThrottleAdcChannel, kThrottleDeadZone);
}

int32_t ThrottleInput::read_mmps() const {
    return g_throttle_mmps.load(std::memory_order_relaxed);
}

void ThrottleInput::poll() {
    int raw = adc1_get_raw(static_cast<adc1_channel_t>(kThrottleAdcChannel));
    if (raw < static_cast<int>(kThrottleDeadZone)) raw = 0;
    int32_t speed = static_cast<int64_t>(raw) * kThrottleMaxSpeedMmps / 4095;
    g_throttle_mmps.store(speed, std::memory_order_relaxed);
}

}  // namespace sys
