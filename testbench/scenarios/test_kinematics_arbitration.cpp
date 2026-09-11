// Section 11: real rt-esp32 kinematics and brake arbitration.
//
// The testbench RtNode uses a simplified motion model; these tests exercise the
// REAL production classes/functions (rt::PhysicsModel, rt::brake_arbitrate,
// compute_dynamic_limit) directly, plus a whole-vehicle mode-change-under-motion
// case.
#include <iostream>
#include <cmath>
#include "shared_config.h"
#include "test_bench.hpp"
#include "rt-esp32/src/physics_model.h"
#include "rt-esp32/src/brake_arbitration.h"

namespace testbench {

namespace {
bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
bool near_i(int32_t a, int32_t b, int32_t tol) { return std::abs(a - b) <= tol; }
}  // namespace

// 11.1 Brake arbitration: highest of obstacle/host wins, clamped to SEB limit.
bool test_real_brake_arbitration() {
    std::cout << "TEST: real brake_arbitrate (max + clamp)\n";
    int host = 3000;
    int obstacle = 4500;
    int a = rt::brake_arbitrate(obstacle, host);        // max(4500,3000)
    int b = rt::brake_arbitrate(host, 0);               // host only
    int c = rt::brake_arbitrate(0, -5);                 // negative -> 0
    int d = rt::brake_arbitrate(20000, 0);              // over-limit -> clamped
    std::cout << "  max=" << a << " host=" << b << " neg=" << c << " over=" << d
              << " kMaxBrakeKpa=" << shared::kMaxBrakeKpa << "\n";
    if (a != obstacle || b != host || c != 0 || d != shared::kMaxBrakeKpa) {
        std::cerr << "  FAIL: brake arbitration incorrect\n";
        return false;
    }
    return true;
}

// 11.2 Obstacle speed limiter / brake request scaling (real functions).
bool test_real_obstacle_limits() {
    std::cout << "TEST: real obstacle_limit / obstacle_to_kpa\n";
    int32_t at_stop = rt::PhysicsModel::obstacle_limit(2000, shared::kObstacleStopMM);
    int32_t at_clear = rt::PhysicsModel::obstacle_limit(2000, shared::kObstacleClearMM);
    int32_t mid = rt::PhysicsModel::obstacle_limit(2000, (shared::kObstacleStopMM + shared::kObstacleClearMM) / 2);
    int32_t kpa_stop = rt::PhysicsModel::obstacle_to_kpa(shared::kObstacleStopMM);
    int32_t kpa_clear = rt::PhysicsModel::obstacle_to_kpa(shared::kObstacleClearMM);

    std::cout << "  limit stop=" << at_stop << " mid=" << mid << " clear=" << at_clear
              << "  kpa stop=" << kpa_stop << " clear=" << kpa_clear << "\n";
    if (at_stop != 0 || at_clear != 2000 || mid <= 0 || mid >= 2000) {
        std::cerr << "  FAIL: obstacle speed limit scaling wrong\n";
        return false;
    }
    if (kpa_stop != shared::kObstacleMaxKpa || kpa_clear != 0) {
        std::cerr << "  FAIL: obstacle brake scaling wrong\n";
        return false;
    }
    return true;
}

// 11.3 Real inverse-bicycle kinematics + dynamic steer limit.
bool test_real_kinematics() {
    std::cout << "TEST: real PhysicsModel inverse bicycle + dynamic limit\n";
    rt::PhysicsModel phys;

    rt::ResolvedSetpoint straight{};
    phys.resolve({2000, 0}, straight);
    bool straight_ok = near_i(straight.motor_speed_mmps, 2000, 1) && straight.steer_angle_mdeg == 0;

    // Low speed + yaw request must NOT generate forward speed (cannot spin in place).
    rt::ResolvedSetpoint spin{};
    phys.resolve({0, 1500}, spin);
    bool spin_ok = (spin.motor_speed_mmps == 0) && (spin.steer_angle_mdeg != 0);

    // Reverse speed is clamped to the reverse limit.
    rt::ResolvedSetpoint rev{};
    phys.resolve({-9999, 0}, rev);
    bool rev_ok = near_i(rev.motor_speed_mmps, -shared::kMaxSpeedRevMmps, 1);

    // Extremely large yaw at speed saturates the steering clamp.
    rt::ResolvedSetpoint sat{};
    phys.resolve({200, 9000}, sat);
    bool sat_ok = sat.steer_saturated;

    // Dynamic limit shrinks with speed (40 deg at low speed -> 5 deg at high).
    float limit_low = rt::compute_dynamic_limit(0);
    float limit_high = rt::compute_dynamic_limit(shared::kMaxSpeedFwdMmps);
    bool limit_ok = near(limit_low, 40.0f, 0.1f) && (limit_high < limit_low);

    std::cout << "  straight=" << straight_ok << " spin0=" << spin_ok
              << " rev=" << rev.motor_speed_mmps << " saturated=" << sat_ok
              << " limit low=" << limit_low << " high=" << limit_high << "\n";
    if (!straight_ok || !spin_ok || !rev_ok || !sat_ok || !limit_ok) {
        std::cerr << "  FAIL: kinematics behavior incorrect\n";
        return false;
    }
    return true;
}

// 11.4 Mode change while moving: SYS AUTO -> MANUAL revokes rt authority.
bool test_mode_change_under_motion() {
    std::cout << "TEST: AUTO->MANUAL while moving revokes rt authority\n";
    TestBench tb;
    tb.reset();
    tb.host().request_mode(can::Mode::Auto);
    tb.host().request_power(true);
    tb.run_for_ms(200, 10);
    tb.press_start_button();
    tb.run_for_ms(300, 10);
    tb.host().set_gear(can::Gear::D);
    tb.host().send_drive_cmd(1500);
    tb.run_for_ms(800, 10);

    bool moving = tb.mtr().is_traction_enabled() && tb.rt().is_motion_authorized();

    tb.host().request_mode(can::Mode::Manual);
    tb.run_for_ms(1000, 10);

    bool authority_gone = !tb.rt().is_mode_authority_ok();
    bool rcmd_zero = (tb.rt().commanded_speed_mmps() == 0);
    std::cout << "  moving=" << moving << " mode_authority_ok=" << tb.rt().is_mode_authority_ok()
              << " rt_cmd=" << tb.rt().commanded_speed_mmps() << "\n";

    if (!moving || !authority_gone || !rcmd_zero) {
        std::cerr << "  FAIL: mode change under motion did not revoke authority\n";
        return false;
    }
    return true;
}

// 11.5 Concurrent throttle + service brake: the brake path must still apply the
// requested pressure (there is no software throttle interlock — documented).
bool test_brake_throttle_concurrent() {
    std::cout << "TEST: concurrent throttle + service brake\n";
    TestBench tb;
    tb.reset();
    tb.host().request_mode(can::Mode::Auto);
    tb.host().request_power(true);
    tb.run_for_ms(200, 10);
    tb.press_start_button();
    tb.run_for_ms(300, 10);

    tb.host().set_gear(can::Gear::D);
    tb.host().send_drive_cmd(1500);
    tb.host().set_brake_kpa(10000);  // -> clamped 5000 kPa -> SEB pressure 5000
    tb.run_for_ms(1200, 10);

    float kpa = tb.seb().actual_pressure_kpa();
    std::cout << "  SEB pressure=" << kpa << "kPa (exp 5000)  MTR traction="
              << (tb.mtr().is_traction_enabled() ? "YES" : "NO") << "\n";
    if (!near(kpa, 5000.0f, 300.0f)) {
        std::cerr << "  FAIL: service brake not applied while throttle active\n";
        return false;
    }
    return true;
}

}  // namespace testbench
