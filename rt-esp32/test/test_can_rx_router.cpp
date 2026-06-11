// g++ -std=c++17 -I. -I../src -I../src test_can_rx_router.cpp ../src/can_rx_router.cpp -o test_can_rx_router && ./test_can_rx_router

#include <cstdio>
#include "can_rx_router.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== SYS Private CAN RX Router Tests ===\n\n");

    {
        can::Frame fr;
        fr.id = can::kIdSyntreeEpsStatus;
        auto route = sys::classify_can_rx_frame(fr);
        CHECK(route.enqueue);
        printf("  ok  EPS-C status queued\n");
    }

    {
        can::Frame fr;
        fr.id = can::kIdSyntreeSebStatus;
        auto route = sys::classify_can_rx_frame(fr);
        CHECK(route.enqueue);
        printf("  ok  SEB status queued\n");
    }

    {
        can::Frame fr;
        fr.id = can::kIdHostDriveCmd;
        auto route = sys::classify_can_rx_frame(fr);
        CHECK(!route.enqueue);
        printf("  ok  public CAN command ignored on SYS private bus\n");
    }

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
