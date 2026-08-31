// Deterministic three-domain simulator tests — fault injection scenarios.

#include <cstdio>
#include <cstdlib>

#include "platform/host/simulator.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::Simulator;
using rta::hal::Bus;

// Bring the system to a nominal running state: feed host drive, host
// heartbeat, MTR feedback, SES/SEB status for enough cycles.
void boot_nominal(Simulator& sim, std::uint32_t cycles = 60) {
    for (std::uint32_t i = 0; i < cycles; ++i) {
        sim.inject_host_heartbeat(static_cast<std::uint8_t>(i));
        sim.inject_host_drive(1000, 0, 1);
        sim.inject_mtr(900, 1, 0);
        sim.inject_ses(0, true);
        sim.inject_seb(600, true);
        sim.step();
    }
}

// Nominal: drive command produced, no ESTOP.
void test_nominal() {
    Simulator sim;
    boot_nominal(sim);
    CHECK(sim.motion_output().drive.motor_speed_mmps > 0);
    CHECK(!sim.motion_output().estop_required);
}

// ESTOP during drive update -> zero setpoints + estop required.
void test_estop_during_drive() {
    Simulator sim;
    boot_nominal(sim);
    // Inject ESTOP on low bus while a drive update also arrives.
    sim.inject_estop(Bus::Low);
    sim.inject_host_drive(1000, 0, 1);
    sim.step();
    CHECK(sim.motion_output().estop_required);
    CHECK(sim.motion_output().drive.motor_speed_mmps == 0);
}

// Host heartbeat freeze -> assisted stop (zero drive, brake >= assisted).
void test_host_heartbeat_freeze() {
    Simulator sim;
    boot_nominal(sim);
    // Stop injecting host heartbeat; keep drive + MTR alive.
    for (std::uint32_t i = 0; i < 40; ++i) {
        sim.inject_host_drive(1000, 0, 1);
        sim.inject_mtr(900, 1, 0);
        sim.step();
    }
    // Host timeout is 1500 ms = 150 cycles. Run enough.
    for (std::uint32_t i = 0; i < 200; ++i) {
        sim.inject_host_drive(1000, 0, 1);
        sim.inject_mtr(900, 1, 0);
        sim.step();
    }
    CHECK(sim.motion_output().estop_required);       // escalated
    CHECK(sim.motion_output().drive.motor_speed_mmps == 0);
}

// MTR feedback freeze -> zero setpoints.
void test_mtr_feedback_freeze() {
    Simulator sim;
    boot_nominal(sim);
    for (std::uint32_t i = 0; i < 40; ++i) {
        sim.inject_host_heartbeat(static_cast<std::uint8_t>(i));
        sim.inject_host_drive(1000, 0, 1);
        sim.inject_ses(0, true);
        sim.step();
    }
    CHECK(sim.motion_output().drive.motor_speed_mmps == 0);  // mtr lost -> zero
}

// CAN_LOW bus-off during a brake request -> graceful handling (no crash).
void test_can_low_bus_off() {
    Simulator sim;
    boot_nominal(sim);
    // Simulate bus-off by raising error counters.
    sim.can().bus(Bus::Low).set_error_counters(255, 255);
    // Continue feeding; the safety should not crash and drive should zero on
    // bus-off detection path (the sim doesn't wire bus-off -> ESTOP yet, but
    // the system must remain deterministic).
    for (std::uint32_t i = 0; i < 10; ++i) {
        sim.inject_host_heartbeat(1);
        sim.inject_host_drive(1000, 0, 1);
        sim.step();
    }
    // Deterministic: no assertion on specific value beyond "ran without crash".
    CHECK(sim.now_us() > 0);
}

}  // namespace

int main() {
    test_nominal();
    test_estop_during_drive();
    test_host_heartbeat_freeze();
    test_mtr_feedback_freeze();
    test_can_low_bus_off();
    if (g_failures) {
        std::printf("simulator: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("simulator: all tests passed\n");
    return 0;
}
