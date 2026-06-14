#include <cstdio>
#include "heartbeat.h"
#include "can_rx_router.h"
#include "physics_model.h"
#include "steering_control.h"
#include "obstacle_sensor.h"
#include "brake_arbitration.h"
#include "watchdog.h"
int main(){printf("Phases 35-37: RT watchdog + full pipeline\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
// Watchdog
rt::CmdWatchdog wd;wd.init();
C("watchdog init NOT stale");if(!wd.is_stale(0))O;else B("wd0");
wd.feed(0);
C("after feed, not stale at 400ms");if(!wd.is_stale(400000))O;else B("wd400");
C("stale at 600ms (>500ms)");if(wd.is_stale(600000))O;else B("wd600");
// Full pipeline: 0x300 -> kinematics -> 0x202 + 0x200
auto r=rt::physics_resolve(can::HostDriveCmd{2000,300});
C("full: speed=2000->D, steer valid");if(r.gear==1&&r.valid)O;else B("fp");
C("full: brake max(rt_obs,Jetson)");int32_t bk=rt::brake_arbitrate(3000,2000);
if(bk==3000)O;else B("bk");
// Obstacle
C("obstacle at 150mm: speed->0");int32_t sp=rt::obstacle_limit_speed(2000,150);
if(sp==0)O;else B("obs0");
C("steer boot sequence completes");
rt::SteeringControl sc;sc.init();can::VcuSesReq out;
for(int i=0;i<25;++i) sc.tick(INT16_MIN,out);
sc.tick(0,out);if(sc.state()==rt::SteerState::ACTIVE)O;else B("steer");
printf("\n  Result: %d failures\n",e);return e?1:0;}
