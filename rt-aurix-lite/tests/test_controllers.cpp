// App orchestration controller tests — typed end-to-end flow.

#include <cstdio>
#include <cstdlib>

#include "app/controllers.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::Mode;
using rta::MotionController;
using rta::BodyController;
using rta::GatewayController;
using rta::DriveDemand;
using rta::MotorFeedback;
using rta::SteeringFeedback;
using rta::BrakeFeedback;

// Motion: forward drive produces a drive command + valid steering.
void test_motion_forward() {
    MotionController mc;
    mc.init();
    DriveDemand demand;
    demand.speed_mmps = 1000;
    demand.yaw_rate_mrad_s = 200;
    demand.gear = 1;

    MotorFeedback mf;
    mf.actual_speed_mmps = 900;  // tracking
    SteeringFeedback sf;
    sf.valid = true;
    sf.angle_aligned = true;
    BrakeFeedback bf;
    bf.valid = true;
    bf.alignment = true;

    MotionController::Output out;
    // Run several control cycles so the steering FSM boots (500 ms) and
    // reaches ACTIVE. Control cadence 100 Hz -> 20 ms per call; steer
    // boot is 25 ticks @ 50 Hz = 500 ms, so ~30 cycles at 20 ms suffice.
    for (int i = 0; i < 40; ++i) {
        mc.control(static_cast<rta::TimeUs>(i * 20'000), demand, mf, sf, bf,
                   1, true, true, 3000, out);
    }
    CHECK(out.drive.motor_speed_mmps > 0);
    CHECK(out.drive.gear == 1);
    CHECK(out.steer.valid);  // steering FSM should be ACTIVE by now
    CHECK(!out.estop_required);
}

// Motion: mode ESTOP forces zero drive + max brake.
void test_motion_estop() {
    MotionController mc;
    mc.init();
    mc.force_estop();
    DriveDemand demand;
    demand.speed_mmps = 1000;
    demand.gear = 1;
    MotorFeedback mf;
    SteeringFeedback sf;
    sf.valid = true;
    BrakeFeedback bf;

    MotionController::Output out;
    mc.control(0, demand, mf, sf, bf, 1, true, true, 3000, out);
    CHECK(out.drive.motor_speed_mmps == 0);   // zeroed
    CHECK(out.estop_required);
}

// Body: brake light on in ESTOP.
void test_body_estop_light() {
    BodyController bc;
    rta::LightState requested;
    requested.left = true;
    requested.head = true;
    BodyController::Output out;
    bc.update(Mode::Estop, requested, out);
    CHECK(out.lights.brake);   // ESTOP forces brake light
    CHECK(out.lights.left);
    CHECK(out.lights.head);
}

// Gateway: passes drive + brake through to motion demand.
void test_gateway_pass_through() {
    GatewayController gc;
    GatewayController::Input in;
    in.drive.speed_mmps = 500;
    in.drive.yaw_rate_mrad_s = 0;
    in.drive.gear = 2;
    in.brake_kpa = 1500;
    in.steer_valid = true;
    in.steer_0_1deg = 100;

    auto out = gc.process(in);
    CHECK(out.drive_demand.speed_mmps == 500);
    CHECK(out.drive_demand.gear == 2);
    CHECK(out.brake_kpa == 1500);
    CHECK(out.steer_from_host);
    CHECK(out.steer_0_1deg == 100);
}

// Mode manager integration through the controller.
void test_mode_via_controller() {
    MotionController mc;
    mc.init();
    CHECK(mc.mode() == Mode::Manual);
    // press-and-release toggles to AUTO.
    mc.mode_tick(true, false);
    bool changed = mc.mode_tick(false, false);
    CHECK(changed);
    CHECK(mc.mode() == Mode::Auto);
}

}  // namespace

int main() {
    test_motion_forward();
    test_motion_estop();
    test_body_estop_light();
    test_gateway_pass_through();
    test_mode_via_controller();
    if (g_failures) {
        std::printf("controllers: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("controllers: all tests passed\n");
    return 0;
}
