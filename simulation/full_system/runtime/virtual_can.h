#pragma once
// Deterministic virtual CAN fabric (low + high buses).
//
// Frames are moved, not copied: a transmitter calls transmit() to enqueue on a
// bus; deliver() (called once per 1 ms quantum by the driver, before any node
// steps) routes every queued frame to the frame callback of every subscriber
// on that bus. No FreeRTOS, no threads, no sleep: time only advances through
// SimClock, so a scenario is fully reproducible.
//
// Bus wiring matches the real vehicle:
//   HIGH  : Host (0x300/0x7FC), RT high side
//   LOW   : SYS (0x110/0x113/0x011/0x7FE), RT low side, MTR (0x206)
// RT is dual-bus and subscribes to both.
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "protocol/core/frame.hpp"

namespace sim {

enum class Bus : std::uint8_t { High = 0, Low = 1, Count };

inline const char* bus_name(Bus bus) noexcept {
    return bus == Bus::High ? "high" : "low";
}

class VirtualCanBus {
public:
    // `on_frame(now_us, frame)` is invoked per subscribed node at delivery.
    using RxCallback = std::function<void(int64_t, const etrike::protocol::Frame&)>;

    void subscribe(Bus bus, RxCallback cb) {
        auto& slot = subscribers_[static_cast<std::size_t>(bus)];
        slot.push_back(std::move(cb));
    }

    void transmit(Bus bus, const etrike::protocol::Frame& frame) {
        tx_[static_cast<std::size_t>(bus)].push_back(frame);
    }

    // Deliver all pending frames. Called once per quantum by the driver.
    void deliver(int64_t now_us) {
        for (std::size_t b = 0; b < tx_.size(); ++b) {
            if (tx_[b].empty()) continue;
            auto pending = std::move(tx_[b]);
            tx_[b].clear();
            for (const auto& cb : subscribers_[b]) {
                for (const auto& frame : pending) cb(now_us, frame);
            }
        }
    }

    std::size_t pending(Bus bus) const noexcept {
        return tx_[static_cast<std::size_t>(bus)].size();
    }

private:
    using Subscribers = std::vector<RxCallback>;
    std::array<Subscribers, static_cast<std::size_t>(Bus::Count)> subscribers_;
    std::array<std::vector<etrike::protocol::Frame>, static_cast<std::size_t>(Bus::Count)> tx_;
};

}  // namespace sim
