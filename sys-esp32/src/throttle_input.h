#pragma once
// Throttle ADC input — 0-5V via voltage divider on GPIO10 (ADC1_CH5).
// Architecture.md §8.6: 12-bit, dead zone 200, maps to 0-3000 mm/s.

#include <cstdint>
#include "config.h"

namespace sys {

class ThrottleInput {
public:
    void init();

    // Poll ADC (call @ 100 Hz). Stores result internally.
    void poll();

    // Last polled speed in mm/s.
    int32_t read_mmps() const;

    // Host-testable tick: pass raw ADC value, get mapped speed.
    int16_t tick(uint16_t raw_adc) {
        if (raw_adc < kThrottleDeadZone) raw_adc = 0;
        return int16_t((int32_t(raw_adc) * kThrottleMaxSpeedMmps) / 4095);
    }
};

}  // namespace sys
