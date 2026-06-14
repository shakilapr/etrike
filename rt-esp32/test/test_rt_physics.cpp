// Phase R4: Physics + Steering + Obstacle + Brake tests (CAN-based, no intermcu)
#include <cstdio>
#include <cmath>
#include "physics_model.h"
#include "steering_control.h"
#include "obstacle_sensor.h"
#include "brake_arbitration.h"

int main() {
    printf("Phases 29-34: Physics + Steering + Obstacle + Brake (R4 update)\n\n");
    int e = 0;
    #define C(d) printf("  %-55s ", d)
    #define O printf("PASS\n")
    #define B(m) do { printf("FAIL: %s\n", m); ++e; } while(0)

    // ── Physics model (class-based) ─────────────────────────────────
    rt::PhysicsModel phys;
    rt::ResolvedSetpoint r;

    // Forward: speed=2000, yaw=500 mrad/s
    phys.resolve(rt::DriveCmd{2000, 500}, r);
    C("speed=2000,yaw=500 → forward, valid");
    if (r.motor_speed_mmps == 2000 && r.steer_valid) O; else B("fwd");
    C("steer ~18-21 deg (atan2(1.5*0.5,2))");
    if (std::abs(r.steer_angle_mdeg) > 15000 && std::abs(r.steer_angle_mdeg) < 22000) O;
    else B("steer angle");

    // Zero speed with yaw → min-radius turn
    rt::ResolvedSetpoint r2;
    phys.resolve(rt::DriveCmd{0, 100}, r2);
    C("speed=0,yaw=100 → min-radius turn");
    if (r2.motor_speed_mmps >= 50) O; else B("zero+yaw: should produce min speed");

    // Reverse
    rt::ResolvedSetpoint r3;
    phys.resolve(rt::DriveCmd{-300, 0}, r3);
    C("speed=-300 → reversing");
    if (r3.reversing && r3.motor_speed_mmps == -300) O; else B("rev");

    // ── Steering state machine ──────────────────────────────────────
    rt::SteeringControl sc; sc.init();
    can::VcuSesReq out;
    C("steer init BOOT_WAIT");
    if (sc.state() == rt::SteerState::BOOT_WAIT) O; else B("sboot");

    for (int i = 0; i < 25; ++i) sc.tick(INT16_MIN, out);
    C("after 500ms → LISTEN_SYNC");
    if (sc.state() == rt::SteerState::LISTEN_SYNC) O; else B("slist");

    sc.tick(455, out);  // 45.5° raw
    C("sync→ACTIVE, angle=455");
    if (sc.state() == rt::SteerState::ACTIVE && out.target_angle == 455) O; else B("sync");

    // ── Obstacle sensor ─────────────────────────────────────────────
    C("echo 5800us → ~994mm");
    if (rt::obstacle_distance_mm(5800) == 994) O; else B("obs");

    C("obstacle 200mm → speed 0");
    if (rt::obstacle_limit_speed(2000, 200) == 0) O; else B("olim");

    C("obstacle 5000mm → full speed");
    if (rt::obstacle_limit_speed(2000, 5000) == 2000) O; else B("ofull");

    // ── Brake arbitration ───────────────────────────────────────────
    C("max(1000,5000)=5000");
    if (rt::brake_arbitrate(1000, 5000) == 5000) O; else B("barb");

    C("max(8000,3000)=8000");
    if (rt::brake_arbitrate(8000, 3000) == 8000) O; else B("barb2");

    printf("\n  Result: %d failures\n", e);
    return e ? 1 : 0;
}
