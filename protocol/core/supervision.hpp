#pragma once

#include <cstdint>
#include <map>
#include <tuple>

namespace etrike::protocol {

// Numeric bus and producer tokens keep the core independent of any network enum.
struct TrackerKey {
    std::uint32_t bus{0};
    std::uint32_t can_id{0};
    std::uint32_t producer{0};
    std::uint64_t session_epoch{0};

    friend bool operator<(const TrackerKey& lhs, const TrackerKey& rhs) noexcept {
        return std::tie(lhs.bus, lhs.can_id, lhs.producer, lhs.session_epoch) <
               std::tie(rhs.bus, rhs.can_id, rhs.producer, rhs.session_epoch);
    }
};

struct CounterConfig {
    std::uint32_t modulus{16};
    // Larger modular jumps are classified as old/reordered samples.
    std::uint32_t max_forward_delta{4};
};

enum class CounterEvent : std::uint8_t {
    First,
    Increment,
    Wrap,
    Duplicate,
    Frozen,
    Gap,
    Reorder,
    Reset,
    Recovery,
    InvalidConfiguration,
    InvalidValue,
};

struct CounterResult {
    CounterEvent event{CounterEvent::First};
    std::uint32_t previous{0};
    std::uint32_t current{0};
    std::uint32_t missed{0};
    bool wrapped{false};
};

class CounterTracker {
public:
    explicit CounterTracker(CounterConfig default_config = {}) noexcept
        : default_config_(default_config) {}

    void configure(const TrackerKey& key, CounterConfig config) {
        Entry& entry = entries_[key];
        entry = Entry{};
        entry.config = config;
        entry.has_custom_config = true;
    }

    CounterResult observe(const TrackerKey& key, std::uint32_t value) {
        Entry& entry = entries_[key];
        const CounterConfig config = entry.has_custom_config ? entry.config : default_config_;
        CounterResult result{};
        result.current = value;

        if (config.modulus < 2 || config.max_forward_delta == 0 ||
            config.max_forward_delta >= config.modulus) {
            result.event = CounterEvent::InvalidConfiguration;
            return result;
        }
        if (value >= config.modulus) {
            result.event = CounterEvent::InvalidValue;
            return result;
        }
        if (entry.reset_pending) {
            entry.initialized = true;
            entry.reset_pending = false;
            entry.degraded = false;
            entry.last = value;
            entry.duplicate_count = 0;
            result.previous = value;
            result.event = CounterEvent::Reset;
            return result;
        }
        if (!entry.initialized) {
            entry.initialized = true;
            entry.last = value;
            entry.duplicate_count = 0;
            result.previous = value;
            result.event = CounterEvent::First;
            return result;
        }

        result.previous = entry.last;
        if (value == entry.last) {
            entry.degraded = true;
            ++entry.duplicate_count;
            result.event = entry.duplicate_count == 1 ? CounterEvent::Duplicate : CounterEvent::Frozen;
            return result;
        }

        entry.duplicate_count = 0;
        const std::uint32_t delta = (value + config.modulus - entry.last) % config.modulus;
        if (delta > config.max_forward_delta) {
            entry.degraded = true;
            result.event = CounterEvent::Reorder;
            return result;
        }

        result.wrapped = value < entry.last;
        result.missed = delta - 1u;
        entry.last = value;
        if (delta > 1u) {
            entry.degraded = true;
            result.event = CounterEvent::Gap;
            return result;
        }
        if (entry.degraded) {
            entry.degraded = false;
            result.event = CounterEvent::Recovery;
        } else {
            result.event = result.wrapped ? CounterEvent::Wrap : CounterEvent::Increment;
        }
        return result;
    }

    void reset(const TrackerKey& key) {
        Entry& entry = entries_[key];
        entry.initialized = false;
        entry.degraded = false;
        entry.reset_pending = true;
        entry.duplicate_count = 0;
    }

    void erase(const TrackerKey& key) { entries_.erase(key); }
    void clear() noexcept { entries_.clear(); }

private:
    struct Entry {
        CounterConfig config{};
        std::uint32_t last{0};
        std::uint32_t duplicate_count{0};
        bool initialized{false};
        bool degraded{false};
        bool reset_pending{false};
        bool has_custom_config{false};
    };

    CounterConfig default_config_;
    std::map<TrackerKey, Entry> entries_;
};

enum class FreshnessEvent : std::uint8_t {
    First,
    Refresh,
    Fresh,
    Expired,
    Stale,
    TimeReorder,
    Reset,
    Recovery,
    NeverSeen,
    InvalidConfiguration,
};

struct FreshnessResult {
    FreshnessEvent event{FreshnessEvent::NeverSeen};
    std::uint64_t elapsed{0};
};

class FreshnessTracker {
public:
    explicit FreshnessTracker(std::uint64_t default_timeout) noexcept
        : default_timeout_(default_timeout) {}

    void configure(const TrackerKey& key, std::uint64_t timeout) {
        Entry& entry = entries_[key];
        entry = Entry{};
        entry.timeout = timeout;
        entry.has_custom_timeout = true;
    }

    FreshnessResult observe(const TrackerKey& key, std::uint64_t now) {
        Entry& entry = entries_[key];
        const std::uint64_t timeout = entry.has_custom_timeout ? entry.timeout : default_timeout_;
        if (timeout == 0) return {FreshnessEvent::InvalidConfiguration, 0};
        if (entry.reset_pending) {
            entry.initialized = true;
            entry.reset_pending = false;
            entry.degraded = false;
            entry.stale = false;
            entry.last_seen = now;
            return {FreshnessEvent::Reset, 0};
        }
        if (!entry.initialized) {
            entry.initialized = true;
            entry.last_seen = now;
            return {FreshnessEvent::First, 0};
        }
        if (now < entry.last_seen) {
            entry.degraded = true;
            return {FreshnessEvent::TimeReorder, 0};
        }

        const std::uint64_t elapsed = now - entry.last_seen;
        const bool recovering = entry.degraded || entry.stale || elapsed > timeout;
        entry.last_seen = now;
        entry.degraded = false;
        entry.stale = false;
        return {recovering ? FreshnessEvent::Recovery : FreshnessEvent::Refresh, elapsed};
    }

    FreshnessResult check(const TrackerKey& key, std::uint64_t now) {
        auto found = entries_.find(key);
        if (found == entries_.end() || !found->second.initialized)
            return {FreshnessEvent::NeverSeen, 0};
        Entry& entry = found->second;
        const std::uint64_t timeout = entry.has_custom_timeout ? entry.timeout : default_timeout_;
        if (timeout == 0) return {FreshnessEvent::InvalidConfiguration, 0};
        if (now < entry.last_seen) {
            entry.degraded = true;
            return {FreshnessEvent::TimeReorder, 0};
        }
        const std::uint64_t elapsed = now - entry.last_seen;
        if (elapsed <= timeout) return {FreshnessEvent::Fresh, elapsed};
        const FreshnessEvent event = entry.stale ? FreshnessEvent::Stale : FreshnessEvent::Expired;
        entry.stale = true;
        return {event, elapsed};
    }

    void reset(const TrackerKey& key) {
        Entry& entry = entries_[key];
        entry.initialized = false;
        entry.degraded = false;
        entry.stale = false;
        entry.reset_pending = true;
    }

    void erase(const TrackerKey& key) { entries_.erase(key); }
    void clear() noexcept { entries_.clear(); }

private:
    struct Entry {
        std::uint64_t timeout{0};
        std::uint64_t last_seen{0};
        bool initialized{false};
        bool degraded{false};
        bool stale{false};
        bool reset_pending{false};
        bool has_custom_timeout{false};
    };

    std::uint64_t default_timeout_;
    std::map<TrackerKey, Entry> entries_;
};

}  // namespace etrike::protocol
