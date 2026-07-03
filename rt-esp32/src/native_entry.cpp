// Native test entry point for RT physics model.
// Build: pio run -e native
// Run:   .pio/build/native/program.exe

#include <cstdio>
#include <cmath>
#include "physics_model.h"

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; } else { fprintf(stderr, "FAIL %s\n", msg); fail++; } \
} while(0)
#define CHECK_NEAR(a, b, eps, msg) do { \
    double _na=(double)(a), _nb=(double)(b); \
    if (std::abs(_na - _nb) <= (double)(eps)) { pass++; } \
    else { fprintf(stderr, "FAIL %s: expected %.4f got %.4f\n", msg, _nb, _na); fail++; } \
} while(0)

int main() {
    printf("\n=== RT Physics Model — Native Tests ===\n\n");

    // ── Dynamic limit ──
    printf("-- Dynamic steering limit vs speed --\n");
    float limit_0 = rt::compute_dynamic_limit(0.0f);
    float limit_1000 = rt::compute_dynamic_limit(1000.0f);
    float limit_3000 = rt::compute_dynamic_limit(3000.0f);
    float limit_5000 = rt::compute_dynamic_limit(5000.0f);
    CHECK(limit_0 > limit_1000, "limit should decrease with speed");
    CHECK(limit_1000 > limit_3000, "limit should decrease further");
    CHECK(limit_3000 >= 5.0f, "limit clamped to min 5 deg");

    // ── Following error threshold ──
    printf("-- Following error threshold --\n");
    float thr_0 = rt::compute_following_error_threshold(0.0f);
    float thr_2000 = rt::compute_following_error_threshold(2000.0f);
    CHECK(thr_0 > thr_2000, "threshold should decrease with speed");
    CHECK(thr_0 >= 5.0f, "threshold has minimum");

    // ── Forward straight ──
    printf("-- Forward straight --\n");
    rt::DriveCmd cmd = {};
    cmd.speed_mmps = 2000; cmd.yaw_rate_mrad_s = 0;
    rt::ResolvedSetpoint out = {};
    rt::PhysicsModel pm;
    CHECK(pm.resolve(cmd, out), "straight should be OK");
    CHECK_NEAR(out.steer_angle_mdeg, 0.0f, 10.0f, "straight steer ~0");

    // ── Turning ──
    printf("-- Turning at speed --\n");
    rt::PhysicsModel pm2;
    cmd.speed_mmps = 2000; cmd.yaw_rate_mrad_s = 200;
    rt::ResolvedSetpoint out2 = {};
    CHECK(pm2.resolve(cmd, out2), "turn should be OK");
    CHECK(out2.steer_angle_mdeg > 0, "right turn -> positive steer");

    // ── Standstill ──
    printf("-- Standstill with yaw --\n");
    rt::PhysicsModel pm3;
    cmd.speed_mmps = 0; cmd.yaw_rate_mrad_s = 100;
    rt::ResolvedSetpoint out3 = {};
    CHECK(pm3.resolve(cmd, out3), "standstill resolves (steer to lock, speed 0)");
    CHECK_NEAR((float)out3.motor_speed_mmps, 0.0f, 0.1f, "standstill speed = 0");

    // ── Reverse ──
    printf("-- Reverse turning --\n");
    rt::PhysicsModel pm4;
    cmd.speed_mmps = -1000; cmd.yaw_rate_mrad_s = 100;
    rt::ResolvedSetpoint out4 = {};
    CHECK(pm4.resolve(cmd, out4), "reverse turn should be OK");
    CHECK(out4.reversing, "reverse should set reversing flag");

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
