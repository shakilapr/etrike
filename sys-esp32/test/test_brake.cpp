#include <cstdio>
#include "brake_control.h"
int main(){printf("Phase 17: Brake SEB control\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
sys::BrakeControl bc;bc.init();
can::VcuSebReq out;
C("init=BOOT_WAIT");if(bc.state()==sys::BrakeState::BOOT_WAIT)O;else B("init");
bool tx=false;
for(int i=0;i<25;++i) tx=bc.tick(false,false,0,nullptr,out);
C("after 500ms->LISTEN_SYNC");if(bc.state()==sys::BrakeState::LISTEN_SYNC)O;else B("boot");
uint8_t seb[8]={1,0,0,0,0,0,0,0};
tx=bc.tick(false,false,0,seb,out);
C("aligned->ACTIVE, sends frame");if(bc.state()==sys::BrakeState::ACTIVE&&tx)O;else B("align");
C("released stroke=600");if(out.stroke_req==600)O;else B("stroke0");
tx=bc.tick(true,false,0,nullptr,out);
C("lever pressed stroke=900");if(out.stroke_req==900)O;else B("lever");
tx=bc.tick(false,true,0,nullptr,out);
C("ESTOP stroke=1140");if(out.stroke_req==1140)O;else B("estop");
tx=bc.tick(false,false,5000,nullptr,out);
C("brake_kpa=5000 -> Pressure Mode");if(out.control_mode==2)O;else B("pmode");
C("pressure_req u8 = 100 (5000*0.02)");if(out.pressure_req==100)O;else B("press");
uint8_t c1=out.rolling_counter;
tx=bc.tick(false,false,0,nullptr,out);
C("rolling counter increments");if(out.rolling_counter==((c1+1)&0xF))O;else B("roll");
uint8_t raw[8];out.pack(raw);
C("checksum non-zero");if(raw[7]!=0)O;else B("cksum");
C("byte6 roll_cnt_en+cksum_en set");if((raw[6]&0x0C)==0x0C)O;else B("en bits");
C("byte5 reserved (=0)");if(raw[5]==0)O;else B("byte5 not rsvd");
printf("\n  Result: %d failures\n",e);return e?1:0;}
