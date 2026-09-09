#pragma once
// Deterministic monotonic clock for the full-system simulator.
//
// All nodes read time exclusively through this clock (advance_us() is the only
// way time moves). Firmware esp_timer_get_time() maps onto now_us(); never
// sleep/block inside the simulated loop.
#include <cstdint>

namespace sim {

class SimClock {
public:
    int64_t now_us() const noexcept { return now_us_; }

    void advance_us(int64_t delta_us) noexcept {
        if (delta_us > 0) now_us_ += delta_us;
    }

    void reset(int64_t start_us = 0) noexcept { now_us_ = start_us; }

private:
    int64_t now_us_ = 0;
};

}  // namespace sim
