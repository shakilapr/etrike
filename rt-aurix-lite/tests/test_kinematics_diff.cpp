// Differential test: rta::Kinematics vs the original RT physics_model.
//
// The old model is compiled here as a read-only behavioral oracle. We feed
// identical DriveCmd inputs to both and assert identical outputs across a
// grid of speed/yaw points (three speed regimes + obstacle functions).
//
// Oracle sources (read-only, from rt-esp32):
//   rt-esp32/src/physics_model.{h,cpp}
//   rt-esp32/src/config.h          (rt:: constants)
//   shared/shared_config.h
//   native-test/hal/shadow/esp_log.h   (logging stub for the oracle)

#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "domain/kinematics.h"
#include "physics_model.h"   // rt::PhysicsModel oracle (rt-esp32/src)

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

// Compare old rt::ResolvedSetpoint vs new rta::KinematicsResult.
// Tolerances are integer-exact; both use the same float math and rounding.
void compare_point(std::int32_t speed_mmps, std::int32_t yaw_mrad_s,
                   std::uint8_t gear, unsigned obstacle_mm) {
    rt::DriveCmd old_cmd;
    old_cmd.speed_mmps = speed_mmps;
    old_cmd.yaw_rate_mrad_s = yaw_mrad_s;

    rta::DriveDemand new_cmd;
    new_cmd.speed_mmps = speed_mmps;
    new_cmd.yaw_rate_mrad_s = yaw_mrad_s;
    new_cmd.gear = gear;

    rt::PhysicsModel old_model;
    rt::ResolvedSetpoint old_out{};
    bool old_ok = old_model.resolve(old_cmd, old_out);

    rta::Kinematics new_model;
    rta::KinematicsResult new_out{};
    bool new_ok = new_model.resolve(new_cmd, new_out);

    CHECK(old_ok == new_ok);
    CHECK(old_out.motor_speed_mmps == new_out.motor_speed_mmps);
    CHECK(old_out.steer_angle_mdeg == new_out.steer_angle_mdeg);
    CHECK(old_out.steer_valid == new_out.steer_valid);
    CHECK(old_out.steer_saturated == new_out.steer_saturated);
    CHECK(old_out.reversing == new_out.reversing);
    // cmd_gear is new-only; old model did not carry it. Verify mapping.
    CHECK(new_out.cmd_gear == gear);

    // Obstacle functions (static, stateless) — compare directly.
    CHECK(rt::PhysicsModel::obstacle_limit(old_cmd.speed_mmps, obstacle_mm)
          == rta::Kinematics::obstacle_limit(new_cmd.speed_mmps, obstacle_mm));
    CHECK(rt::PhysicsModel::obstacle_to_kpa(obstacle_mm)
          == rta::Kinematics::obstacle_to_kpa(obstacle_mm));
}

void test_dynamic_limit() {
    // Same formula; compare across the speed range.
    for (std::int32_t s = -4000; s <= 4000; s += 250) {
        float old_limit = rt::compute_dynamic_limit(static_cast<float>(s));
        float new_limit = rta::compute_dynamic_limit(static_cast<float>(s));
        CHECK(std::abs(old_limit - new_limit) < 1e-4f);

        float old_thr = rt::compute_following_error_threshold(static_cast<float>(s));
        float new_thr = rta::compute_following_error_threshold(static_cast<float>(s));
        CHECK(std::abs(old_thr - new_thr) < 1e-4f);
    }
}

void run_grid() {
    // Three regimes: forward, reverse, low-speed decay, zero-speed yaw.
    const std::int32_t speeds[] = {-4000, -3000, -1500, -500, -100, -50, -10,
                                    0,     10,    50,    100,  500,  1500, 3000, 4000};
    const std::int32_t yaws[]   = {-3000, -1000, -500, -100, -10, 0,
                                    10,    100,   500,  1000, 3000};
    const unsigned obstacles[] = {0, 100, 300, 800, 1500, 3000, 5000};

    for (std::int32_t s : speeds) {
        for (std::int32_t y : yaws) {
            for (unsigned o : obstacles) {
                compare_point(s, y, 1, o);
            }
        }
    }

    // Gear mapping (new-only behavior): verify gear passes through.
    for (std::uint8_t g = 0; g <= 3; ++g) {
        compare_point(1000, 200, g, 3000);
    }
}

}  // namespace

int main() {
    run_grid();
    test_dynamic_limit();
    if (g_failures) {
        std::printf("kinematics_diff: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("kinematics_diff: all differential checks passed\n");
    return 0;
}
