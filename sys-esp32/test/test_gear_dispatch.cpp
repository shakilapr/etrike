#include <cstdio>
#include "gear_control.h"
#include "can_dispatch.h"
int main(){printf("Phases 13-15: Gear + Dispatch\n\n");int err=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++err;}while(0)
sys::GearControl gc;gc.init();
C("gear init=N");if(gc.gear()==can::Gear::N)O;else B("init");
gc.tick(can::Mode::Manual,1,0);C("MAN D->D");if(gc.gear()==can::Gear::D)O;else B("D");
gc.tick(can::Mode::Manual,3,0);C("MAN D+S conflict->N");if(gc.gear()==can::Gear::N)O;else B("conflict");
gc.tick(can::Mode::Auto,0,1);C("AUTO setpoint->D");if(gc.gear()==can::Gear::D)O;else B("autoD");
gc.tick(can::Mode::Estop,1,1);C("ESTOP->N");if(gc.gear()==can::Gear::N)O;else B("estop");

sys::DispatchTargets t;can::RtDriveCmd sp;t.setpoint=&sp;
int32_t bkpa=0;t.brake_kpa=&bkpa;uint8_t lb=0;t.light_bits=&lb;
bool estop=false;t.estop_flag=&estop;uint8_t hb=0;t.rt_hb_ctr=&hb;bool hbr=false;t.rt_hb_received=&hbr;
can::Frame fr;
can::RtDriveCmd{1500,uint8_t(can::Gear::D)}.to_frame(fr);sys::dispatch_frame(fr,t);
C("0x202 dispatch");if(sp.motor_speed_mmps==1500)O;else B("202");
can::RtBrakeCmd{5000}.to_frame(fr);sys::dispatch_frame(fr,t);
C("0x203 dispatch");if(bkpa==5000)O;else B("203");
can::HostLightCmd{true,false,true,false}.to_frame(fr);sys::dispatch_frame(fr,t);
C("0x302 dispatch");if(lb==5)O;else B("302");
fr.id=0x001;sys::dispatch_frame(fr,t);
C("0x001 ESTOP");if(estop)O;else B("001");
fr.id=0x7FD;fr.dlc=1;fr.put_u8(0,42);sys::dispatch_frame(fr,t);
C("0x7FD HB");if(hb==42&&hbr)O;else B("7FD");
fr.id=0x999;sys::dispatch_frame(fr,t);
C("unknown ID ignored");O;
printf("\n  Result: %d failures\n",err);return err?1:0;}
