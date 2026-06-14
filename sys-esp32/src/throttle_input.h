#pragma once
// Throttle ADC — 0-5V via voltage divider on GPIO10 (ADC1_CH5).
// Architecture.md §8.6: 12-bit, dead zone 200, maps to 0-3000 mm/s.
#include <cstdint>
#include "config.h"
namespace sys {
class ThrottleInput {
public:
    void init() { m_speed_mmps = 0; }
    void poll() {} // ESP-IDF: adc1_get_raw(ADC1_CH5), map, store
    int32_t read_mmps() const { return m_speed_mmps; }
    int16_t tick(uint16_t raw_adc) {
        if (raw_adc < kThrottleDeadZone) raw_adc = 0;
        m_speed_mmps = int16_t((int32_t(raw_adc) * kThrottleMaxSpeedMmps) / 4095);
        return m_speed_mmps;
    }
private:
    int16_t m_speed_mmps = 0;
};
}
