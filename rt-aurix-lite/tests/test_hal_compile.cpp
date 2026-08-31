// HAL interface compile check — verifies the abstract interfaces are
// valid C++ and the Bus/TxClass aliases resolve. Not a behavioral test.

#include <cstdio>

#include "hal/can.h"
#include "hal/gpio.h"
#include "hal/clock.h"
#include "hal/watchdog.h"
#include "protocol/route_table.h"
#include "protocol/core/frame.hpp"

namespace {

// Minimal fake implementations to prove the interfaces are implementable.
class FakeCan : public rta::hal::Can {
public:
    bool transmit(rta::hal::Bus, const etrike::protocol::Frame&, rta::hal::TxClass) override { return true; }
    bool receive(rta::hal::Bus, etrike::protocol::Frame&) override { return false; }
    void error_counters(rta::hal::Bus, std::uint8_t&, std::uint8_t&) const override {}
    bool bus_off(rta::hal::Bus) const override { return false; }
};

class FakeClock : public rta::hal::Clock {
public:
    rta::TimeUs monotonic_us() override { return 0; }
};

class FakeWatchdog : public rta::hal::Watchdog {
public:
    void service() override {}
};

}  // namespace

int main() {
    FakeCan can;
    FakeClock clock;
    FakeWatchdog wdt;
    rta::hal::Gpio* gpio = nullptr;  // interface only

    // TxClass semantics resolve.
    rta::hal::TxClass urgent = rta::hal::TxClass::Urgent;
    (void)urgent;
    (void)can;
    (void)clock;
    (void)wdt;
    (void)gpio;
    std::printf("hal_compile: OK\n");
    return 0;
}
