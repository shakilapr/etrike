#include <cstdio>
#include "brake_control.h"
int main(){printf("Phase 17: Brake SEB control\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
sys::BrakeControl bc;bc.init();
can::VcuSebReq out;
C("init=BOOT_WAIT");if(bc.state()==sys::BrakeState::BOOT_WAIT)O;else B("init");
// Tick through boot wait (25 ticks @ 50Hz = 500ms)
bool tx=false;
for(int i=0;i<25;++i) tx=bc.tick(false,false,0,nullptr,out);
C("after 500ms->LISTEN_SYNC");if(bc.state()==sys::BrakeState::LISTEN_SYNC)O;else B("boot");
// Feed SEB status with alignment=1
uint8_t seb[8]={1,0,0,0,0,0,0,0}; // byte0 bit0=1 (aligned)
tx=bc.tick(false,false,0,seb,out);
C("aligned->ACTIVE, sends frame");if(bc.state()==sys::BrakeState::ACTIVE&&tx)O;else B("align");
// Check stroke value (released -> 0mm -> 600 raw)
C("released stroke=600");if(out.stroke_req==600)O;else B("stroke0");
// Lever pressed -> 15mm -> 900 raw
tx=bc.tick(true,false,0,nullptr,out);
C("lever pressed stroke=900");if(out.stroke_req==900)O;else B("lever");
// ESTOP -> 27mm -> 1140 raw
tx=bc.tick(false,true,0,nullptr,out);
C("ESTOP stroke=1140");if(out.stroke_req==1140)O;else B("estop");
// Pressure mode from 0x203
tx=bc.tick(false,false,5000,nullptr,out);
C("brake_kpa=5000 -> Pressure Mode");if(out.control_mode==2)O;else B("pmode");
// Rolling counter increments
uint8_t c1=out.rolling_counter;
tx=bc.tick(false,false,0,nullptr,out);
C("rolling counter increments");if(out.rolling_counter==((c1+1)&0xF))O;else B("roll");
// Pack checksum non-zero
uint8_t raw[8];out.pack(raw);
C("checksum non-zero");if(raw[7]!=0)O;else B("cksum");
printf("\n  Result: %d failures\n",e);return e?1:0;}
