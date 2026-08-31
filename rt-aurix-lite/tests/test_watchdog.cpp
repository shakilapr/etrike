// Watchdog health supervisor unit tests — pure, deterministic.

#include <cstdio>
#include <cstdlib>

#include "app/watchdog_supervisor.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::WatchdogSupervisor;

void test_initial_denied() {
    WatchdogSupervisor ws;
    ws.init();
    // No host command seen yet -> denied even if healthy.
    CHECK(!ws.service_allowed(0, true, true));
}

void test_healthy_allowed() {
    WatchdogSupervisor ws;
    ws.init();
    ws.note_host_alive(100'000);
    CHECK(ws.service_allowed(100'000, true, true));
    // Within stale window (500 ms)
    CHECK(ws.service_allowed(200'000, true, true));
    CHECK(ws.service_allowed(599'000, true, true));
}

void test_stale_denied() {
    WatchdogSupervisor ws;
    ws.init();
    ws.note_host_alive(100'000);
    CHECK(ws.service_allowed(100'000, true, true));
    // Beyond 500 ms stale window -> denied
    CHECK(!ws.service_allowed(601'000, true, true));
}

void test_task_or_can_unhealthy() {
    WatchdogSupervisor ws;
    ws.init();
    ws.note_host_alive(100'000);
    CHECK(!ws.service_allowed(100'000, false, true));  // task unhealthy
    CHECK(!ws.service_allowed(100'000, true, false));  // CAN unhealthy
}

void test_recovery() {
    WatchdogSupervisor ws;
    ws.init();
    ws.note_host_alive(100'000);
    CHECK(!ws.service_allowed(601'000, true, true));  // stale
    ws.note_host_alive(650'000);                      // fresh command
    CHECK(ws.service_allowed(660'000, true, true));   // recovered
}

}  // namespace

int main() {
    test_initial_denied();
    test_healthy_allowed();
    test_stale_denied();
    test_task_or_can_unhealthy();
    test_recovery();
    if (g_failures) {
        std::printf("watchdog: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("watchdog: all tests passed\n");
    return 0;
}
