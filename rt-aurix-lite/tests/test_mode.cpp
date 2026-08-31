// Mode/ESTOP state model unit tests — pure, deterministic.

#include <cstdio>
#include <cstdlib>

#include "domain/mode.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::Mode;
using rta::ModeManager;
using rta::ModeRequest;

// Drain the post-toggle debounce (kModeDebounceMs=500ms @10Hz => 5 ticks).
void drain_debounce(ModeManager& mm) {
    for (int i = 0; i < 6; ++i) mm.tick(false, false);
}

// Toggle MANUAL<->AUTO via MODE button (toggle fires on release);
// START exits ESTOP only.
void test_toggle_and_start() {
    ModeManager mm;
    mm.init();
    CHECK(mm.mode() == Mode::Manual);

    // Full press-and-release of MODE -> AUTO (fires on release).
    bool changed = mm.tick(false, false);  // idle
    CHECK(!changed);
    changed = mm.tick(true, false);        // press
    CHECK(!changed);                       // toggle on release, not press
    changed = mm.tick(false, false);       // release
    CHECK(changed);
    CHECK(mm.mode() == Mode::Auto);
    drain_debounce(mm);

    // Second press-and-release -> MANUAL
    mm.tick(true, false);
    changed = mm.tick(false, false);
    CHECK(changed);
    CHECK(mm.mode() == Mode::Manual);
    drain_debounce(mm);

    // START press-and-release in MANUAL does nothing
    mm.tick(false, true);
    mm.tick(false, false);
    CHECK(mm.mode() == Mode::Manual);
    drain_debounce(mm);

    // ESTOP -> START exits to MANUAL
    mm.force_estop();
    CHECK(mm.mode() == Mode::Estop);
    mm.tick(false, true);  // START press
    mm.tick(false, false); // START release -> exits ESTOP
    CHECK(mm.mode() == Mode::Manual);
}

// MODE long-press in ESTOP -> MANUAL.
void test_estop_longpress_exit() {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    CHECK(mm.mode() == Mode::Estop);

    // Hold MODE for 3 s (30 ticks @ 10 Hz)
    bool changed = false;
    for (int i = 0; i < 30; ++i) {
        bool c = mm.tick(true, false);
        if (c) changed = true;
    }
    CHECK(changed);
    CHECK(mm.mode() == Mode::Manual);
}

// MODE button ignored in ESTOP (no toggle out).
void test_mode_ignored_in_estop() {
    ModeManager mm;
    mm.init();
    mm.force_estop();
    // A single MODE press (falling edge) must not toggle to AUTO.
    bool changed = mm.tick(true, false);
    CHECK(!changed);
    CHECK(mm.mode() == Mode::Estop);
}

// HMI request: MANUAL/AUTO selectable, ESTOP rejected, ignored in ESTOP.
void test_hmi_request() {
    ModeManager mm;
    mm.init();
    CHECK(mm.mode() == Mode::Manual);

    ModeRequest req;
    req.valid = true;
    req.mode = Mode::Auto;
    CHECK(mm.apply_hmi_request(req));
    CHECK(mm.mode() == Mode::Auto);

    req.mode = Mode::Manual;
    CHECK(mm.apply_hmi_request(req));
    CHECK(mm.mode() == Mode::Manual);

    // ESTOP not requestable
    req.mode = Mode::Estop;
    CHECK(!mm.apply_hmi_request(req));
    CHECK(mm.mode() == Mode::Manual);

    // While in ESTOP, HMI ignored
    mm.force_estop();
    req.mode = Mode::Auto;
    CHECK(!mm.apply_hmi_request(req));
    CHECK(mm.mode() == Mode::Estop);

    // Invalid request ignored
    ModeRequest bad;
    bad.valid = false;
    bad.mode = Mode::Auto;
    CHECK(!mm.apply_hmi_request(bad));
}

}  // namespace

int main() {
    test_toggle_and_start();
    test_estop_longpress_exit();
    test_mode_ignored_in_estop();
    test_hmi_request();
    if (g_failures) {
        std::printf("mode: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("mode: all tests passed\n");
    return 0;
}
