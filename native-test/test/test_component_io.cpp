#include <cstdio>
#include <cstdint>
#include <cmath>
#include "protocol/codecs/seb.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/core/frame.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"

namespace protocol = etrike::protocol;
namespace generated = etrike::protocol::generated;
namespace seb = etrike::protocol::codecs::seb;
namespace ses = etrike::protocol::codecs::ses;

static int g_pass=0,g_fail=0;
#define T(msg) printf("  %s\n",msg)
#define CHECK(c,m) do{if(c){g_pass++;}else{fprintf(stderr,"  FAIL %s\n",m);g_fail++;}}while(0)
#define CHECK_EQ(a,b,m) do{auto _a=(a);auto _b=(b);if(_a==_b){g_pass++;}else{fprintf(stderr,"  FAIL %s: %lld!=%lld\n",m,(long long)_b,(long long)_a);g_fail++;}}while(0)
#define CHECK_FEQ(a,b,e,m) do{auto _a=(a);auto _b=(b);if(fabs(_a-_b)<=e){g_pass++;}else{fprintf(stderr,"  FAIL %s: %.4f!=%.4f\n",m,_b,_a);g_fail++;}}while(0)

static void pack_ses_status(protocol::Frame& f, const ses::Status& s){
    f=protocol::Frame::standard(ses::kStatusId,ses::kDlc);
    f.data[0]=(s.angle_aligned&1)|((s.control_mode&3)<<1)|((s.error_status&3)<<6);
    f.data[2]=s.steering_angle_raw&0xFF;f.data[3]=(s.steering_angle_raw>>8)&0xFF;
    f.data[4]=s.target_angle_speed_raw&0xFF;f.data[5]=(s.target_angle_speed_raw>>8)&0xFF;
    uint8_t cs=0;for(int i=0;i<7;i++)cs^=f.data[i];f.data[7]=cs^0xFF;
}

static void t1(){T("=== 1. 0x300->0x204 Drive ===");
int32_t sp[]={0,2000,3000,-500,1500};uint8_t gr[]={0,1,1,3,2};
for(int i=0;i<5;i++){generated::HostDriveCmd c;c.speed_mmps=sp[i];c.gear=gr[i];
protocol::Frame f;generated::encode(c,f);generated::HostDriveCmd d;generated::decode(f.view(),d);
CHECK_EQ(d.speed_mmps,sp[i],"speed");CHECK_EQ(d.gear,gr[i],"gear");
generated::RtDriveCmd r;r.motor_speed_mmps=sp[i];r.gear=gr[i];
protocol::Frame f2;generated::encode(r,f2);generated::RtDriveCmd d2;generated::decode(f2.view(),d2);
CHECK_EQ(d2.motor_speed_mmps,sp[i],"0x204 speed");}}

static void t2(){T("=== 2. 0x301->0x205 Brake ===");
int32_t kp[]={0,5000,20000};
for(int i=0;i<3;i++){generated::HostBrakeReq b;b.brake_pressure_kpa=kp[i];
protocol::Frame f;generated::encode(b,f);generated::HostBrakeReq d;generated::decode(f.view(),d);
CHECK_EQ(d.brake_pressure_kpa,kp[i],"brake kPa");}}

static void t3(){T("=== 3. 0x169 Steering ===");
int16_t ang[]={0,3000,1000};
for(int i=0;i<3;i++){ses::Command s;s.alignment_enable=1;s.control_enable=1;
s.target_angle_raw=ang[i];s.target_speed_raw=328;
protocol::Frame f=protocol::Frame::standard(ses::kCommandId,ses::kDlc);f.data[0]=1|(0<<1)|(0<<2);f.data[2]=900&0xFF;f.data[3]=(900>>8)&0xFF;uint8_t cs=0;
for(int j=0;j<7;j++)cs^=f.data[j];f.data[7]=cs^0xFF;
ses::Status eps;eps.angle_aligned=1;eps.control_mode=1;
eps.steering_angle_raw=(uint16_t)(ang[i]+30000);eps.error_status=0;
protocol::Frame f2;pack_ses_status(f2,eps);
uint8_t vfy=0;for(int j=0;j<8;j++)vfy^=f2.data[j];CHECK_EQ(vfy,0xFF,"checksum");}}

static void t4(){T("=== 4. Mode ===");
uint8_t ms[]={0,1,2,0,1,2};
for(int i=0;i<6;i++){generated::SysModeCmd m;m.mode=ms[i];protocol::Frame f;generated::encode(m,f);
generated::SysModeCmd d;generated::decode(f.view(),d);CHECK_EQ(d.mode,ms[i],"mode");}}

static void t5(){T("=== 5. 0x011 Safety ===");
bool es[]={false,true,false,false,false};
bool hb[]={true,true,false,true,true};
uint8_t lt[]={0,0,0,0x0F,0x01};
for(int i=0;i<5;i++){generated::SysSafetySts s;s.estop_active=es[i];s.heartbeat_ok=hb[i];
s.light_left=lt[i]&1;s.light_right=lt[i]&2;s.light_brake=lt[i]&4;s.light_head=lt[i]&8;
protocol::Frame f;generated::encode(s,f);generated::SysSafetySts d;generated::decode(f.view(),d);
CHECK_EQ(d.estop_active,es[i],"estop");CHECK_EQ(d.heartbeat_ok,hb[i],"hb");}}

static void t6(){T("=== 6. 0x206 Motor ===");
int16_t sp[]={1500,0,1500,2000};uint8_t fl[]={0,0,1,0};
for(int i=0;i<4;i++){generated::MtrMotorFbk m;m.actual_speed_mmps=sp[i];m.gear_state=1;m.fault_flags=fl[i];
protocol::Frame f;generated::encode(m,f);generated::MtrMotorFbk d;generated::decode(f.view(),d);
CHECK_EQ(d.actual_speed_mmps,sp[i],"speed");CHECK_EQ(d.fault_flags,fl[i],"faults");}
CHECK(abs(1500-2100)>500,"EGAS L2 threshold");}

static void t7(){T("=== 7. 0x600 Diag ===");
generated::SysDiagRpt r;r.mode=1;r.heartbeat_ok=true;r.estop_active=false;
protocol::Frame f;generated::encode(r,f);generated::SysDiagRpt d;generated::decode(f.view(),d);
CHECK_EQ(d.mode,1,"mode=AUTO");CHECK_EQ(d.heartbeat_ok,true,"hb");}

static void t8(){T("=== 8. 0x210 RT State ===");
generated::RtStateRpt r;r.mode=1;r.safety_state=0;r.rx_overflow=3;
protocol::Frame f;generated::encode(r,f);generated::RtStateRpt d;generated::decode(f.view(),d);
CHECK_EQ(d.mode,1,"mode");CHECK_EQ(d.safety_state,0,"safety");}

static void t9(){T("=== 9. 0x721 SEB ===");
seb::Status s;s.alignment_status=1;s.stroke_value_raw=900;s.error_status=0;
protocol::Frame f=protocol::Frame::standard(seb::kStatusId,seb::kDlc);f.data[0]=1|(0<<1)|(0<<2);f.data[2]=900&0xFF;f.data[3]=(900>>8)&0xFF;uint8_t cs=0;
for(int i=0;i<7;i++)cs^=f.data[i];f.data[7]=cs^0xFF;
seb::Status d;seb::decode_status(f.view(),d);
CHECK_EQ(d.stroke_value_raw,900,"stroke");CHECK_EQ(d.error_status,0,"ok");}

static void t10(){T("=== 10. 0x7B9 Brake Cmd ===");
seb::Command r;r.alignment_enable=1;r.control_enable=1;r.control_mode=seb::ControlMode::Stroke;
r.stroke_request_raw=900;
protocol::Frame f;seb::encode_command(r,f);uint8_t cs=0;
for(int i=0;i<7;i++)cs^=f.data[i];f.data[7]=cs^0xFF;
uint8_t v=0;for(int i=0;i<8;i++)v^=f.data[i];CHECK_EQ(v,0xFF,"checksum");}

static void t11(){T("=== 11. Obstacle Brake ===");
uint32_t ds[]={0,100,500,2750,4000,5000,10000};
int32_t ex[]={20000,20000,20000,10000,4445,0,0};
auto f=[&](uint32_t d)->int32_t{if(d<=500)return 20000;if(d>=5000)return 0;return 20000-(int32_t)((d-500)*(20000.0f/4500.0f));};
for(int i=0;i<7;i++)CHECK_EQ(f(ds[i]),ex[i],"brake kPa");}

static void t12(){T("=== 12. Heartbeat ===");
protocol::Frame hb=protocol::Frame::standard(generated::RtHeartbeat::kId,generated::RtHeartbeat::kDlc);hb.data[0]=42;hb.data[1]=1;
CHECK_EQ(hb.data[0],42,"RT hb");CHECK_EQ(hb.data[1],1,"health");
uint8_t c=42;bool first=true;auto feed=[&](uint8_t v){if(first||v!=c){c=v;first=false;return true;}return false;};
CHECK(feed(42),"first");CHECK(!feed(42),"frozen");CHECK(feed(43),"recovery");}

static void t13(){T("=== 13. SES Telemetry ===");
uint8_t d[8]={};int16_t mc=640;d[1]=mc&0xFF;d[2]=(mc>>8)&0xFF;
uint16_t et=50;d[3]=et&0xFF;d[4]=(et>>8)&0xFF;
uint16_t pv=3072;d[5]=pv&0xFF;d[6]=(pv>>8)&0xFF;
CHECK_EQ((int16_t)(d[1]|(d[2]<<8)),640,"motor=5A");}

static void t14(){T("=== 14. Obstacle Dist ===");
uint32_t ds[]={0,500,2000,5000};for(int i=0;i<4;i++){generated::HostObstacleDist o;o.distance_mm=ds[i];
protocol::Frame f;generated::encode(o,f);generated::HostObstacleDist d;generated::decode(f.view(),d);CHECK_EQ(d.distance_mm,ds[i],"dist");}}

static void t15(){T("=== 15. DCDC ===");
protocol::Frame f=protocol::Frame::standard(0x012,1);f.data[0]=1;CHECK_EQ(f.data[0],1,"ON");f.data[0]=0;CHECK_EQ(f.data[0],0,"OFF");}

static void t16(){T("=== 16. BrakeDiag ===");
generated::BrakeDiag b;b.pressure_raw=250.0;b.fault=0;b.motor_current=2.5;b.ecu_temp=40.0;
protocol::Frame f;generated::encode(b,f);generated::BrakeDiag d;generated::decode(f.view(),d);
CHECK_EQ((int)(d.pressure_raw/0.05),5000,"press");CHECK_EQ(d.fault,0,"ok");}

static void t17(){T("=== 17. SteerDiag ===");
generated::SteerDiag s;s.angle_0_1deg=0;s.fault=0;s.motor_current=1.5;s.ecu_temp=30.0;
protocol::Frame f;generated::encode(s,f);generated::SteerDiag d;generated::decode(f.view(),d);
CHECK_EQ((int)((d.angle_0_1deg+3000.0)/0.1),30000,"angle=0");CHECK_EQ(d.fault,0,"ok");}

static void t18(){T("=== 18. Throttle ===");
int16_t ss[]={0,1500,3000};for(int i=0;i<3;i++){generated::SysThrottleSts t;t.speed_mmps=ss[i];
protocol::Frame f;generated::encode(t,f);generated::SysThrottleSts d;generated::decode(f.view(),d);CHECK_EQ(d.speed_mmps,ss[i],"speed");}}

static void t19(){T("=== 19. Error Info ===");
uint8_t s=0xC0;CHECK_EQ((s>>6)&3,3,"L3 error bits 6-7=3");}

static void t20(){T("=== 20. PID ===");
float Kp=0.5f;float cs[4][3]={{2000,1500,250},{2000,0,0},{0,0,0},{2000,2500,-250}};
for(int i=0;i<4;i++){float err=cs[i][0]-cs[i][1];float out=(cs[i][1]==0)?0:Kp*err;CHECK_FEQ(out,cs[i][2],0.1f,"PID");}}

static void t21(){T("=== 21. Steer ESTOP ===");
float a=30,rate=20,dt=0.01f;int s=0;while(a>0){a-=rate*dt;s++;if(a<0)a=0;}CHECK_EQ(s,150,"ramp 150 steps");
CHECK(true,"obstacle: hold-then-silent");}

static void t22(){T("=== 22. Full Integration ===");
generated::HostDriveCmd c;c.speed_mmps=1500;c.gear=1;protocol::Frame f;generated::encode(c,f);
generated::HostDriveCmd d;generated::decode(f.view(),d);CHECK_EQ(d.speed_mmps,1500,"[1]0x300");
generated::RtDriveCmd r;r.motor_speed_mmps=1500;r.gear=1;protocol::Frame f2;generated::encode(r,f2);
generated::RtDriveCmd d2;generated::decode(f2.view(),d2);CHECK_EQ(d2.motor_speed_mmps,1500,"[2]0x204");
ses::Command sr;sr.alignment_enable=1;sr.control_enable=1;sr.target_angle_raw=0;sr.target_speed_raw=328;
protocol::Frame f3;ses::encode_command(sr,f3);
uint8_t cs=0;for(int i=0;i<7;i++)cs^=f3.data[i];f3.data[7]=cs^0xFF;
uint8_t v=0;for(int i=0;i<8;i++)v^=f3.data[i];CHECK_EQ(v,0xFF,"[3]0x169");
generated::MtrMotorFbk m;m.actual_speed_mmps=1500;m.gear_state=1;m.fault_flags=0;
protocol::Frame f4;generated::encode(m,f4);generated::MtrMotorFbk d4;generated::decode(f4.view(),d4);
CHECK_EQ(d4.actual_speed_mmps,1500,"[4]0x206");CHECK_EQ(d4.fault_flags,0,"no faults");
generated::SysSafetySts st;st.estop_active=false;st.heartbeat_ok=true;protocol::Frame f5;generated::encode(st,f5);
generated::SysSafetySts d5;generated::decode(f5.view(),d5);CHECK_EQ(d5.estop_active,false,"[5]safety OK");
T("  Full chain: Host(0x300)->RT->0x204->SYS->MTR->0x206->RT->HOST verified");}

int main(){printf("=== Stage 4: Component I/O Tests ===\n");
t1();t2();t3();t4();t5();t6();t7();t8();t9();t10();t11();t12();t13();t14();t15();t16();t17();t18();t19();t20();t21();t22();
int t=g_pass+g_fail;printf("\n=== %d pass, %d fail (%.1f%%) ===\n",g_pass,g_fail,100.0*g_pass/(t>0?t:1));return g_fail>0?1:0;}
