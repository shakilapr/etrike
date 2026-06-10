#pragma once
// Manual throttle — ADC input, active only in MANUAL mode.
// In AUTO mode, readings are telemetry only.

#include <cstdint>

namespace sys {

class ThrottleInput {
public:
    ThrottleInput() = default;

    void init();                       // Configure ADC1_CH5
    int32_t read_mmps();               // Thread-safe: most recent reading
    void poll();                       // Read ADC, update value (call @ 100 Hz)

private:
    int32_t m_speed_mmps = 0;
};

}  // namespace sys
