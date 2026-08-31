#pragma once
// Host platform: VirtualCan HAL adapter over native-test VirtualCanBus.
// Reuses the existing virtual_can implementation (not the legacy HAL-shadow
// trick). Two VirtualCanBus instances: low + high.

#include <array>
#include <cstdint>

#include "hal/can.h"
#include "can/virtual_can_bus.h"

namespace rta::platform::host {

class VirtualCan : public rta::hal::Can {
public:
    // Ordered [Low, High] to match rta::Bus enum values.
    VirtualCan() = default;

    can::sim::VirtualCanBus& bus(Bus b) {
        return m_buses[static_cast<std::size_t>(b)];
    }
    const can::sim::VirtualCanBus& bus(Bus b) const {
        return m_buses[static_cast<std::size_t>(b)];
    }

    bool transmit(Bus b, const etrike::protocol::Frame& frame,
                  rta::hal::TxClass = rta::hal::TxClass::Normal) override {
        return bus(b).send(frame);
    }

    bool receive(Bus b, etrike::protocol::Frame& out) override {
        return bus(b).receive(out);
    }

    void error_counters(Bus b, std::uint8_t& tec, std::uint8_t& rec) const override {
        bus(b).get_error_counters(tec, rec);
    }

    bool bus_off(Bus b) const override {
        std::uint8_t tec = 0, rec = 0;
        bus(b).get_error_counters(tec, rec);
        return tec >= 255 || rec >= 255;
    }

    // Fault injection passthrough (test harness).
    void inject_fault(Bus b, can::sim::FaultType type, std::uint32_t can_id = 0) {
        bus(b).inject_fault(type, can_id);
    }

private:
    std::array<can::sim::VirtualCanBus, 2> m_buses;
};

}  // namespace rta::platform::host
