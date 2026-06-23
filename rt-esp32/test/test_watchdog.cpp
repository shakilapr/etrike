// g++ -std=c++17 -I. -I../src -I../../shared test_watchdog.cpp ../src/watchdog.cpp -o test_watchdog && ./test_watchdog

#include <cstdio>
#include "stubs.h"
#include "config.h"
#include "watchdog.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== RT Watchdog Tests ===\n\n");
    using namespace rt;

    CHECK(shared::kCmdStaleTimeoutMs <= 200);
    printf("  ok  command stale timeout bounded\n");

    Watchdog wd;
    g_mock_time_us = 0;
    wd.init();
    CHECK(!wd.is_stale());
    printf("  ok  fresh after init\n");

    g_mock_time_us = (shared::kCmdStaleTimeoutMs - 10) * 1000;
    CHECK(!wd.is_stale());
    printf("  ok  fresh before timeout\n");

    g_mock_time_us = (shared::kCmdStaleTimeoutMs + 10) * 1000;
    CHECK(wd.is_stale());
    printf("  ok  stale after timeout\n");

    wd.feed();
    CHECK(!wd.is_stale());
    printf("  ok  feed restores freshness\n");

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
