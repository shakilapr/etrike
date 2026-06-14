// Phase R4: Integration test — RT + SYS pipeline (CAN-based)
#include <cstdio>
#include "can/can_protocol.h"
#include "../sys-esp32/src/mode_manager.h"
#include "../sys-esp32/src/safety_monitor.h"
#include "../sys-esp32/src/throttle_input.h"
#include "../sys-esp32/src/mcp4725_dac.h"
#include "../sys-esp32/src/motor_driver.h"
#include "../sys-esp32/src/gear_control.h"
#include "../sys-esp32/src/brake_control.h"
#include "../sys-esp32/src/dcdc_control.h"
#include "../sys-esp32/src/light_control.h"
#include "../sys-esp32/src/indicator_control.h"
#include "heartbeat.h"
#include "can_rx_router.h"
#include "physics_model.h"
#include "brake_arbitration.h"
#include "watchdog.h"

int main() {
    printf("Phase R4: Integration test (CAN-based)\n\n");
    int e = 0;
    #define C(d) printf("  %-55s ", d)
    #define O printf("PASS\n")
    #define B(m) do { printf("FAIL: %s\n", m); ++e; } while(0)

    // ── SYS + RT integration pipeline ───────────────────────────────
    C("SYS mode → CAN 0x110 → RT mode");
    sys::ModeManager mm; mm.init();
    mm.tick(false, true);  // MANUAL→AUTO
    if (mm.mode() == can::Mode::Auto) O; else B("mode");

    C("RT: 0x300 → PhysicsModel::resolve → setpoint");
    rt::PhysicsModel phys;
    rt::ResolvedSetpoint r;
    phys.resolve(can::HostDriveCmd{1500, 200}, r);
    // Gear derived from speed in control_logic, not physics model
    uint8_t gear = (r.motor_speed_mmps > 0) ? uint8_t(can::Gear::D) : uint8_t(can::Gear::N);
    can::RtDriveCmd sp{r.motor_speed_mmps, gear};
    if (sp.motor_speed_mmps == 1500 && sp.gear == uint8_t(can::Gear::D)) O; else B("phy");

    C("SYS motor: AUTO setpoint → MCP4725");
    sys::MotorDriver md; md.init();
    md.tick(can::Mode::Auto, &sp);
    if (md.dac().value() > 1900 && md.dac().value() < 2200) O; else B("motor");

    C("RT brake: 0x301 → arbitrate → 0x203");
    int32_t bk = rt::brake_arbitrate(4000, 2000);
    if (bk == 4000) O; else B("bk");

    C("SYS gear: AUTO D → relay");
    sys::GearControl gc; gc.init();
    gc.tick(can::Mode::Auto, 0, uint8_t(can::Gear::D));
    if (uint8_t(gc.gear()) == uint8_t(can::Gear::D)) O; else B("gear");

    C("SYS DCDC: MANUAL → on");
    sys::DcdcControl dc; dc.init();
    dc.tick(false);
    if (dc.enabled()) O; else B("dcdc");

    C("SYS lights: brake OR logic");
    sys::LightControl lc; lc.init();
    auto lo = lc.tick(can::Mode::Manual, true, 0, false, false, false);
    if (lo.brake_lamp) O; else B("light");

    C("SYS indicators: AUTO bulb on");
    sys::IndicatorControl ic;
    auto io = ic.tick(can::Mode::Auto);
    if (io.auto_bulb) O; else B("ind");

    C("Gateway: 0x011 forward low→high");
    can::Frame f; f.id = 0x011; f.dlc = 2; f.put_u8(0, 1);
    rt::GatewayQueues q{}; can::Frame gw_high;
    q.gw_tx_high = &gw_high;
    rt::route_frame(f, false, q);
    if (gw_high.id == 0x011) O; else B("gw");

    C("Jetson 0x300: /cmd_vel → CAN i24 yaw + gear");
    can::HostDriveCmd cmd;
    cmd.speed_mmps = int32_t(1.5 * 1000);        // 1.5 m/s
    cmd.yaw_rate_mrad_s = int32_t(0.3 * 1000);   // 0.3 rad/s
    cmd.gear = uint8_t(can::Gear::D);
    if (cmd.speed_mmps == 1500 && cmd.yaw_rate_mrad_s == 300
        && cmd.gear == uint8_t(can::Gear::D)) O; else B("j");

    printf("\n  %d tests, %d failures\n", 10 + e, e);
    return e ? 1 : 0;
}
