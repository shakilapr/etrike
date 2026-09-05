#pragma once
// Stream validity supervisor for CAN rolling-counter frames.
//
// Implements the locked plan semantics:
//   delta == 1 -> accept + refresh freshness
//   delta == 2 -> accept + record one missed frame + refresh freshness
//   delta == 0 -> reject duplicate; DO NOT refresh freshness
//   delta >  2 -> sequence fault -> authority invalid
// Reacquisition (after timeout/staleness OR sequence fault):
//   first fresh frame establishes a baseline (still invalid);
//   the next correctly-advancing frame restores authority.
//
// Timeout value is an implementation decision (pass cycle*N in ticks).
#include <cstdint>
#include "protocol/core/supervision.hpp"

namespace etrike::protocol {

class StreamValidity {
public:
    // modulus: counter modulus (256 for an 8-bit rolling counter).
    // max_forward_delta: accepted forward jump (2 => tolerate one missed frame).
    explicit StreamValidity(std::uint32_t modulus = 256,
                            std::uint32_t max_forward_delta = 2) noexcept
        : modulus_(modulus), max_fwd_(max_forward_delta) {}

    // Bind this supervisor to a stream and configure its trackers. Call ONCE
    // (not per frame): calling again resets stream state.
    void set_key(std::uint32_t bus, std::uint32_t can_id, std::uint64_t timeout,
                 std::uint32_t producer = 0, std::uint64_t epoch = 0) noexcept {
        bus_ = bus; can_id_ = can_id; producer_ = producer; epoch_ = epoch;
        timeout_ = timeout;
        counters_.configure(key(), CounterConfig{modulus_, max_fwd_});
        fresh_.configure(key(), timeout_);
        valid_ = false;
    }

    // Observe a new frame. `counter` is the decoded rolling counter,
    // `now` is the current monotonic clock (e.g. FreeRTOS ticks).
    // Returns the current authority-valid state.
    bool observe(std::uint8_t counter, std::uint64_t now) noexcept {
        CounterResult cr = counters_.observe(key(), counter);

        if (!valid_) {
            // Reacquiring: baseline frame (First/Reset) keeps authority invalid;
            // a clean advancing frame restores it.
            if (cr.event == CounterEvent::Increment || cr.event == CounterEvent::Wrap ||
                cr.event == CounterEvent::Gap || cr.event == CounterEvent::Recovery) {
                valid_ = true;
            }
            return valid_;
        }

        const bool counter_fault = (cr.event == CounterEvent::Reorder);  // delta > max_fwd
        const bool duplicate = (cr.event == CounterEvent::Duplicate ||
                                cr.event == CounterEvent::Frozen);      // delta == 0

        if (duplicate) {
            // Reject duplicate but DO NOT refresh freshness.
            FreshnessResult fr = fresh_.check(key(), now);
            if (fr.event == FreshnessEvent::Expired || fr.event == FreshnessEvent::Stale) {
                invalidate();
            }
            return valid_;
        }

        FreshnessResult fr = fresh_.observe(key(), now);
        const bool fresh_ok = (fr.event != FreshnessEvent::Expired &&
                               fr.event != FreshnessEvent::Stale);
        if (counter_fault || !fresh_ok) {
            invalidate();
        }
        return valid_;
    }

    bool valid() const noexcept { return valid_; }

    // Force the stream into the invalid/reacquiring state (e.g. on a detected fault).
    void invalidate_now() noexcept { invalidate(); }

private:
    TrackerKey key() const noexcept { return TrackerKey{bus_, can_id_, producer_, epoch_}; }

    void invalidate() noexcept {
        counters_.reset(key());
        fresh_.reset(key());
        valid_ = false;
    }

    std::uint32_t modulus_;
    std::uint32_t max_fwd_;
    std::uint64_t timeout_{0};
    bool valid_ = false;
    std::uint32_t bus_ = 0;
    std::uint32_t can_id_ = 0;
    std::uint32_t producer_ = 0;
    std::uint64_t epoch_ = 0;
    CounterTracker counters_{};
    FreshnessTracker fresh_{0};
};

}  // namespace etrike::protocol
