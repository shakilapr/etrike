#include <cstdio>
#include <cmath>
#include "physics_model.h"
#include "steering_control.h"
#include "brake_arbitration.h"
int main(){printf("Phases 29-34: Physics + Steering + Brake\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
rt::PhysicsModel pm;rt::ResolvedSetpoint out;
rt::DriveCmd cmd{2000,500};pm.resolve(cmd,out);
C("speed=2000,yaw=500 -> valid");if(out.steer_valid)O;else B("fwd");
C("steer ~18deg");if(abs(out.steer_angle_mdeg)>15000&&abs(out.steer_angle_mdeg)<21000)O;else B("steer");
rt::DriveCmd cmd2{0,100};pm.resolve(cmd2,out);
C("speed=0,yaw=100 -> valid (min-radius turn)");if(out.steer_valid)O;else B("zero");
rt::DriveCmd cmd3{-300,0};pm.resolve(cmd3,out);
C("speed=-300 -> reversing");if(out.reversing)O;else B("rev");
rt::SteeringControl sc;sc.init();can::VcuSesReq so;
C("steer init BOOT_WAIT");if(sc.state()==rt::SteerState::BOOT_WAIT)O;else B("sboot");
for(int i=0;i<25;++i) sc.tick(INT16_MIN,so);
C("after 500ms LISTEN_SYNC");if(sc.state()==rt::SteerState::LISTEN_SYNC)O;else B("slist");
sc.tick(455,so);
C("sync->ACTIVE, angle=455");if(sc.state()==rt::SteerState::ACTIVE&&so.target_angle==455)O;else B("sync");
C("obstacle 200mm->0");if(rt::PhysicsModel::obstacle_limit(2000,200)==0)O;else B("olim");
C("obstacle 5000mm->full");if(rt::PhysicsModel::obstacle_limit(2000,5000)==2000)O;else B("ofull");
C("max(1000,5000)=5000");if(rt::brake_arbitrate(1000,5000)==5000)O;else B("barb");
printf("\n  Result: %d failures\n",e);return e?1:0;}
