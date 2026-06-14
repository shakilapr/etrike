#pragma once
// Throttle ADC input — 0-5V via voltage divider on GPIO10 (ADC1_CH5).
// Architecture.md §8.6: 12-bit, dead zone 200, maps to 0-3000 mm/s.

#include <cstdint>
#include "config.h"

namespace sys {

class ThrottleInput {
public:
    void init() { m_speed_mmps = 0; }

    // Call @ 100 Hz. raw_adc: 0-4095. Returns mapped speed in mm/s.
    int16_t tick(uint16_t raw_adc) {
        if (raw_adc < kThrottleDeadZone) raw_adc = 0;
        m_speed_mmps = int16_t((int32_t(raw_adc) * kThrottleMaxSpeedMmps) / 4095);
        return m_speed_mmps;
    }
    int16_t speed_mmps() const { return m_speed_mmps; }
private:
    int16_t m_speed_mmps = 0;
};

}  // namespace sys
