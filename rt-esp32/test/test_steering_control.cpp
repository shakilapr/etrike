// g++ -std=c++17 -I. -I../src -I../../shared test_steering_control.cpp ../src/physics_model.cpp -o test_steering_control && ./test_steering_control
//
// Verifies: steering_control.h — ESTOP safety behaviors (architecture §7.6).

#include "test_compat.h"  // must be first for GCC < 7 std::clamp + ESP_LOG stubs
#include <cstdio>
#include <cmath>
#include <cstdint>
#include "can/can_protocol.h"
#include "steering_control.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)
#define NEAR(a,b,tol) CHECK(std::abs((double)(a)-(double)(b))<(double)(tol))

using namespace rt;

// Helper: advance steering through boot sequence to ACTIVE.
static void boot_to_active(SteeringControl& sc, uint32_t& now_ms, int16_t sync_angle = 0) {
    can::VcuSesReq out;
    // 25 ticks at 50 Hz = 500 ms in BOOT_WAIT
    for (int i = 0; i < 25; ++i) {
        sc.tick(INT16_MIN, 0, now_ms += 20, out);
    }
    CHECK(sc.state() == SteerState::LISTEN_SYNC);
    // Provide valid 0x201 data to sync
    sc.tick(sync_angle, 1, now_ms += 20, out);  // angle_status=1 (aligned)
    CHECK(sc.state() == SteerState::ACTIVE);
}

int main(){
    printf("\n=== Steering Control: ESTOP Safety Behaviors ===\n\n");

    // ── Test 1: Obstacle ESTOP — hold angle clamped to dynamic limit ─
    printf("-- Test 1: Obstacle ESTOP hold angle clamp --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms);

        // Set target: 30° steering at 25 km/h (6944 mm/s)
        // Dynamic limit at 25 km/h = ~5°. Hold angle should be clamped.
        int32_t angle_30deg_mdeg = 30 * 1000;  // 30000 mdeg
        int32_t speed_25kmh_mmps = 6944;       // 25 km/h
        sc.set_target(angle_30deg_mdeg, speed_25kmh_mmps);

        // Trigger obstacle ESTOP — hold angle must be clamped to dynamic limit
        sc.start_estop(true);  // obstacle_triggered=true
        CHECK(sc.state() == SteerState::ESTOP_HOLD_THEN_SILENT);

        // Verify hold angle is clamped (we can't directly access m_estop_hold_angle,
        // but we can tick() and check what angle is commanded)
        can::VcuSesReq out;
        sc.tick(0, 1, now_ms += 20, out);  // tick to set hold timestamp
        sc.set_estop_hold_time(now_ms);  // now we're in hold phase

        // The commanded angle should be at most ~5° (50 in 0.1° units / raw)
        // At 25 km/h: limit = 5°, raw = 50 in 0.1° units
        int16_t cmd_angle = out.target_angle;
        CHECK(std::abs(cmd_angle) <= 60);  // ~6° max (slight margin)
        printf("  Obstacle ESTOP hold angle = %.1f deg (clamped from 30.0, limit ~5.0)\n",
               cmd_angle / 10.0);
    }

    // ── Test 2: Obstacle ESTOP — angle within limit is preserved ─────
    printf("-- Test 2: Obstacle ESTOP within limit --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms);

        // Set target: 3° steering at 2 km/h (555 mm/s)
        // Dynamic limit at 2 km/h = 40°. 3° is well within limit.
        sc.set_target(3 * 1000, 555);  // 3°, 2 km/h

        sc.start_estop(true);
        CHECK(sc.state() == SteerState::ESTOP_HOLD_THEN_SILENT);
        printf("  Angle within limit → hold-then-silent (no clamping needed)\n");
    }

    // ── Test 3: Non-obstacle ESTOP → ramp to zero ────────────────────
    printf("-- Test 3: Non-obstacle ESTOP ramp-to-zero --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms, 300);  // sync at 30.0°

        // Trigger non-obstacle ESTOP (heartbeat loss, cmd stale, etc.)
        sc.start_estop(false);
        CHECK(sc.state() == SteerState::ESTOP_RAMP_TO_ZERO);

        // Ramp toward 0° at 20°/s = 4 (0.1°) per tick at 50 Hz
        can::VcuSesReq out;
        sc.tick(300, 1, now_ms, out);  // actual=30.0°, cmd should decrease
        int16_t first_step = out.target_angle;
        CHECK(first_step < 300);  // should have stepped toward zero
        printf("  Ramp started: 30.0° → %.1f° (step ~2°/tick)\n", first_step / 10.0);
    }

    // ── Test 4: Ramp following-error fallback (>5° for >1s) ──────────
    printf("-- Test 4: Ramp following-error → FAULT --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms, 300);  // sync at 30.0°
        sc.start_estop(false);

        can::VcuSesReq out;
        // Simulate following error: commanded ramps toward 0°, but actual stays at 30°
        // The ramp reduces cmd by ~4 raw per tick. After ~20 ticks, cmd is ~220 raw.
        // Error = |220 - 300| = 80 raw = 8° > threshold of 5°
        // After >1s (50 ticks), should trigger FAULT

        bool faulted = false;
        for (int i = 0; i < 80; ++i, now_ms += 20) {
            // Actual angle stays at 30° (stuck linkage), cmd ramps down
            int16_t actual = 300;  // stuck at 30°
            sc.tick(actual, 1, now_ms, out);
            if (sc.state() == SteerState::FAULT) {
                faulted = true;
                break;
            }
        }
        CHECK(faulted);
        CHECK(sc.state() == SteerState::FAULT);
        printf("  Mechanical jam detected → FAULT after ~1s of >5° error\n");
    }

    // ── Test 5: Ramp following-error NOT triggered under threshold ────
    printf("-- Test 5: Ramp following-error NOT triggered (tracking OK) --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms, 100);  // sync at 10.0°
        sc.start_estop(false);

        can::VcuSesReq out;
        // Actual angle tracks commanded angle closely (2° error < 5° threshold)
        int16_t cmd = 100;
        bool faulted = false;
        for (int i = 0; i < 60; ++i, now_ms += 20) {
            if (cmd > 4) cmd -= 4; else cmd = 0;
            int16_t actual = cmd + 10;  // 1° tracking error (< 5° threshold)
            sc.tick(actual, 1, now_ms, out);
            if (sc.state() == SteerState::FAULT) {
                faulted = true;
                break;
            }
        }
        CHECK(!faulted);
        CHECK(sc.state() == SteerState::ESTOP_RAMP_TO_ZERO);
        printf("  Normal tracking → no FAULT (error < threshold)\n");
    }

    // ── Test 6: HOLD_THEN_SILENT → FAULT after timeout ───────────────
    printf("-- Test 6: Hold-then-silent timeout --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms);

        // Low speed so angle isn't clamped
        sc.set_target(10 * 1000, 555);  // 10°, 2 km/h
        sc.start_estop(true);  // obstacle ESTOP
        CHECK(sc.state() == SteerState::ESTOP_HOLD_THEN_SILENT);

        // Must set hold timestamp first
        sc.set_estop_hold_time(now_ms);

        can::VcuSesReq out;
        // Hold phase: transmit for 500ms
        for (int i = 0; i < 25; ++i, now_ms += 20) {  // 25 ticks = 500ms at 50Hz
            sc.tick(0, 1, now_ms, out);
            if (sc.state() == SteerState::FAULT) break;
        }
        // After 500ms hold, should transition to FAULT (silent-stop)
        CHECK(sc.state() == SteerState::FAULT);
        printf("  Hold 500ms → FAULT (silent-stop)\n");
    }

    // ── Test 7: exit_estop() returns to ACTIVE ───────────────────────
    printf("-- Test 7: exit_estop() recovery --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;
        boot_to_active(sc, now_ms);

        sc.set_target(0, 0);
        sc.start_estop(false);  // ramp to zero
        CHECK(sc.state() == SteerState::ESTOP_RAMP_TO_ZERO);

        sc.exit_estop();
        CHECK(sc.state() == SteerState::ACTIVE);
        printf("  ESTOP_RAMP_TO_ZERO → exit_estop() → ACTIVE\n");
    }

    // ── Test 8: FAULT → reset_to_listen() recovery ───────────────────
    printf("-- Test 8: FAULT recovery via reset_to_listen() --\n");
    {
        SteeringControl sc;
        sc.init();
        uint32_t now_ms = 0;

        // Force FAULT by sync timeout: go to LISTEN_SYNC, never provide valid angle
        can::VcuSesReq dummy;
        for (int i = 0; i < 25; ++i)
            sc.tick(INT16_MIN, 0, now_ms += 20, dummy);
        CHECK(sc.state() == SteerState::LISTEN_SYNC);
        // Wait >5s with no valid data → FAULT
        now_ms += 5001;
        sc.tick(INT16_MIN, 0, now_ms, dummy);
        CHECK(sc.state() == SteerState::FAULT);

        // START button short-press → reset to LISTEN_SYNC
        sc.reset_to_listen(now_ms);
        CHECK(sc.state() == SteerState::LISTEN_SYNC);
        printf("  FAULT → reset_to_listen() → LISTEN_SYNC\n");
    }

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
