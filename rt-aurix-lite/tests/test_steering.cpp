// Steering controller unit tests — pure, deterministic.

#include <cstdio>
#include <cstdlib>

#include "domain/steering.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::SteerState;
using rta::SteeringControl;
using rta::SteeringCommand;
using rta::SteeringFeedback;

// Boot a fresh SteeringControl through BOOT_WAIT + LISTEN_SYNC into ACTIVE.
// Returns the control, with aligned centered feedback.
void boot_to_active(SteeringControl& sc, SteeringCommand& out, SteeringFeedback& fb) {
    sc.init();
    fb.valid = true;
    fb.angle_aligned = true;
    fb.angle_0_1deg = 0;
    // 25 ticks to pass BOOT_WAIT (500 ms @ 50 Hz).
    for (int i = 0; i < 25; ++i) {
        CHECK(!sc.tick(fb, static_cast<rta::TimeUs>(i * 20'000), out));
    }
    CHECK(sc.state() == SteerState::LISTEN_SYNC);
    // One more tick with aligned feedback -> ACTIVE.
    bool tx = sc.tick(fb, static_cast<rta::TimeUs>(26 * 20'000), out);
    CHECK(tx);
    CHECK(sc.state() == SteerState::ACTIVE);
}

// BOOT_WAIT -> LISTEN_SYNC after 500 ms at 50 Hz (25 ticks).
void test_boot_wait() {
    SteeringControl sc;
    sc.init();
    SteeringCommand out;
    SteeringFeedback fb;
    fb.valid = true;
    fb.angle_aligned = true;
    fb.angle_0_1deg = 0;

    CHECK(sc.state() == SteerState::BOOT_WAIT);
    // 24 ticks still boot wait
    for (int i = 0; i < 24; ++i) {
        CHECK(!sc.tick(fb, static_cast<rta::TimeUs>(i * 20'000), out));
    }
    CHECK(sc.state() == SteerState::BOOT_WAIT);
    // 25th tick -> LISTEN_SYNC
    CHECK(!sc.tick(fb, static_cast<rta::TimeUs>(25 * 20'000), out));
    CHECK(sc.state() == SteerState::LISTEN_SYNC);
}

// LISTEN_SYNC -> ACTIVE on aligned feedback; sync timeout -> FAULT.
void test_listen_sync() {
    SteeringCommand out;
    SteeringFeedback fb;
    SteeringControl sc;
    boot_to_active(sc, out, fb);

    // Not aligned yet -> stay (reset to LISTEN_SYNC first).
    sc.init();
    rta::TimeUs now = 0;
    for (int i = 0; i < 25; ++i) sc.tick(fb, now += 20'000, out);
    CHECK(sc.state() == SteerState::LISTEN_SYNC);
    fb.angle_aligned = false;
    CHECK(!sc.tick(fb, now += 20'000, out));  // 20 ms later, still < 5 s sync window
    CHECK(sc.state() == SteerState::LISTEN_SYNC);

    // Aligned -> ACTIVE, command emitted
    fb.angle_aligned = true;
    fb.angle_0_1deg = 12;
    bool tx = sc.tick(fb, now += 20'000, out);
    CHECK(tx);
    CHECK(sc.state() == SteerState::ACTIVE);
    CHECK(out.valid);
    CHECK(out.angle_0_1deg == 12);

    // Sync timeout path: reset and stall feedback for > 5 s
    SteeringControl sc2;
    sc2.init();
    now = 0;
    for (int i = 0; i < 25; ++i) sc2.tick(fb, now += 20'000, out);
    CHECK(sc2.state() == SteerState::LISTEN_SYNC);
    fb.valid = false;  // no feedback at all
    for (int i = 0; i < 300; ++i) {
        if (sc2.state() == SteerState::FAULT) break;
        sc2.tick(fb, now += 20'000, out);
    }
    CHECK(sc2.state() == SteerState::FAULT);

    // Implausible angle at sync -> FAULT
    SteeringControl sc3;
    sc3.init();
    now = 0;
    for (int i = 0; i < 25; ++i) sc3.tick(fb, now += 20'000, out);
    fb.valid = true;
    fb.angle_aligned = true;
    fb.angle_0_1deg = 450;  // 45° off center
    CHECK(!sc3.tick(fb, now += 20'000, out));
    CHECK(sc3.state() == SteerState::FAULT);
}

// ACTIVE: set_target applies; ESTOP ramp-to-zero.
void test_active_and_ramp() {
    SteeringCommand out;
    SteeringFeedback fb;
    SteeringControl sc;
    boot_to_active(sc, out, fb);

    // Set target to 300 (30°), active transmits it
    sc.set_target(300, 2000);
    CHECK(sc.tick(fb, 30'000'000, out));
    CHECK(out.angle_0_1deg == 300);

    // ESTOP (non-obstacle) -> ramp to zero at 4 0.1°/tick
    sc.start_estop(false);
    CHECK(sc.state() == SteerState::ESTOP_RAMP_TO_ZERO);
    rta::TimeUs now = 30'000'000;
    bool reached_zero = false;
    for (int i = 0; i < 200; ++i) {
        now += 20'000;
        // Feed back the commanded angle so follow-error does not trip.
        fb.angle_0_1deg = out.angle_0_1deg;
        bool tx = sc.tick(fb, now, out);
        (void)tx;
        if (out.angle_0_1deg == 0) { reached_zero = true; break; }
    }
    CHECK(reached_zero);
    CHECK(out.angle_0_1deg == 0);
    // Without an exit request, ramp holds at 0 (stays in RAMP_TO_ZERO).
    CHECK(sc.state() == SteerState::ESTOP_RAMP_TO_ZERO);
}

// ESTOP hold-then-silent (obstacle) with exit.
void test_estop_hold_silent() {
    SteeringCommand out;
    SteeringFeedback fb;
    SteeringControl sc;
    boot_to_active(sc, out, fb);

    sc.set_target(150, 500);  // 15°
    sc.tick(fb, 30'000'000, out);

    // Obstacle ESTOP -> hold-then-silent
    sc.start_estop(true);
    sc.set_estop_hold_time(31'000'000);
    CHECK(sc.state() == SteerState::ESTOP_HOLD_THEN_SILENT);

    // During hold (< 500 ms): transmit hold angle
    bool tx = sc.tick(fb, 31'100'000, out);
    CHECK(tx);
    CHECK(out.angle_0_1deg == 150);

    // After hold expires with no exit -> FAULT (silent stop)
    for (int i = 0; i < 30; ++i) {
        tx = sc.tick(fb, static_cast<rta::TimeUs>(31'600'000 + i * 20'000), out);
        if (sc.state() == SteerState::FAULT) break;
    }
    CHECK(sc.state() == SteerState::FAULT);

    // Exit path: request exit during hold -> ACTIVE after hold
    SteeringControl sc2;
    boot_to_active(sc2, out, fb);
    sc2.set_target(150, 500);
    sc2.tick(fb, 30'000'000, out);
    sc2.start_estop(true);
    sc2.set_estop_hold_time(31'000'000);
    sc2.exit_estop();
    for (int i = 0; i < 40; ++i) {
        sc2.tick(fb, static_cast<rta::TimeUs>(31'600'000 + i * 20'000), out);
        if (sc2.state() == SteerState::ACTIVE) break;
    }
    CHECK(sc2.state() == SteerState::ACTIVE);

    // reset_to_listen from FAULT
    SteeringControl sc3;
    sc3.init();
    for (int i = 0; i < 25; ++i) sc3.tick(fb, static_cast<rta::TimeUs>(i * 20'000), out);
    fb.angle_0_1deg = 500;  // implausible -> FAULT
    fb.valid = true; fb.angle_aligned = true;
    sc3.tick(fb, 40'000'000, out);
    CHECK(sc3.state() == SteerState::FAULT);
    sc3.reset_to_listen(41'000'000);
    CHECK(sc3.state() == SteerState::LISTEN_SYNC);
}

// Dynamic clamp/follow-error are exercised via ESTOP hold angle clamp.
void test_dynamic_clamp_in_estop_hold() {
    SteeringCommand out;
    SteeringFeedback fb;
    SteeringControl sc;
    boot_to_active(sc, out, fb);

    // At high speed (25 m/s -> 90 km/h), dynamic limit is 5° (max_raw 50).
    sc.set_target(3000, 25000);  // 300° request but hold clamps
    sc.tick(fb, 30'000'000, out);
    sc.start_estop(true);  // obstacle
    sc.set_estop_hold_time(31'000'000);
    CHECK(sc.state() == SteerState::ESTOP_HOLD_THEN_SILENT);
    sc.tick(fb, 31'100'000, out);
    CHECK(std::abs(static_cast<int>(out.angle_0_1deg)) <= 50);  // clamped to <=5°
}

}  // namespace

int main() {
    test_boot_wait();
    test_listen_sync();
    test_active_and_ramp();
    test_estop_hold_silent();
    test_dynamic_clamp_in_estop_hold();
    if (g_failures) {
        std::printf("steering: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("steering: all tests passed\n");
    return 0;
}
