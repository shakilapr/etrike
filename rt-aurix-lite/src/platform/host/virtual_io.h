#pragma once
// Host platform: virtual GPIO + clock + watchdog adapters.

#include <array>
#include <cstdint>

#include "hal/gpio.h"
#include "hal/clock.h"
#include "hal/watchdog.h"

namespace rta::platform::host {

// Scriptable GPIO — the simulation sets input states and records outputs.
class VirtualGpio : public rta::hal::Gpio {
public:
    void set_input(rta::hal::InputSignal sig, bool asserted) {
        m_inputs[static_cast<std::size_t>(sig)] = asserted;
    }
    bool read(rta::hal::InputSignal sig) const override {
        return m_inputs[static_cast<std::size_t>(sig)];
    }
    void write(rta::hal::OutputSignal sig, bool asserted) override {
        m_outputs[static_cast<std::size_t>(sig)] = asserted;
    }
    bool output(rta::hal::OutputSignal sig) const {
        return m_outputs[static_cast<std::size_t>(sig)];
    }

private:
    std::array<bool, 8> m_inputs{};   // InputSignal count
    std::array<bool, 8> m_outputs{};  // OutputSignal count
};

// Virtual monotonic clock — the simulation advances it explicitly.
class VirtualClock : public rta::hal::Clock {
public:
    void advance(TimeUs delta_us) { m_now += delta_us; }
    void set(TimeUs now) { m_now = now; }
    TimeUs monotonic_us() override { return m_now; }

private:
    TimeUs m_now = 0;
};

// Records service() calls (count).
class VirtualWatchdog : public rta::hal::Watchdog {
public:
    void service() override { ++m_services; }
    std::uint32_t service_count() const { return m_services; }

private:
    std::uint32_t m_services = 0;
};

}  // namespace rta::platform::host
