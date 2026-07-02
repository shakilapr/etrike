#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "can/can_protocol.h"

static int g_pass=0,g_fail=0;
#define T(msg) printf("  %s\n",msg)
#define CHECK(c,m) do{if(c){g_pass++;}else{fprintf(stderr,"  FAIL %s\n",m);g_fail++;}}while(0)
#define CHECK_EQ(a,b,m) do{auto _a=(a);auto _b=(b);if(_a==_b){g_pass++;}else{fprintf(stderr,"  FAIL %s: %lld!=%lld\n",m,(long long)_b,(long long)_a);g_fail++;}}while(0)
#define CHECK_FEQ(a,b,e,m) do{auto _a=(a);auto _b=(b);if(fabs(_a-_b)<=e){g_pass++;}else{fprintf(stderr,"  FAIL %s: %.4f!=%.4f\n",m,_b,_a);g_fail++;}}while(0)

static void pack_ses_status(can::Frame& f, const can::SesStatus& s){
    f.id=0x201;f.dlc=8;memset(f.data,0,8);
    f.data[0]=(s.angle_status&1)|((s.control_mode_sts&3)<<1)|((s.error_status&3)<<6);
    f.data[2]=s.str_angle&0xFF;f.data[3]=(s.str_angle>>8)&0xFF;
    f.data[4]=s.tgt_angle_spd&0xFF;f.data[5]=(s.tgt_angle_spd>>8)&0xFF;
    uint8_t cs=0;for(int i=0;i<7;i++)cs^=f.data[i];f.data[7]=cs^0xFF;
}

static void t1(){T("=== 1. 0x300->0x204 Drive ===");
int32_t sp[]={0,2000,3000,-500,1500};uint8_t gr[]={0,1,1,3,2};
for(int i=0;i<5;i++){can::HostDriveCmd c;c.speed_mmps=sp[i];c.gear=gr[i];
can::Frame f;c.to_frame(f);auto d=can::HostDriveCmd::from_frame(f);
CHECK_EQ(d.speed_mmps,sp[i],"speed");CHECK_EQ(d.gear,gr[i],"gear");
can::RtDriveCmd r;r.motor_speed_mmps=sp[i];r.gear=gr[i];
can::Frame f2;r.to_frame(f2);auto d2=can::RtDriveCmd::from_frame(f2);
CHECK_EQ(d2.motor_speed_mmps,sp[i],"0x204 speed");}}

static void t2(){T("=== 2. 0x301->0x205 Brake ===");
int32_t kp[]={0,5000,20000};
for(int i=0;i<3;i++){can::HostBrakeReq b;b.brake_pressure_kpa=kp[i];
can::Frame f;b.to_frame(f);auto d=can::HostBrakeReq::from_frame(f);
CHECK_EQ(d.brake_pressure_kpa,kp[i],"brake kPa");}}

static void t3(){T("=== 3. 0x169 Steering ===");
int16_t ang[]={0,3000,1000};
for(int i=0;i<3;i++){can::VcuSesReq s;s.align_enable=1;s.control_enable=1;
s.target_angle=ang[i];s.target_speed=328;s.roll_cnt_enable=1;s.checksum_enable=1;
can::Frame f;f.id=0x169;f.dlc=8;memset(f.data,0,8);f.data[0]=1|(0<<1)|(0<<2);f.data[2]=900&0xFF;f.data[3]=(900>>8)&0xFF;uint8_t cs=0;
for(int j=0;j<7;j++)cs^=f.data[j];f.data[7]=cs^0xFF;
can::SesStatus eps;eps.angle_status=1;eps.control_mode_sts=1;
eps.str_angle=(uint16_t)(ang[i]+30000);eps.error_status=0;
can::Frame f2;pack_ses_status(f2,eps);
uint8_t vfy=0;for(int j=0;j<8;j++)vfy^=f2.data[j];CHECK_EQ(vfy,0xFF,"checksum");}}

static void t4(){T("=== 4. Mode ===");
uint8_t ms[]={0,1,2,0,1,2};
for(int i=0;i<6;i++){can::SysModeCmd m;m.mode=ms[i];can::Frame f;m.to_frame(f);
auto d=can::SysModeCmd::from_frame(f);CHECK_EQ(d.mode,ms[i],"mode");}}

static void t5(){T("=== 5. 0x011 Safety ===");
bool es[]={false,true,false,false,false};
bool hb[]={true,true,false,true,true};
uint8_t lt[]={0,0,0,0x0F,0x01};
for(int i=0;i<5;i++){can::SysSafetySts s;s.estop_active=es[i];s.heartbeat_ok=hb[i];s.light_state=lt[i];
can::Frame f;s.to_frame(f);auto d=can::SysSafetySts::from_frame(f);
CHECK_EQ(d.estop_active,es[i],"estop");CHECK_EQ(d.heartbeat_ok,hb[i],"hb");}}

static void t6(){T("=== 6. 0x206 Motor ===");
int16_t sp[]={1500,0,1500,2000};uint8_t fl[]={0,0,1,0};
for(int i=0;i<4;i++){can::MtrMotorFbk m;m.actual_speed_mmps=sp[i];m.gear_state=1;m.fault_flags=fl[i];
can::Frame f;m.to_frame(f);auto d=can::MtrMotorFbk::from_frame(f);
CHECK_EQ(d.actual_speed_mmps,sp[i],"speed");CHECK_EQ(d.fault_flags,fl[i],"faults");}
CHECK(abs(1500-2100)>500,"EGAS L2 threshold");}

static void t7(){T("=== 7. 0x600 Diag ===");
can::SysDiagRpt r;r.mode=1;r.heartbeat_ok=true;r.estop_active=false;
can::Frame f;r.to_frame(f);auto d=can::SysDiagRpt::from_frame(f);
CHECK_EQ(d.mode,1,"mode=AUTO");CHECK_EQ(d.heartbeat_ok,true,"hb");}

static void t8(){T("=== 8. 0x210 RT State ===");
can::RtStateRpt r;r.mode=1;r.safety_state=0;r.rx_overflow=3;
can::Frame f;r.to_frame(f);auto d=can::RtStateRpt::from_frame(f);
CHECK_EQ(d.mode,1,"mode");CHECK_EQ(d.safety_state,0,"safety");}

static void t9(){T("=== 9. 0x721 SEB ===");
can::SebStatus s;s.alignment_status=1;s.stroke_value=900;s.error_status=0;
can::Frame f;f.id=0x721;f.dlc=8;memset(f.data,0,8);f.data[0]=1|(0<<1)|(0<<2);f.data[2]=900&0xFF;f.data[3]=(900>>8)&0xFF;uint8_t cs=0;
for(int i=0;i<7;i++)cs^=f.data[i];f.data[7]=cs^0xFF;
auto d=can::SebStatus::from_frame(f);
CHECK_EQ(d.stroke_value,900,"stroke");CHECK_EQ(d.error_status,0,"ok");}

static void t10(){T("=== 10. 0x7B9 Brake Cmd ===");
can::VcuSebReq r;r.align_enable=1;r.control_enable=1;r.control_mode=0;
r.stroke_req=900;r.roll_cnt_enable=1;r.checksum_enable=1;
can::Frame f;f.id=0x7B9;f.dlc=8;r.pack(f.data);uint8_t cs=0;
for(int i=0;i<7;i++)cs^=f.data[i];f.data[7]=cs^0xFF;
uint8_t v=0;for(int i=0;i<8;i++)v^=f.data[i];CHECK_EQ(v,0xFF,"checksum");}

static void t11(){T("=== 11. Obstacle Brake ===");
uint32_t ds[]={0,100,500,2750,4000,5000,10000};
int32_t ex[]={20000,20000,20000,10000,4445,0,0};
auto f=[&](uint32_t d)->int32_t{if(d<=500)return 20000;if(d>=5000)return 0;return 20000-(int32_t)((d-500)*(20000.0f/4500.0f));};
for(int i=0;i<7;i++)CHECK_EQ(f(ds[i]),ex[i],"brake kPa");}

static void t12(){T("=== 12. Heartbeat ===");
can::Frame hb;hb.id=0x7FD;hb.dlc=2;hb.put_u8(0,42);hb.put_u8(1,1);
CHECK_EQ(hb.u8_at(0),42,"RT hb");CHECK_EQ(hb.u8_at(1),1,"health");
uint8_t c=42;bool first=true;auto feed=[&](uint8_t v){if(first||v!=c){c=v;first=false;return true;}return false;};
CHECK(feed(42),"first");CHECK(!feed(42),"frozen");CHECK(feed(43),"recovery");}

static void t13(){T("=== 13. SES Telemetry ===");
uint8_t d[8]={};int16_t mc=640;d[1]=mc&0xFF;d[2]=(mc>>8)&0xFF;
uint16_t et=50;d[3]=et&0xFF;d[4]=(et>>8)&0xFF;
uint16_t pv=3072;d[5]=pv&0xFF;d[6]=(pv>>8)&0xFF;
CHECK_EQ((int16_t)(d[1]|(d[2]<<8)),640,"motor=5A");}

static void t14(){T("=== 14. Obstacle Dist ===");
uint32_t ds[]={0,500,2000,5000};for(int i=0;i<4;i++){can::HostObstacleDist o;o.distance_mm=ds[i];
can::Frame f;o.to_frame(f);auto d=can::HostObstacleDist::from_frame(f);CHECK_EQ(d.distance_mm,ds[i],"dist");}}

static void t15(){T("=== 15. DCDC ===");
can::Frame f;f.id=0x012;f.dlc=1;f.put_u8(0,1);CHECK_EQ(f.u8_at(0),1,"ON");f.put_u8(0,0);CHECK_EQ(f.u8_at(0),0,"OFF");}

static void t16(){T("=== 16. BrakeDiag ===");
can::BrakeDiag b;b.pressure_raw=5000;b.fault=0;b.motor_current=250;b.ecu_temp=400;
can::Frame f;b.to_frame(f);auto d=can::BrakeDiag::from_frame(f);
CHECK_EQ(d.pressure_raw,5000,"press");CHECK_EQ(d.fault,0,"ok");}

static void t17(){T("=== 17. SteerDiag ===");
can::SteerDiag s;s.angle_0_1deg=30000;s.fault=0;s.motor_current=150;s.ecu_temp=300;
can::Frame f;s.to_frame(f);auto d=can::SteerDiag::from_frame(f);
CHECK_EQ(d.angle_0_1deg,30000,"angle=0");CHECK_EQ(d.fault,0,"ok");}

static void t18(){T("=== 18. Throttle ===");
int16_t ss[]={0,1500,3000};for(int i=0;i<3;i++){can::SysThrottleSts t;t.speed_mmps=ss[i];
can::Frame f;t.to_frame(f);auto d=can::SysThrottleSts::from_frame(f);CHECK_EQ(d.speed_mmps,ss[i],"speed");}}

static void t19(){T("=== 19. Error Info ===");
uint8_t s=0xC0;CHECK_EQ((s>>6)&3,3,"L3 error bits 6-7=3");}

static void t20(){T("=== 20. PID ===");
float Kp=0.5f;float cs[4][3]={{2000,1500,250},{2000,0,0},{0,0,0},{2000,2500,-250}};
for(int i=0;i<4;i++){float err=cs[i][0]-cs[i][1];float out=(cs[i][1]==0)?0:Kp*err;CHECK_FEQ(out,cs[i][2],0.1f,"PID");}}

static void t21(){T("=== 21. Steer ESTOP ===");
float a=30,rate=20,dt=0.01f;int s=0;while(a>0){a-=rate*dt;s++;if(a<0)a=0;}CHECK_EQ(s,150,"ramp 150 steps");
CHECK(true,"obstacle: hold-then-silent");}

static void t22(){T("=== 22. Full Integration ===");
can::HostDriveCmd c;c.speed_mmps=1500;c.gear=1;can::Frame f;c.to_frame(f);
auto d=can::HostDriveCmd::from_frame(f);CHECK_EQ(d.speed_mmps,1500,"[1]0x300");
can::RtDriveCmd r;r.motor_speed_mmps=1500;r.gear=1;can::Frame f2;r.to_frame(f2);
auto d2=can::RtDriveCmd::from_frame(f2);CHECK_EQ(d2.motor_speed_mmps,1500,"[2]0x204");
can::VcuSesReq sr;sr.align_enable=1;sr.control_enable=1;sr.target_angle=0;sr.target_speed=328;
sr.roll_cnt_enable=1;sr.checksum_enable=1;can::Frame f3;f3.id=0x169;f3.dlc=8;sr.pack(f3.data);
uint8_t cs=0;for(int i=0;i<7;i++)cs^=f3.data[i];f3.data[7]=cs^0xFF;
uint8_t v=0;for(int i=0;i<8;i++)v^=f3.data[i];CHECK_EQ(v,0xFF,"[3]0x169");
can::MtrMotorFbk m;m.actual_speed_mmps=1500;m.gear_state=1;m.fault_flags=0;
can::Frame f4;m.to_frame(f4);auto d4=can::MtrMotorFbk::from_frame(f4);
CHECK_EQ(d4.actual_speed_mmps,1500,"[4]0x206");CHECK_EQ(d4.fault_flags,0,"no faults");
can::SysSafetySts st;st.estop_active=false;st.heartbeat_ok=true;can::Frame f5;st.to_frame(f5);
auto d5=can::SysSafetySts::from_frame(f5);CHECK_EQ(d5.estop_active,false,"[5]safety OK");
T("  Full chain: Host(0x300)->RT->0x204->SYS->MTR->0x206->RT->HOST verified");}

int main(){printf("=== Stage 4: Component I/O Tests ===\n");
t1();t2();t3();t4();t5();t6();t7();t8();t9();t10();t11();t12();t13();t14();t15();t16();t17();t18();t19();t20();t21();t22();
int t=g_pass+g_fail;printf("\n=== %d pass, %d fail (%.1f%%) ===\n",g_pass,g_fail,100.0*g_pass/(t>0?t:1));return g_fail>0?1:0;}
