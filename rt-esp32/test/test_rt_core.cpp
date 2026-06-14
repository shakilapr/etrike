#include <cstdio>
#include "heartbeat.h"
#include "can_rx_router.h"
int main(){printf("Phases 23-28: RT CAN + HB + Gateway\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
// Heartbeat
rt::DualHeartbeat hb;hb.init();can::Frame f;
C("init ctr=0");if(hb.ctr()==0)O;else B("init");
hb.tick_low(f);C("tick low ID=0x7FD DLC=1");if(f.id==0x7FD&&f.dlc==1)O;else B("low");
hb.tick_high(f);C("tick high same ctr");if(f.u8_at(0)==1)O;else B("high");
// Gateway routing
rt::GatewayQueues q;can::Frame gw_low,gw_high;q.gw_tx_low=&gw_low;q.gw_tx_high=&gw_high;
can::HostDriveCmd cmd;q.cmd=&cmd;int32_t bkpa=0;q.brake_req_kpa=&bkpa;
bool estop=false;q.estop_flag=&estop;uint8_t mode=0;q.mode_from_sys=&mode;
int16_t ang=0;q.steer_feedback_angle=&ang;

can::Frame fr;fr.id=0x011;fr.dlc=2;fr.put_u8(0,1);
rt::route_frame(fr,false,q);C("low 0x011->gw_high");if(gw_high.id==0x011)O;else B("f011");
fr.id=0x300;fr.dlc=8;fr.put_i32(0,1500);fr.put_i32(4,-200);
rt::route_frame(fr,true,q);C("high 0x300->cmd");if(cmd.speed_mmps==1500&&cmd.yaw_rate_mrad_s==-200)O;else B("300");
fr.id=0x301;fr.dlc=4;fr.put_i32(0,8000);
rt::route_frame(fr,true,q);C("high 0x301->brake_kpa");if(bkpa==8000)O;else B("301");
fr.id=0x302;fr.dlc=1;fr.put_u8(0,5);
rt::route_frame(fr,true,q);C("high 0x302->gw_low");if(gw_low.id==0x302)O;else B("302");
fr.id=0x110;fr.dlc=1;fr.put_u8(0,1);
rt::route_frame(fr,false,q);C("low 0x110->mode");if(mode==1)O;else B("110");
fr.id=0x201;fr.dlc=8;fr.data[2]=55;fr.data[3]=1; // 311 raw = int16(55|1<<8)
rt::route_frame(fr,false,q);
C("low 0x201->steer angle");if(ang==311)O;else B("201");
fr.id=0x001;rt::route_frame(fr,false,q);
C("0x001->estop");if(estop)O;else B("001");
printf("\n  Result: %d failures\n",e);return e?1:0;}
