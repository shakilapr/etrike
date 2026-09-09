#pragma once
// Structured event trace for temporal assertions.
//
// Scenarios assert on trace events (never on wall-clock sleeps). A failing
// scenario dumps the whole trace so the causal chain is visible: e.g. the
// exact 0x011 zero frame that was rejected as a duplicate clear.
#include <cstdint>
#include <string>
#include <vector>

namespace sim {

struct TraceEvent {
    int64_t       time_us = 0;
    std::string   component;  // "host", "sys", "rt", "mtr"
    std::string   event;      // e.g. "estop_set", "tx_0x204"
    std::string   detail;     // e.g. "speed=2000 gear=D"
    int64_t       v1 = 0;     // optional numeric payload (for assertions)
};

class Trace {
public:
    void record(int64_t time_us, std::string component, std::string event,
                std::string detail = {}, int64_t v1 = 0) {
        events_.push_back({time_us, std::move(component), std::move(event),
                           std::move(detail), v1});
    }

    const std::vector<TraceEvent>& events() const noexcept { return events_; }

    // First index of a matching event for `component`/`event` (prefix match).
    // Returns -1 when never observed.
    int find(std::string_view component, std::string_view event) const noexcept {
        for (std::size_t i = 0; i < events_.size(); ++i) {
            if (events_[i].component.rfind(component, 0) == 0
                && events_[i].event.rfind(event, 0) == 0)
                return static_cast<int>(i);
        }
        return -1;
    }

    int count(std::string_view component, std::string_view event) const noexcept {
        int n = 0;
        for (const auto& e : events_) {
            if (e.component.rfind(component, 0) == 0
                && e.event.rfind(event, 0) == 0)
                ++n;
        }
        return n;
    }

    // Time of the LAST matching event, or -1.
    int64_t last_time_us(std::string_view component,
                         std::string_view event) const noexcept {
        int64_t t = -1;
        for (const auto& e : events_) {
            if (e.component.rfind(component, 0) == 0
                && e.event.rfind(event, 0) == 0)
                t = e.time_us;
        }
        return t;
    }

    void clear() noexcept { events_.clear(); }

private:
    std::vector<TraceEvent> events_;
};

}  // namespace sim
