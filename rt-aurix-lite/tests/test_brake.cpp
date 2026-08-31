// Brake controller unit tests — pure, deterministic.

#include <cstdio>
#include <cstdlib>

#include "domain/brake.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::BrakeCommand;
using rta::BrakeControl;
using rta::BrakeFeedback;
using rta::BrakeState;
using rta::Mode;

// BOOT_WAIT 500 ms @ 50 Hz = 25 ticks -> LISTEN_SYNC.
void test_boot_wait() {
    BrakeControl bc;
    bc.init();
    BrakeCommand out;
    BrakeFeedback fb;

    CHECK(bc.state() == BrakeState::BOOT_WAIT);
    for (int i = 0; i < 24; ++i) {
        CHECK(!bc.tick(false, false, 0, Mode::Manual, fb, out));
    }
    CHECK(bc.state() == BrakeState::BOOT_WAIT);
    CHECK(!bc.tick(false, false, 0, Mode::Manual, fb, out));  // 25th -> LISTEN_SYNC
    CHECK(bc.state() == BrakeState::LISTEN_SYNC);
}

// LISTEN_SYNC -> ACTIVE on aligned feedback; timeout -> DEGRADED.
void test_listen_sync() {
    BrakeControl bc;
    bc.init();
    BrakeCommand out;
    BrakeFeedback fb;
    // pass boot
    for (int i = 0; i < 25; ++i) bc.tick(false, false, 0, Mode::Manual, fb, out);
    CHECK(bc.state() == BrakeState::LISTEN_SYNC);

    // No status yet -> stay
    CHECK(!bc.tick(false, false, 0, Mode::Manual, fb, out));
    CHECK(bc.state() == BrakeState::LISTEN_SYNC);

    // Valid but not aligned -> stay
    fb.valid = true;
    fb.alignment = false;
    fb.stroke_raw = 700;
    CHECK(!bc.tick(false, false, 0, Mode::Manual, fb, out));
    CHECK(bc.state() == BrakeState::LISTEN_SYNC);

    // Aligned -> ACTIVE, first frame holds sync stroke
    fb.alignment = true;
    bool tx = bc.tick(false, false, 0, Mode::Manual, fb, out);
    CHECK(tx);
    CHECK(bc.state() == BrakeState::ACTIVE);
    CHECK(out.valid);
    CHECK(out.stroke_mode);
    CHECK(out.stroke_raw == 700);  // hold-on-sync

    // Next frame: released -> 0 mm (600 raw)
    tx = bc.tick(false, false, 0, Mode::Manual, fb, out);
    CHECK(tx);
    CHECK(out.stroke_mode);
    CHECK(out.stroke_raw == 600);

    // Timeout path: no status for 2 s -> DEGRADED
    BrakeControl bc2;
    bc2.init();
    for (int i = 0; i < 25; ++i) bc2.tick(false, false, 0, Mode::Manual, fb, out);
    BrakeFeedback fb_none;  // invalid
    for (int i = 0; i < 120; ++i) {
        if (bc2.state() == BrakeState::DEGRADED) break;
        bc2.tick(false, false, 0, Mode::Manual, fb_none, out);
    }
    CHECK(bc2.state() == BrakeState::DEGRADED);

    // DEGRADED recovers to ACTIVE on aligned status
    bool t2 = bc2.tick(false, false, 0, Mode::Manual, fb, out);
    CHECK(t2);
    CHECK(bc2.state() == BrakeState::ACTIVE);
}

// Priority: ESTOP > lever > pressure > released.
void test_build_command_priority() {
    BrakeControl bc;
    bc.init();
    BrakeCommand out;
    BrakeFeedback fb;
    fb.valid = true;
    fb.alignment = true;
    fb.stroke_raw = 600;
    for (int i = 0; i < 25; ++i) bc.tick(false, false, 0, Mode::Manual, fb, out);
    CHECK(bc.tick(false, false, 0, Mode::Manual, fb, out));  // -> ACTIVE
    CHECK(bc.state() == BrakeState::ACTIVE);

    // Released -> 0 mm (600)
    CHECK(bc.tick(false, false, 0, Mode::Manual, fb, out));
    CHECK(out.stroke_mode);
    CHECK(out.stroke_raw == 600);

    // Pressure from automated braking (kPa=1000 -> raw=20)
    CHECK(bc.tick(false, false, 1000, Mode::Auto, fb, out));
    CHECK(!out.stroke_mode);
    CHECK(out.pressure_raw == 20);
    CHECK(out.auto_brake);

    // Lever override beats pressure
    CHECK(bc.tick(true, false, 5000, Mode::Auto, fb, out));
    CHECK(out.stroke_mode);
    CHECK(out.stroke_raw == 900);  // 15 mm -> raw 900
    CHECK(!out.auto_brake);

    // ESTOP beats lever and pressure -> max stroke 27 mm (1140)
    CHECK(bc.tick(true, true, 0, Mode::Manual, fb, out));
    CHECK(out.stroke_mode);
    CHECK(out.stroke_raw == 1140);
    CHECK(!out.auto_brake);
}

}  // namespace

int main() {
    test_boot_wait();
    test_listen_sync();
    test_build_command_priority();
    if (g_failures) {
        std::printf("brake: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("brake: all tests passed\n");
    return 0;
}
