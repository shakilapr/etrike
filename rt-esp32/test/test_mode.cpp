// g++ -std=c++17 -I. -I../src test_mode.cpp ../src/mode_manager.cpp -o test_mode && ./test_mode

#include <cstdio>
#include <cstring>
#include "stubs.h"
#include "../src/config.h"
#include "../src/can_protocol.h"
#include "../src/mode_manager.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== Mode Manager Tests ===\n\n");
    using namespace can;

    // Initialize mock GPIO: mode switch = Manual (HIGH/open)
    g_mock_gpio[sys::kModeSwitchGpio] = 1;

    sys::ModeManager mm;
    mm.init();
    CHECK(mm.current() == Mode::Manual);
    printf("  ok  default is Manual\n");

    mm.set(Mode::Auto);
    CHECK(mm.current() == Mode::Auto);
    printf("  ok  Manual -> Auto\n");

    mm.set(Mode::Manual);
    CHECK(mm.current() == Mode::Manual);
    printf("  ok  Auto -> Manual\n");

    mm.set(Mode::Estop);
    CHECK(mm.current() == Mode::Estop);
    printf("  ok  -> Estop\n");

    // Estop blocks lower-priority transitions
    mm.set(Mode::Auto);
    CHECK(mm.current() == Mode::Estop);
    printf("  ok  Estop blocks Auto\n");

    mm.set(Mode::Manual);
    CHECK(mm.current() == Mode::Estop);
    printf("  ok  Estop blocks Manual\n");

    mm.set(Mode::Estop);  // idempotent
    CHECK(mm.current() == Mode::Estop);
    printf("  ok  Estop idempotent\n");

    // Switch polling: ignored in Estop
    g_mock_gpio[sys::kModeSwitchGpio] = 0;  // LOW = Auto
    mm.poll();
    CHECK(mm.current() == Mode::Estop);
    printf("  ok  switch ignored in Estop\n");

    // Test switch polling from clean state
    sys::ModeManager mm2;
    mm2.init();
    CHECK(mm2.current() == Mode::Manual);

    g_mock_gpio[sys::kModeSwitchGpio] = 0;  // LOW = Auto
    mm2.poll();
    CHECK(mm2.current() == Mode::Auto);
    printf("  ok  switch -> Auto\n");

    g_mock_gpio[sys::kModeSwitchGpio] = 1;  // HIGH = Manual
    mm2.poll();
    CHECK(mm2.current() == Mode::Manual);
    printf("  ok  switch -> Manual\n");

    // mode_name strings
    CHECK(strcmp(mode_name(Mode::Manual), "MANUAL") == 0);
    CHECK(strcmp(mode_name(Mode::Auto),   "AUTO")   == 0);
    CHECK(strcmp(mode_name(Mode::Estop),  "ESTOP")  == 0);
    printf("  ok  mode_name strings\n");

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
