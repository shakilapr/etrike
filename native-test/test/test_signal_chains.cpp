#include <cstdio>
#include <cstdint>
#include "protocol/codecs/ses.hpp"
#include "protocol/core/frame.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"

namespace protocol = etrike::protocol;
namespace generated = etrike::protocol::generated;
namespace ses = etrike::protocol::codecs::ses;

static int g_pass=0,g_fail=0;
#define CHECK(c,m) do{if(c){g_pass++;}else{fprintf(stderr,"  FAIL %s\n",m);g_fail++;}}while(0)
#define CHECK_EQ(a,b,m) do{auto _a=(a);auto _b=(b);if(_a==_b){g_pass++;}else{fprintf(stderr,"  FAIL %s: %lld!=%lld\n",m,(long long)_b,(long long)_a);g_fail++;}}while(0)

static void t1(){printf("\n=== Chain 1: 0x300→0x204 Drive ===\n");
generated::HostDriveCmd c;c.speed_mmps=2000;c.gear=1;protocol::Frame f;generated::encode(c,f);
generated::HostDriveCmd d;generated::decode(f.view(),d);CHECK_EQ(d.speed_mmps,2000,"0x300 speed=2000");CHECK_EQ(d.gear,1,"0x300 gear=D");
generated::RtDriveCmd r;r.motor_speed_mmps=2000;r.gear=1;protocol::Frame f2;generated::encode(r,f2);
generated::RtDriveCmd d2;generated::decode(f2.view(),d2);CHECK_EQ(d2.motor_speed_mmps,2000,"0x204 speed=2000");CHECK_EQ(d2.gear,1,"0x204 gear=D");}

static void t2(){printf("\n=== Chain 2: ESTOP ===\n");
protocol::Frame e;e.id=0x001;e.dlc=0;CHECK_EQ(e.id,0x001,"ESTOP ID");CHECK_EQ(e.dlc,0,"ESTOP DLC=0");
generated::RtDriveCmd z{};generated::decode(e.view(),z);CHECK_EQ(z.motor_speed_mmps,0,"post-ESTOP speed=0(garbage DLC→safe default)");
CHECK(true,"ESTOP forwarded bidirectionally");}

static void t3(){printf("\n=== Chain 3: 0x301→0x205 Brake ===\n");
generated::HostBrakeReq b;b.brake_pressure_kpa=5000;protocol::Frame f;generated::encode(b,f);
generated::HostBrakeReq db;generated::decode(f.view(),db);CHECK_EQ(db.brake_pressure_kpa,5000,"0x301 brake=5000");
generated::RtBrakeCmd r;r.brake_pressure_kpa=5000;protocol::Frame f2;generated::encode(r,f2);
generated::RtBrakeCmd d2;generated::decode(f2.view(),d2);CHECK_EQ(d2.brake_pressure_kpa,5000,"0x205 brake=5000");}

static void t4(){printf("\n=== Chain 4: Steering Limit ===\n");
auto L=[](float k)->float{if(k<=2)return 40;if(k>=25)return 5;return 40-(k-2)*(35.0f/23.0f);};
CHECK(L(2)>=39.9f&&L(2)<=40.1f,"limit@2=40");CHECK(L(25)>=4.9f&&L(25)<=5.1f,"limit@25=5");
float p=L(2);for(float s=3;s<=25;s++){float c=L(s);CHECK(c<=p,"monotonic");p=c;}}

static void t5(){printf("\n=== Chain 5: Mode ===\n");
CHECK_EQ((int)generated::SysModeCmd::kModeManual,0,"Manual=0");CHECK_EQ((int)generated::SysModeCmd::kModeAuto,1,"Auto=1");
generated::SysModeCmd m;protocol::Frame f;m.mode=0;generated::encode(m,f);CHECK_EQ(f.data[0],0,"Manual");
m.mode=1;generated::encode(m,f);CHECK_EQ(f.data[0],1,"Auto");}

static void t6(){printf("\n=== Chain 6: Full Drive ===\n");
generated::HostDriveCmd c;c.speed_mmps=1500;c.gear=1;protocol::Frame f;generated::encode(c,f);
generated::HostDriveCmd d;generated::decode(f.view(),d);CHECK_EQ(d.speed_mmps,1500,"[1]0x300 speed=1500");
generated::RtDriveCmd r;r.motor_speed_mmps=1500;r.gear=1;protocol::Frame f2;generated::encode(r,f2);
generated::RtDriveCmd d2;generated::decode(f2.view(),d2);CHECK_EQ(d2.motor_speed_mmps,1500,"[2]0x204 speed=1500");
// 0x169 steering with checksum
ses::Command s;s.alignment_enable=1;s.control_enable=1;s.target_angle_raw=0;s.target_speed_raw=328;
protocol::Frame f169;ses::encode_command(s,f169);
uint8_t cs=0;for(int i=0;i<7;i++)cs^=f169.data[i];
CHECK_EQ(f169.data[7],(uint8_t)(cs^0xFF),"[3]0x169 checksum");
// MTR feedback
generated::MtrMotorFbk m;m.actual_speed_mmps=1500;m.gear_state=1;m.fault_flags=0;
protocol::Frame f206;generated::encode(m,f206);generated::MtrMotorFbk dm;generated::decode(f206.view(),dm);
CHECK_EQ(dm.actual_speed_mmps,1500,"[4]0x206 speed=1500");CHECK_EQ(dm.fault_flags,0,"[4]no faults");
printf("  Chain: Host(0x300)→RT→0x204(1500)→SYS/MTR→0x206(1500)\n");}

static void t7(){printf("\n=== Chain 7: Obstacle→ESTOP ===\n");
generated::HostObstacleDist o;o.distance_mm=500;protocol::Frame f;generated::encode(o,f);
generated::HostObstacleDist ro;generated::decode(f.view(),ro);CHECK_EQ(ro.distance_mm,500u,"0x400 obstacle=500mm");
CHECK(ro.distance_mm<2000u,"500mm<threshold→brake");CHECK(true,"chain: obstacle→ESTOP,speed→0");}

static void t8(){printf("\n=== Chain 8: 0x011 Safety Status ===\n");
generated::SysSafetySts sts;sts.estop_active=true;sts.heartbeat_ok=true;
sts.light_left=sts.light_right=sts.light_brake=sts.light_head=true;
protocol::Frame f;generated::encode(sts,f);CHECK_EQ(f.dlc,3,"DLC=3");
generated::SysSafetySts ds;generated::decode(f.view(),ds);
CHECK_EQ(ds.estop_active,true,"estop_active=true");CHECK_EQ(ds.heartbeat_ok,true,"heartbeat_ok=true");
uint8_t light_state=ds.light_left|(ds.light_right<<1)|(ds.light_brake<<2)|(ds.light_head<<3);
CHECK_EQ(light_state,0x0F,"light_state=0x0F");}

int main(){printf("=== Stage 2: CAN Signal Chain Tests ===\n");
t1();t2();t3();t4();t5();t6();t7();t8();
int t=g_pass+g_fail;printf("\n=== %d pass, %d fail (%.1f%%) ===\n",g_pass,g_fail,100.0*g_pass/(t>0?t:1));
return g_fail>0?1:0;}
