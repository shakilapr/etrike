#pragma once
// HC-SR04 ultrasonic obstacle sensor.
// Distance reported via CAN 0x400 and shared atomic for control loop.

#include <cstdint>

namespace rt {

class ObstacleSensor {
public:
    ObstacleSensor() = default;

    void init();                         // Configure TRIG/ECHO GPIOs
    unsigned distance_mm() const;        // Thread-safe: most recent reading
    void poll();                         // Single poll cycle (called from obstacle_task @ 10 Hz)

private:
    static constexpr int kTimeoutUs  = 30'000;   // ~5m max range
    static constexpr int kIntervalMs =    100;

    unsigned m_distance_mm = UINT32_MAX;  // UINT32_MAX = no reading yet
};

}  // namespace rt
