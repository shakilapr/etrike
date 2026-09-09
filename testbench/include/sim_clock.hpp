#pragma once

#include <cstdint>
#include <functional>

namespace testbench {

class SimClock {
public:
    explicit SimClock(uint32_t start_ms = 0)
        : now_ms_(start_ms) {}

    uint32_t now_ms() const { return now_ms_; }
    uint64_t now_us() const { return static_cast<uint64_t>(now_ms_) * 1000; }

    void advance_ms(uint32_t dt_ms) {
        now_ms_ += dt_ms;
    }

    void reset(uint32_t start_ms = 0) {
        now_ms_ = start_ms;
    }

private:
    uint32_t now_ms_{0};
};

} // namespace testbench
