#include <cstdio>
#include <cmath>
#include "physics_model.h"
#include "steering_control.h"
#include "obstacle_sensor.h"
#include "brake_arbitration.h"
int main(){printf("Phases 29-34: Physics + Steering + Obstacle + Brake\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
auto r=rt::physics_resolve(can::HostDriveCmd{2000,500});
C("speed=2000,yaw=500 -> D,valid");if(r.gear==1&&r.valid)O;else B("fwd");
C("steer ~18deg");if(abs(r.steer_mdeg)>15000&&abs(r.steer_mdeg)<21000)O;else B("steer");
r=rt::physics_resolve(can::HostDriveCmd{0,100});
C("speed=0->N,invalid");if(r.gear==0&&!r.valid)O;else B("zero");
r=rt::physics_resolve(can::HostDriveCmd{-300,0});
C("speed=-300->R");if(r.gear==3&&r.reversing)O;else B("rev");
// Steering
rt::SteeringControl sc;sc.init();
can::VcuSesReq out;
C("steer init BOOT_WAIT");if(sc.state()==rt::SteerState::BOOT_WAIT)O;else B("sboot");
for(int i=0;i<25;++i) sc.tick(INT16_MIN,out);
C("after 500ms LISTEN_SYNC");if(sc.state()==rt::SteerState::LISTEN_SYNC)O;else B("slist");
sc.tick(455,out); // 45.5deg raw
C("sync->ACTIVE, angle=455");if(sc.state()==rt::SteerState::ACTIVE&&out.target_angle==455)O;else B("sync");
// Obstacle
C("echo 5800us->1000mm");if(rt::obstacle_distance_mm(5800)==994)O;else B("obs");
C("obstacle 200mm->speed 0");if(rt::obstacle_limit_speed(2000,200)==0)O;else B("olim");
C("obstacle 5000mm->full speed");if(rt::obstacle_limit_speed(2000,5000)==2000)O;else B("ofull");
// Brake
C("max(1000,5000)=5000");if(rt::brake_arbitrate(1000,5000)==5000)O;else B("barb");
C("max(8000,3000)=8000");if(rt::brake_arbitrate(8000,3000)==8000)O;else B("barb2");
printf("\n  Result: %d failures\n",e);return e?1:0;}
