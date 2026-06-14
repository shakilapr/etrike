// Phase R4: RT full pipeline test (CAN-based)
#include <cstdio>
#include "heartbeat.h"
#include "can_rx_router.h"
#include "physics_model.h"
#include "steering_control.h"
#include "obstacle_sensor.h"
#include "brake_arbitration.h"
#include "watchdog.h"

int main() {
    printf("Phase R4: RT full pipeline (CAN-based)\n\n");
    int e = 0;
    #define C(d) printf("  %-55s ", d)
    #define O printf("PASS\n")
    #define B(m) do { printf("FAIL: %s\n", m); ++e; } while(0)

    // Watchdog
    rt::CmdWatchdog wd; wd.init();
    C("watchdog init NOT stale");
    if (!wd.is_stale(0)) O; else B("wd0");

    wd.feed(0);
    C("after feed, not stale at 400ms");
    if (!wd.is_stale(400000)) O; else B("wd400");

    C("stale at 600ms (>500ms)");
    if (wd.is_stale(600000)) O; else B("wd600");

    // Full pipeline: DriveCmd → PhysicsModel::resolve
    rt::PhysicsModel phys;
    rt::ResolvedSetpoint r;
    phys.resolve(rt::DriveCmd{2000, 300}, r);
    C("resolve: speed=2000, yaw=300 → valid");
    if (r.motor_speed_mmps == 2000 && r.steer_valid) O; else B("resolve");

    // Brake arbitration
    C("brake max(rt_obs=3000, Jetson=2000) = 3000");
    int32_t bk = rt::brake_arbitrate(3000, 2000);
    if (bk == 3000) O; else B("bk");

    // Obstacle
    C("obstacle at 150mm → speed 0");
    int32_t sp = rt::obstacle_limit_speed(2000, 150);
    if (sp == 0) O; else B("obs0");

    // Steering boot sequence
    C("steer boot sequence completes");
    rt::SteeringControl sc; sc.init();
    can::VcuSesReq out;
    for (int i = 0; i < 25; ++i) sc.tick(INT16_MIN, out);
    sc.tick(0, out);
    if (sc.state() == rt::SteerState::ACTIVE) O; else B("steer");

    // Heartbeat
    rt::DualHeartbeat hb; hb.init();
    can::Frame f_low, f_high;
    hb.tick_low(f_low); hb.tick_high(f_high);
    C("heartbeat low: id=0x7FD, dlc=1");
    if (f_low.id == 0x7FD && f_low.dlc == 1) O; else B("hb_low");
    C("heartbeat counter > 0");
    if (f_low.u8_at(0) > 0) O; else B("hb_ctr");

    // CAN routing (using can_rx_router logic)
    C("route_frame 0x300 → HostDriveCmd parsed");
    can::Frame f300; f300.id = 0x300; f300.dlc = 8;
    f300.put_i32(0, 1500);
    f300.put_u8(4, 0); f300.put_u8(5, 0); f300.put_u8(6, 0);  // i24 yaw=0
    f300.put_u8(7, 0);  // gear=N
    auto cmd = can::HostDriveCmd::from_frame(f300);
    if (cmd.speed_mmps == 1500 && cmd.yaw_rate_mrad_s == 0) O; else B("route300");

    printf("\n  Result: %d failures\n", e);
    return e ? 1 : 0;
}
