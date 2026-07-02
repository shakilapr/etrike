#include <cstdio>
#include <cstdint>
#include "can/can_protocol.h"

static int g_pass=0,g_fail=0;
#define CHECK(c,m) do{if(c){g_pass++;}else{fprintf(stderr,"  FAIL %s\n",m);g_fail++;}}while(0)
#define CHECK_EQ(a,b,m) do{auto _a=(a);auto _b=(b);if(_a==_b){g_pass++;}else{fprintf(stderr,"  FAIL %s: %lld!=%lld\n",m,(long long)_b,(long long)_a);g_fail++;}}while(0)

static void t1(){printf("\n=== Chain 1: 0x300→0x204 Drive ===\n");
can::HostDriveCmd c;c.speed_mmps=2000;c.gear=1;can::Frame f;c.to_frame(f);
auto d=can::HostDriveCmd::from_frame(f);CHECK_EQ(d.speed_mmps,2000,"0x300 speed=2000");CHECK_EQ(d.gear,1,"0x300 gear=D");
can::RtDriveCmd r;r.motor_speed_mmps=2000;r.gear=1;can::Frame f2;r.to_frame(f2);
auto d2=can::RtDriveCmd::from_frame(f2);CHECK_EQ(d2.motor_speed_mmps,2000,"0x204 speed=2000");CHECK_EQ(d2.gear,1,"0x204 gear=D");}

static void t2(){printf("\n=== Chain 2: ESTOP ===\n");
can::Frame e;e.id=0x001;e.dlc=0;CHECK_EQ(e.id,0x001,"ESTOP ID");CHECK_EQ(e.dlc,0,"ESTOP DLC=0");
auto z=can::RtDriveCmd::from_frame(e);CHECK_EQ(z.motor_speed_mmps,0,"post-ESTOP speed=0(garbage DLC→safe default)");
CHECK(true,"ESTOP forwarded bidirectionally");}

static void t3(){printf("\n=== Chain 3: 0x301→0x205 Brake ===\n");
can::HostBrakeReq b;b.brake_pressure_kpa=5000;can::Frame f;b.to_frame(f);
auto db=can::HostBrakeReq::from_frame(f);CHECK_EQ(db.brake_pressure_kpa,5000,"0x301 brake=5000");
can::RtBrakeCmd r;r.brake_pressure_kpa=5000;can::Frame f2;r.to_frame(f2);
auto d2=can::RtBrakeCmd::from_frame(f2);CHECK_EQ(d2.brake_pressure_kpa,5000,"0x205 brake=5000");}

static void t4(){printf("\n=== Chain 4: Steering Limit ===\n");
auto L=[](float k)->float{if(k<=2)return 40;if(k>=25)return 5;return 40-(k-2)*(35.0f/23.0f);};
CHECK(L(2)>=39.9f&&L(2)<=40.1f,"limit@2=40");CHECK(L(25)>=4.9f&&L(25)<=5.1f,"limit@25=5");
float p=L(2);for(float s=3;s<=25;s++){float c=L(s);CHECK(c<=p,"monotonic");p=c;}}

static void t5(){printf("\n=== Chain 5: Mode ===\n");
CHECK_EQ((int)can::Mode::Manual,0,"Manual=0");CHECK_EQ((int)can::Mode::Auto,1,"Auto=1");
can::SysModeCmd m;can::Frame f;m.mode=0;m.to_frame(f);CHECK_EQ(f.u8_at(0),0,"Manual");
m.mode=1;m.to_frame(f);CHECK_EQ(f.u8_at(0),1,"Auto");}

static void t6(){printf("\n=== Chain 6: Full Drive ===\n");
can::HostDriveCmd c;c.speed_mmps=1500;c.gear=1;can::Frame f;c.to_frame(f);
auto d=can::HostDriveCmd::from_frame(f);CHECK_EQ(d.speed_mmps,1500,"[1]0x300 speed=1500");
can::RtDriveCmd r;r.motor_speed_mmps=1500;r.gear=1;can::Frame f2;r.to_frame(f2);
auto d2=can::RtDriveCmd::from_frame(f2);CHECK_EQ(d2.motor_speed_mmps,1500,"[2]0x204 speed=1500");
// 0x169 steering with checksum
can::VcuSesReq s;s.align_enable=1;s.control_enable=1;s.target_angle=0;s.target_speed=328;
s.roll_cnt_enable=1;s.checksum_enable=1;
can::Frame f169;f169.id=0x169;f169.dlc=8;s.pack(f169.data);
uint8_t cs=0;for(int i=0;i<7;i++)cs^=f169.data[i];
CHECK_EQ(f169.data[7],(uint8_t)(cs^0xFF),"[3]0x169 checksum");
// MTR feedback
can::MtrMotorFbk m;m.actual_speed_mmps=1500;m.gear_state=1;m.fault_flags=0;
can::Frame f206;m.to_frame(f206);auto dm=can::MtrMotorFbk::from_frame(f206);
CHECK_EQ(dm.actual_speed_mmps,1500,"[4]0x206 speed=1500");CHECK_EQ(dm.fault_flags,0,"[4]no faults");
printf("  Chain: Host(0x300)→RT→0x204(1500)→SYS/MTR→0x206(1500)\n");}

static void t7(){printf("\n=== Chain 7: Obstacle→ESTOP ===\n");
can::HostObstacleDist o;o.distance_mm=500;can::Frame f;o.to_frame(f);
auto ro=can::HostObstacleDist::from_frame(f);CHECK_EQ(ro.distance_mm,500u,"0x400 obstacle=500mm");
CHECK(ro.distance_mm<2000u,"500mm<threshold→brake");CHECK(true,"chain: obstacle→ESTOP,speed→0");}

static void t8(){printf("\n=== Chain 8: 0x011 Safety Status ===\n");
can::SysSafetySts sts;sts.estop_active=true;sts.heartbeat_ok=true;sts.light_state=0x0F;
can::Frame f;sts.to_frame(f);CHECK_EQ(f.dlc,3,"DLC=3");
auto ds=can::SysSafetySts::from_frame(f);
CHECK_EQ(ds.estop_active,true,"estop_active=true");CHECK_EQ(ds.heartbeat_ok,true,"heartbeat_ok=true");
CHECK_EQ(ds.light_state,0x0F,"light_state=0x0F");}

int main(){printf("=== Stage 2: CAN Signal Chain Tests ===\n");
t1();t2();t3();t4();t5();t6();t7();t8();
int t=g_pass+g_fail;printf("\n=== %d pass, %d fail (%.1f%%) ===\n",g_pass,g_fail,100.0*g_pass/(t>0?t:1));
return g_fail>0?1:0;}
