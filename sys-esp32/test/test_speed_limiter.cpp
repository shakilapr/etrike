// g++ -std=c++17 -I. -I../src -I../../shared test_speed_limiter.cpp ../src/speed_limiter.cpp -o test_speed_limiter && ./test_speed_limiter

#include <cstdio>
#include <cmath>
#include <limits>

#include "config.h"
#include "speed_limiter.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== SYS Speed Limiter Tests ===\n\n");
    using namespace sys;

    CHECK(limit_forward_speed_for_obstacle(2000, std::numeric_limits<unsigned>::max()) == 2000);
    printf("  ok  unknown obstacle leaves speed unchanged\n");

    CHECK(limit_forward_speed_for_obstacle(2000, kObstacleClearDistMM + 1) == 2000);
    printf("  ok  clear distance leaves speed unchanged\n");

    CHECK(limit_forward_speed_for_obstacle(2000, kObstacleStopDistMM) == 0);
    CHECK(limit_forward_speed_for_obstacle(2000, 50) == 0);
    printf("  ok  stop distance blocks forward throttle\n");

    CHECK(std::abs(limit_forward_speed_for_obstacle(2000, 1650) - 1000) <= 5);
    printf("  ok  mid distance scales forward throttle\n");

    CHECK(limit_forward_speed_for_obstacle(-300, kObstacleStopDistMM) == -300);
    CHECK(limit_forward_speed_for_obstacle(0, kObstacleStopDistMM) == 0);
    printf("  ok  non-forward commands are not front-obstacle limited\n");

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
