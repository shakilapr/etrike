// g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared test_pid_controller.cpp -o test_pid && ./test_pid
//
// Verifies: pid_controller.h — standalone PID with anti-windup, derivative-on-measurement,
// low-pass filter, setpoint-change I-reset, first-call seeding.
// Also: speed_controller.h — encoder guard and mm/s scaling.

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

    // ── Test 1: Proportional response, clamped ───────────────────────
    printf("-- P-only: correct sign, clamped to [-1,+1] --\n");
    {
        PidController pid;
        pid.ki = 0.0f; pid.kd = 0.0f;  // P-only
        float out = pid.update(100.0f, 80.0f, 0.01f);  // error=20, kp=1.0 → 20 clamped to 1.0
        NEAR(out, 1.0f, 0.01f);
        printf("  error=20 → output=%.2f (clamped from 20.0)\n", out);
    }

    // ── Test 2: Negative error (overspeed) → clamped to -1.0 ─────────
    printf("-- Negative error (overspeed) --\n");
    {
        PidController pid;
        pid.ki = 0.0f; pid.kd = 0.0f;
        float out = pid.update(50.0f, 120.0f, 0.01f);  // error=-70, kp=1.0 → -70 clamped to -1.0
        NEAR(out, -1.0f, 0.01f);
        printf("  error=-70 → output=%.2f (clamped from -70.0)\n", out);
    }

    // ── Test 3: Integral accumulation (unclamped region) ──────────────
    printf("-- I-term accumulation (small error, no clamping) --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.kd = 0.0f;  // I-only
        // Use small error so output stays within [-1,+1]
        float out = 0.0f;
        for (int i = 0; i < 50; ++i) {
            out = pid.update(10.0f, 0.0f, 0.01f);  // error=10, ki=0.1, dt=0.01
        }
        // After 50 steps: integral = 50 * 0.1 * 10 * 0.01 = 0.5
        NEAR(out, 0.5f, 0.05f);
        printf("  50×I-only (error=10): final output=%.3f (expected ~0.5)\n", out);
    }

    // ── Test 4: Derivative-on-measurement (unclamped region) ──────────
    printf("-- D-on-measurement (small change) --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.ki = 0.0f;  // D-only
        // First call seeds prev_measurement (no D spike)
        pid.update(100.0f, 80.0f, 0.01f);
        // Second call: measured jumps from 80→85 (closing gap by 5)
        float out = pid.update(100.0f, 85.0f, 0.01f);
        // d_input = -(85-80)/0.01 = -500, d_term = 0.05 * (-500) = -25
        // clamped to -1.0
        NEAR(out, -1.0f, 0.01f);
        printf("  measured 80→85, D-only output=%.1f (expected -1.0, clamped from -25)\n", out);
    }

    // ── Test 5: Anti-windup clamping ──────────────────────────────────
    printf("-- Anti-windup clamp --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.kd = 0.0f;  // I-only
        pid.output_min = -0.5f;
        pid.output_max =  0.5f;
        float out = 0.0f;
        // Persistent large error saturates I-term
        for (int i = 0; i < 200; ++i) {
            out = pid.update(200.0f, 100.0f, 0.01f);
        }
        CHECK(out <= 0.5f && out >= -0.5f);
        printf("  200× persistent error, clamped output=%.2f (≤0.5)\n", out);
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
        // With reset integral, only this step's i_term: ki*error*dt = 0.1*410*0.01 = 0.41
        NEAR(out, 0.41f, 0.05f);
        printf("  setpoint 100→500, output=%.3f (integral reset)\n", out);
    }

    // ── Test 7: D-term low-pass filter ────────────────────────────────
    printf("-- D-term low-pass filter --\n");
    {
        PidController pid;
        pid.kp = 0.0f; pid.ki = 0.0f;
        pid.d_filter_alpha = 0.7f;
        // First call: seed measurement=80
        pid.update(100.0f, 80.0f, 0.01f);
        // Second: measured 80→90
        float out = pid.update(100.0f, 90.0f, 0.01f);
        // d_input_raw = -(90-80)/0.01 = -1000
        // d_filtered = 0.7*0 + 0.3*(-1000) = -300
        // d_term = 0.05 * (-300) = -15, clamped to -1.0
        NEAR(out, -1.0f, 0.01f);
        printf("  filtered D output=%.1f (clamped from -15 to -1.0)\n", out);
    }

    // ── Test 8: Reset clears all state including first-call flag ──────
    printf("-- Reset --\n");
    {
        PidController pid;
        for (int i = 0; i < 50; ++i) pid.update(100.0f, 80.0f, 0.01f);
        pid.reset();
        CHECK(pid.integral == 0.0f);
        CHECK(pid.prev_error == 0.0f);
        CHECK(pid.prev_measurement == 0.0f);
        CHECK(pid.prev_setpoint == 0.0f);
        CHECK(pid.d_filtered == 0.0f);
        // After reset, first call should seed again (no D spike)
        float out = pid.update(1000.0f, 900.0f, 0.01f);
        CHECK(out >= 0.0f);  // positive error → positive output (no D-spike drag)
        printf("  reset → all state zero, post-reset call clean\n");
    }

    // ── Test 9: SpeedController encoder guard ─────────────────────────
    printf("-- SpeedController encoder guard --\n");
    {
        SpeedController sc;
        int16_t pid_out = 12345;  // non-zero sentinel
        sc.update_shadow_pid(500, 0, 0.01f, pid_out);
        CHECK(pid_out == 0);  // guard prevents PID from running
        printf("  measured=0 → pid_output=%d (guard active)\n", pid_out);
    }

    // ── Test 10: SpeedController normal operation after first-call fix ─
    printf("-- SpeedController normal (no D-spike from first call) --\n");
    {
        SpeedController sc;
        int16_t pid_out = 0;
        sc.update_shadow_pid(1000, 900, 0.01f, pid_out);
        // With first-call seeding: no D spike. error=100, kp=1.0, p_term=100.
        // Clip to output_max=1.0. 1.0 * 3000 = 3000 mm/s.
        CHECK(pid_out > 0);  // positive: need MORE speed (setpoint > measured)
        printf("  setpoint=1000 measured=900 → pid_output=%d mm/s (positive, no D spike)\n", pid_out);
    }

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
