// g++ -std=c++17 -I. -I../src test_physics.cpp ../src/physics_model.cpp -o test_physics && ./test_physics

#include <cstdio>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "../src/config.h"
#include "../src/physics_model.h"
using namespace cfg;

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)
#define CHECK_NEAR(a,b,tol) CHECK(std::abs(static_cast<int64_t>(a) - static_cast<int64_t>(b)) < (tol))

int main() {
    printf("\n=== Physics Model Tests ===\n\n");
    using namespace rt;

    PhysicsModel pm;

    // straight line
    {
        DriveCmd cmd{1000, 0};
        ResolvedSetpoint sp;
        CHECK(pm.resolve(cmd, sp));
        CHECK(sp.motor_speed_mmps == 1000);
        CHECK(std::abs(sp.steer_angle_mdeg) < 100);
        CHECK(sp.steer_valid);
        CHECK(!sp.steer_saturated);
        CHECK(!sp.reversing);
        printf("  ok  straight forward\n");
    }

    // right turn: δ = atan2(1.5 * 0.5, 1.0) ≈ 36.87°
    {
        DriveCmd cmd{1000, 500};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        float expected = std::atan2(1.5f * 0.5f, 1.0f) * 180.0f / M_PI;
        CHECK_NEAR(sp.steer_angle_mdeg, expected * 1000.0f, 100.0f);
        CHECK(sp.steer_valid);
        CHECK(!sp.steer_saturated);
        printf("  ok  right turn %.1f°\n", sp.steer_angle_mdeg / 1000.0);
    }

    // left turn
    {
        DriveCmd cmd{1000, -300};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.steer_angle_mdeg < 0);
        printf("  ok  left turn %.1f°\n", sp.steer_angle_mdeg / 1000.0);
    }

    // reverse
    {
        DriveCmd cmd{-300, 0};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.motor_speed_mmps == -300);
        CHECK(sp.reversing);
        printf("  ok  reverse\n");
    }

    // forward clamp
    {
        DriveCmd cmd{5000, 0};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.motor_speed_mmps == kMaxSpeedFwdMmps);
        printf("  ok  fwd clamp\n");
    }

    // reverse clamp
    {
        DriveCmd cmd{-2000, 0};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.motor_speed_mmps == -kMaxSpeedRevMmps);
        printf("  ok  rev clamp\n");
    }

    // steering clamp
    {
        DriveCmd cmd{100, 3000};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        int32_t lim = static_cast<int32_t>(kSteerLimitDeg * 1000.0f);
        CHECK(std::abs(sp.steer_angle_mdeg) <= lim + 5);
        CHECK(sp.steer_saturated);
        CHECK(!sp.steer_valid);
        printf("  ok  steer clamp reports saturation\n");
    }

    // standstill
    {
        DriveCmd cmd{0, 0};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(!sp.steer_valid);
        printf("  ok  standstill\n");
    }

    // zero speed with yaw → min-radius turn (new behavior: steer IS valid)
    {
        DriveCmd cmd{0, 200};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.steer_valid);          // now handled as min-radius arc
        CHECK(sp.motor_speed_mmps > 0);
        CHECK(std::abs(sp.steer_angle_mdeg) >= static_cast<int32_t>(kSteerLimitDeg * 1000.0f) - 5);
        CHECK(!sp.steer_saturated);
        printf("  ok  zero speed + yaw → min-radius turn\n");
    }

    // pure negative yaw also becomes a min-radius turn with left steering
    {
        DriveCmd cmd{0, -200};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.steer_valid);
        CHECK(sp.motor_speed_mmps > 0);
        CHECK(sp.steer_angle_mdeg < 0);
        printf("  ok  zero speed + negative yaw → left min-radius turn\n");
    }

    // obstacle limit
    CHECK(PhysicsModel::obstacle_limit(2000, kObstacleClearDistMM + 1) == 2000);
    printf("  ok  obstacle clear\n");
    CHECK(PhysicsModel::obstacle_limit(2000, kObstacleStopDistMM) == 0);
    printf("  ok  obstacle stop\n");
    CHECK(PhysicsModel::obstacle_limit(2000, 50) == 0);
    printf("  ok  obstacle too close\n");
    CHECK(std::abs(PhysicsModel::obstacle_limit(2000, 1650) - 1000) <= 5);
    printf("  ok  obstacle mid\n");

    // reversing flag edge cases
    {
        DriveCmd cmd{1, 0};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(!sp.reversing);
    }
    {
        DriveCmd cmd{-1, 0};
        ResolvedSetpoint sp;
        pm.resolve(cmd, sp);
        CHECK(sp.reversing);
    }
    printf("  ok  reversing flag\n");

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
