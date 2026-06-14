// Phase 7: sys-esp32/src/mode_manager validation
// g++ -std=c++17 -I. -I../src -I../../shared test_mode.cpp ../src/mode_manager.cpp -o t && ./t

#include <cstdio>
#include "mode_manager.h"

static int fails = 0;
#define CHK(d) printf("  %-50s ", d)
#define OK      printf("PASS\n")
#define BAD(m)  do { printf("FAIL: %s\n", m); ++fails; } while(0)

int main() {
    printf("Phase 7: Mode manager\n\n");
    sys::ModeManager mm;
    mm.init();

    CHK("init = MANUAL"); if (mm.mode() == can::Mode::Manual) OK; else BAD("init");

    bool changed = mm.tick(false, true);
    CHK("MODE press: MANUAL->AUTO");
    if (changed && mm.mode()==can::Mode::Auto) OK; else BAD("no toggle");

    CHK("debounce blocks immediate press");
    changed = mm.tick(false, true);
    if (!changed) OK; else BAD("debounce");

    for (int i=0;i<6;++i) mm.tick(true, true);
    changed = mm.tick(false, true);
    CHK("MODE press: AUTO->MANUAL");
    if (changed && mm.mode()==can::Mode::Manual) OK; else BAD("back");

    mm.force_estop();
    CHK("force_estop() -> ESTOP");
    if (mm.mode()==can::Mode::Estop) OK; else BAD("estop");

    changed = mm.tick(false, true);
    CHK("MODE ignored in ESTOP");
    if (!changed && mm.mode()==can::Mode::Estop) OK; else BAD("override");

    for (int i=0;i<6;++i) mm.tick(true, true);
    changed = mm.tick(true, false);
    CHK("START: ESTOP->MANUAL");
    if (changed && mm.mode()==can::Mode::Manual) OK; else BAD("start");

    mm.set_from_can(1);
    CHK("set_from_can(1)->AUTO"); if (mm.mode()==can::Mode::Auto) OK; else BAD("can");
    mm.set_from_can(99);
    CHK("set_from_can(99) ignored"); if (mm.mode()==can::Mode::Auto) OK; else BAD("invalid");

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
