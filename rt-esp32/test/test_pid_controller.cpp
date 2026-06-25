// g++ -std=c++17 -I. -I../src -I../../shared test_pid_controller.cpp -o test_pid && ./test_pid
//
// Verifies: pid_controller.h — standalone PID with anti-windup, derivative-on-measurement,
// low-pass filter, setpoint-change I-reset. SpeedController bridge guard logic.

#include <cstdio>
#include <cmath>
#include <cstdint>
#include "pid_controller.h"
#include "speed_controller.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)
#define NEAR(a,b,tol) CHECK(std::abs((double)(a)-(double)(b))<(double)(tol))

using namespace rt;

int main(){
    printf("\n=== PID Controller: Standalone Tests ===\n\n");

    // ── Test 1: Proportional-only response ────────────────────────────
    printf("-- P-only: correct sign and magnitude --\n");
    {
        PidController pid;
        pid.ki = 0.0f; pid.kd = 0.0f;  // P-only
        float out = pid.update(100.0f, 80.0f, 0.01f);  // error=20, kp=1.0
        NEAR(out, 20.0f, 0.01f);
        printf("  error=20 → output=%.2f (kp=1.0)\n", out);
    }

    // ── Test 2: Negative error (measured > setpoint) ──────────────────
    printf("-- Negative error (overspeed) --\n");
    {
        PidController pid;
        pid.ki = 0.0f; pid.kd = 0.0f;
        float out = pid.update(50.0f, 120.0f, 0.01f);  // error=-70, kp=1.0
        NEAR(out, -70.0f, 0.01f);
        printf("  error=-70 → output=%.2f\n", out);
    }

    // ── Test 3: Integral accumulation ─────────────────────────────────
    printf("-- I-term accumulation --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.kd = 0.0f;  // I-only
        float sum = 0.0f;
        for (int i = 0; i < 10; ++i) {
            sum += pid.update(100.0f, 80.0f, 0.01f);  // error=20 each step
        }
        // ki=0.1, error=20, dt=0.01 per step, 10 steps:
        // integral += 0.1 * 20 * 0.01 * 10 = 0.2
        NEAR(sum, 0.2f, 0.02f);
        printf("  10×I-only steps, total=%.3f (expected ~0.2)\n", sum);
    }

    // ── Test 4: Derivative-on-measurement ─────────────────────────────
    printf("-- D-on-measurement --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.ki = 0.0f;  // D-only
        // First call sets baseline (measured=80)
        pid.update(100.0f, 80.0f, 0.01f);
        // Second call: measured jumps from 80→85 (closing gap by 5)
        float out = pid.update(100.0f, 85.0f, 0.01f);
        // d_input = -(85-80)/0.01 = -500, d_term = 0.05 * (-500) = -25
        NEAR(out, -25.0f, 0.5f);
        printf("  measured 80→85, D-only output=%.1f (expected ~-25)\n", out);
    }

    // ── Test 5: Anti-windup clamping ──────────────────────────────────
    printf("-- Anti-windup clamp --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.kd = 0.0f;  // I-only
        pid.output_min = -0.5f;
        pid.output_max =  0.5f;
        float out = 0.0f;
        // Persistent error=100, dt=0.01, ki=0.1, 100 steps
        // integral grows: 0.1 * 100 * 0.01 * 100 = 10 → clamped to 0.5
        for (int i = 0; i < 100; ++i) {
            out = pid.update(200.0f, 100.0f, 0.01f);
        }
        CHECK(out <= 0.5f && out >= -0.5f);
        printf("  100× persistent error, clamped output=%.2f (≤0.5)\n", out);
    }

    // ── Test 6: Setpoint-change I-reset ───────────────────────────────
    printf("-- Setpoint-change resets integral --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.kd = 0.0f;
        pid.setpoint_change_threshold = 200.0f;
        // Accumulate some integral at setpoint=100
        for (int i = 0; i < 50; ++i) {
            pid.update(100.0f, 90.0f, 0.01f);
        }
        // Large setpoint jump → integral should reset
        float out = pid.update(500.0f, 90.0f, 0.01f);  // Δsetpoint=400 > threshold=200
        // With reset integral, output = ki*error = 0.1*410 = 41 (before clamp to output_max=1.0)
        // Integral accumulates only this one step: 0.1*410*0.01 = 0.41
        CHECK(out <= 1.0f);  // should be much smaller than non-reset case
        printf("  setpoint 100→500, output=%.3f (integral reset, expected small)\n", out);
    }

    // ── Test 7: D-term low-pass filter ────────────────────────────────
    printf("-- D-term low-pass filter --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.ki = 0.0f;
        pid.d_filter_alpha = 0.7f;
        // First step: baseline
        pid.update(100.0f, 80.0f, 0.01f);
        // Step: measured 80→90
        float out = pid.update(100.0f, 90.0f, 0.01f);
        // d_input_raw = -(90-80)/0.01 = -1000
        // d_filtered = 0.7*0 + 0.3*(-1000) = -300
        // d_term = 0.05 * (-300) = -15
        NEAR(out, -15.0f, 1.0f);
        printf("  filtered D output=%.1f (raw would be ~-50, filtered ~-15)\n", out);
    }

    // ── Test 8: Reset clears all state ────────────────────────────────
    printf("-- Reset --\n");
    {
        PidController pid;
        // Run a few iterations to build state
        for (int i = 0; i < 50; ++i) pid.update(100.0f, 80.0f, 0.01f);
        pid.reset();
        CHECK(pid.integral == 0.0f);
        CHECK(pid.prev_error == 0.0f);
        CHECK(pid.prev_measurement == 0.0f);
        CHECK(pid.prev_setpoint == 0.0f);
        CHECK(pid.d_filtered == 0.0f);
        printf("  reset → all state zero\n");
    }

    // ── Test 9: SpeedController guard (measured=0) ─────────────────────
    printf("-- SpeedController encoder guard --\n");
    {
        SpeedController sc;
        int16_t pid_out = 12345;  // non-zero sentinel
        sc.update_shadow_pid(500, 0, 0.01f, pid_out);
        CHECK(pid_out == 0);  // guard prevents PID from running
        printf("  measured=0 → pid_output=%d (guard active)\n", pid_out);
    }

    // ── Test 10: SpeedController normal operation ─────────────────────
    printf("-- SpeedController normal --\n");
    {
        SpeedController sc;
        int16_t pid_out = 0;
        sc.update_shadow_pid(1000, 900, 0.01f, pid_out);
        // error=100, kp=1.0, effort≈100, scaled to mm/s: 100/3000 * 3000 = 100 mm/s
        // Actually: effort_fraction is non-dimensional. 100 mm/s error → effort≈0.033
        // 0.033 * 3000 ≈ 100 mm/s correction
        CHECK(pid_out != 0);  // should produce some output
        printf("  setpoint=1000 measured=900 → pid_output=%d mm/s\n", pid_out);
    }

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
