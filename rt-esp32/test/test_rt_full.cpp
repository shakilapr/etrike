#include <cstdio>
#include "heartbeat.h"
#include "can_rx_router.h"
#include "physics_model.h"
#include "steering_control.h"
#include "brake_arbitration.h"
#include "watchdog.h"
int main(){printf("Phases 35-37: RT full pipeline\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
rt::CmdWatchdog wd;wd.init();
C("watchdog init NOT stale");if(!wd.is_stale(0))O;else B("wd0");
wd.feed(0);
C("not stale at 400ms");if(!wd.is_stale(400000))O;else B("wd400");
C("stale at 600ms");if(wd.is_stale(600000))O;else B("wd600");
rt::PhysicsModel pm;rt::ResolvedSetpoint out;
rt::DriveCmd cmd{2000,300};pm.resolve(cmd,out);
C("full: speed=2000, valid");if(out.steer_valid)O;else B("fp");
C("brake max(3000,2000)=3000");if(rt::brake_arbitrate(3000,2000)==3000)O;else B("bk");
C("obstacle 150mm->0");if(rt::PhysicsModel::obstacle_limit(2000,150)==0)O;else B("obs0");
rt::SteeringControl sc;sc.init();can::VcuSesReq so;
for(int i=0;i<25;++i) sc.tick(INT16_MIN,so);
sc.tick(0,so);
C("steer boot complete");if(sc.state()==rt::SteerState::ACTIVE)O;else B("steer");
printf("\n  Result: %d failures\n",e);return e?1:0;}
