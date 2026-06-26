// g++ -std=c++17 -DTESTING -I. -I../src -I../../shared -I../../shared/can \
//     test_mode_manager.cpp ../src/mode_manager.cpp -o test_mm && ./test_mm
//
// Verifies: mode_manager.h — toggle, ESTOP exit, long-press, debounce.

#include <cstdio>
#include <cstdint>
#include "can/can_protocol.h"
#include "mode_manager.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

// Run N ticks, return true if any returned true
static bool run_ticks(sys::ModeManager& mm, int n, bool mb, bool sb) {
    bool any = false;
    for (int i = 0; i < n; ++i)
        if (mm.tick(mb, sb)) any = true;
    return any;
}
static void wait_debounce(sys::ModeManager& mm) { run_ticks(mm, 6, false, false); }

int main() {
    printf("\n=== Mode Manager: Toggle, ESTOP Exit, Debounce ===\n\n");
    using namespace sys;
    using namespace can;

    // ── Test 1: MANUAL → AUTO (press + release) ────────────────────────
    printf("-- MANUAL -> AUTO via MODE button --\n");
    {
        ModeManager mm;
        mm.init();
        CHECK(mm.mode() == Mode::Manual);

        // Toggle happens on button RELEASE (falling edge)
        bool changed = mm.tick(true, false);   // press
        CHECK(!changed);
        changed = mm.tick(false, false);       // release → falling edge
        CHECK(changed);
        CHECK(mm.mode() == Mode::Auto);
    }

    // ── Test 2: AUTO → MANUAL ──────────────────────────────────────────
    printf("-- AUTO -> MANUAL --\n");
    {
        ModeManager mm;
        mm.init();
        // Go to AUTO
        mm.tick(true, false);   // press
        mm.tick(false, false);  // release → AUTO
        wait_debounce(mm);
        CHECK(mm.mode() == Mode::Auto);

        // Toggle back
        mm.tick(true, false);   // press
        bool changed = mm.tick(false, false);  // release
        CHECK(changed);
        CHECK(mm.mode() == Mode::Manual);
    }

    // ── Test 3: ESTOP → MANUAL via START button ────────────────────────
    printf("-- ESTOP exit via START button --\n");
    {
        ModeManager mm;
        mm.init();
        mm.force_estop();
        CHECK(mm.mode() == Mode::Estop);

        // MODE short-press ignored in ESTOP
        mm.tick(true, false);   // press
        bool changed = mm.tick(false, false);  // release
        CHECK(!changed);
        CHECK(mm.mode() == Mode::Estop);

        // START button release → MANUAL
        mm.tick(false, true);   // press START
        changed = mm.tick(false, false);  // release START (falling edge)
        CHECK(changed);
        CHECK(mm.mode() == Mode::Manual);
    }

    // ── Test 4: ESTOP → MANUAL via MODE long-press (Gap #11) ───────────
    printf("-- ESTOP exit via MODE long-press (3s) --\n");
    {
        ModeManager mm;
        mm.init();
        mm.force_estop();

        // Hold MODE for 30 ticks (3s @ 10 Hz)
        bool exited = false;
        for (int i = 0; i < 31; ++i) {
            bool ch = mm.tick(true, false);
            if (ch) { exited = true; break; }
        }
        CHECK(exited);
        CHECK(mm.mode() == Mode::Manual);
    }

    // ── Test 5: MODE long-press released early (2s) → stays ESTOP ──────
    printf("-- MODE long-press released early (2s) --\n");
    {
        ModeManager mm;
        mm.init();
        mm.force_estop();

        run_ticks(mm, 20, true, false);  // hold 2s — not enough
        CHECK(mm.mode() == Mode::Estop);

        mm.tick(false, false);  // release → reset counter
        CHECK(mm.mode() == Mode::Estop);
    }

    // ── Test 6: Debounce blocks rapid re-trigger ───────────────────────
    printf("-- Debounce blocks immediate re-press --\n");
    {
        ModeManager mm;
        mm.init();

        // First toggle: MANUAL → AUTO
        mm.tick(true, false);
        mm.tick(false, false);
        CHECK(mm.mode() == Mode::Auto);

        // Immediate re-press during debounce → ignored
        bool changed = mm.tick(true, false);
        CHECK(!changed);
        CHECK(mm.mode() == Mode::Auto);

        // Wait out debounce
        wait_debounce(mm);
        // Now toggle back
        mm.tick(true, false);
        changed = mm.tick(false, false);
        CHECK(changed);
        CHECK(mm.mode() == Mode::Manual);
    }

    // ── Test 7: START button ignored in MANUAL/AUTO ────────────────────
    printf("-- START ignored in non-ESTOP modes --\n");
    {
        ModeManager mm;
        mm.init();
        CHECK(mm.mode() == Mode::Manual);

        // START press+release → no effect in MANUAL
        mm.tick(false, true);
        bool changed = mm.tick(false, false);
        CHECK(!changed);
        CHECK(mm.mode() == Mode::Manual);

        // Go to AUTO
        mm.tick(true, false);
        mm.tick(false, false);
        wait_debounce(mm);
        CHECK(mm.mode() == Mode::Auto);

        // START in AUTO → no effect
        mm.tick(false, true);
        changed = mm.tick(false, false);
        CHECK(!changed);
        CHECK(mm.mode() == Mode::Auto);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail ? 1 : 0;
}
