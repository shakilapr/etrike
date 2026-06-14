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
int main(){printf("Phases 38-44: Integration\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
sys::ModeManager mm;mm.init();
mm.tick(false,true);
C("SYS mode->CAN 0x110->RT");if(mm.mode()==can::Mode::Auto)O;else B("mode");
rt::PhysicsModel pm;rt::ResolvedSetpoint out;
rt::DriveCmd cmd{1500,200};pm.resolve(cmd,out);
can::RtDriveCmd sp{out.motor_speed_mmps,0};
C("RT: 0x300->physics->0x202");if(sp.motor_speed_mmps>0)O;else B("phy");
sys::MotorDriver md;md.init();
md.tick(can::Mode::Auto,&sp);
C("SYS motor: AUTO->MCP4725");if(md.dac().value()>0)O;else B("motor");
C("RT brake: max(4000,2000)=4000");if(rt::brake_arbitrate(4000,2000)==4000)O;else B("bk");
sys::GearControl gc;gc.init();
gc.tick(can::Mode::Auto,0,1);
C("SYS gear: AUTO D");if(uint8_t(gc.gear())==1)O;else B("gear");
sys::DcdcControl dc;dc.init();dc.tick(false);
C("SYS DCDC: MANUAL->on");if(dc.enabled())O;else B("dcdc");
sys::LightControl lc;lc.init();
auto lo=lc.tick(can::Mode::Manual,true,0,false,false,false);
C("SYS lights: brake OR");if(lo.brake_lamp)O;else B("light");
sys::IndicatorControl ic;
auto io=ic.tick(can::Mode::Auto);
C("SYS indicators: AUTO bulb");if(io.auto_bulb)O;else B("ind");
can::Frame f;f.id=0x011;f.dlc=2;f.put_u8(0,1);
rt::GatewayQueues q;can::Frame gw_high;q.gw_tx_high=&gw_high;
rt::route_frame(f,false,q);
C("Gateway: 0x011 low->high");if(gw_high.id==0x011)O;else B("gw");
can::HostDriveCmd jcmd;jcmd.speed_mmps=1500;jcmd.yaw_rate_mrad_s=300;
C("Jetson 0x300: /cmd_vel->CAN");if(jcmd.speed_mmps==1500)O;else B("j");
printf("\n  %d tests, %d failures\n",10+e,e);return e?1:0;}
