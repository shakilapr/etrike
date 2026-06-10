// g++ -std=c++17 -I. -I../src -I../../shared test_pid.cpp -o test_pid && ./test_pid

#include <cstdio>
#include <cmath>

#include "speed_pid.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)
#define CHECK_NEAR(a,b,tol) CHECK(std::abs((a)-(b)) < (tol))

int main() {
    printf("\n=== PID Tests ===\n\n");

    using namespace rt;

    // P-only: error=100, Kp=1 → output ≈ 100
    {
        SpeedPid pid;
        float out = pid.update(1000.0f, 900.0f, 0.01f);
        CHECK_NEAR(out, 100.0f, 20.0f);
        printf("  ok  proportional: %.1f\n", static_cast<double>(out));
    }

    // Integral buildup
    {
        SpeedPid pid;
        for (int i = 0; i < 10; ++i)
            pid.update(1000.0f, 900.0f, 0.01f);
        CHECK(pid.output() > 100.0f);
        printf("  ok  integral buildup: %.1f\n", static_cast<double>(pid.output()));
    }

    // Reset
    {
        SpeedPid pid;
        pid.update(1000.0f, 0.0f, 0.01f);
        pid.reset();
        float out = pid.update(1000.0f, 900.0f, 0.01f);
        CHECK_NEAR(out, 100.0f, 20.0f);
        printf("  ok  reset: %.1f\n", static_cast<double>(out));
    }

    // Zero error
    {
        SpeedPid pid;
        float out = pid.update(1000.0f, 1000.0f, 0.01f);
        CHECK_NEAR(out, 0.0f, 1.0f);
        printf("  ok  zero error: %.1f\n", static_cast<double>(out));
    }

    // Negative error → negative output
    {
        SpeedPid pid;
        float out = pid.update(500.0f, 1000.0f, 0.01f);
        CHECK(out < 0.0f);
        printf("  ok  negative: %.1f\n", static_cast<double>(out));
    }

    // Anti-windup
    {
        SpeedPid pid;
        for (int i = 0; i < 1000; ++i)
            pid.update(3000.0f, 0.0f, 0.01f);
        CHECK(pid.output() > 3000.0f && pid.output() < 3100.0f);
        printf("  ok  anti-windup: %.1f\n", static_cast<double>(pid.output()));
    }

    // Zero dt → returns previous
    {
        SpeedPid pid;
        pid.update(1000.0f, 0.0f, 0.01f);
        float prev = pid.output();
        float out = pid.update(2000.0f, 0.0f, 0.0f);
        CHECK_NEAR(out, prev, 0.01f);
        printf("  ok  zero-dt safety\n");
    }

    // Custom gains
    {
        SpeedPid::Gains g{2.0f, 0.0f, 0.0f, 100.0f};
        SpeedPid pid(g);
        float out = pid.update(1000.0f, 0.0f, 0.01f);
        CHECK_NEAR(out, 2000.0f, 20.0f);  // P-only with Kp=2
        printf("  ok  custom gains\n");
    }

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
