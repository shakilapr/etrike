// Phase R6: RT CAN gateway — forwarding categories per architecture §2.3
#include <cstdio>
#include "can_rx_router.h"
#include "can/can_protocol.h"

static int fails = 0;
#define C(d) printf("  %-55s ", d)
#define O printf("PASS\n")
#define B(m) do { printf("FAIL: %s\n", m); ++fails; } while(0)
#define H(s) printf("\n== %s ==\n", s)

int main() {
    printf("Phase R6: CAN Gateway\n\n");

    H("Category 1: Low->High forward");
    can::Frame f, gw;
    f.id=0x011; f.dlc=2; f.put_u8(0,0); f.put_u8(1,1);
    rt::GatewayQueues q{}; q.gw_tx_high=&gw;
    rt::route_frame(f,false,q);
    C("0x011 on low -> forwarded to high"); if(gw.id==0x011)O; else B("011");

    f.id=0x120; gw={}; rt::route_frame(f,false,q);
    C("0x120 on low -> forwarded to high"); if(gw.id==0x120)O; else B("120");

    f.id=0x600; gw={}; rt::route_frame(f,false,q);
    C("0x600 on low -> forwarded to high"); if(gw.id==0x600)O; else B("600");

    H("Category 1: High->Low forward");
    f.id=0x302; can::Frame gl; q.gw_tx_low=&gl;
    rt::route_frame(f,true,q);
    C("0x302 on high -> forwarded to low"); if(gl.id==0x302)O; else B("302");

    f.id=0x300; gl={}; rt::route_frame(f,true,q);
    C("0x300 on high -> NOT forwarded (consumed)"); if(gl.id==0)O; else B("300fwd");

    H("Category 1: ESTOP bidirectional");
    bool estop=false; q.estop_flag=&estop;
    f.id=0x001; rt::route_frame(f,false,q);
    C("0x001 on low -> estop_flag=true"); if(estop)O; else B("001lo");
    estop=false; rt::route_frame(f,true,q);
    C("0x001 on high -> estop_flag=true"); if(estop)O; else B("001hi");

    H("Category 2: Consume 0x300, 0x301");
    can::HostDriveCmd cmd; q.cmd=&cmd;
    f.id=0x300; f.dlc=8; f.put_i32(0,2000);
    f.put_u8(4,0); f.put_u8(5,1); f.put_u8(6,0x90); f.put_u8(7,1);
    rt::route_frame(f,true,q);
    C("0x300 on high -> parsed"); if(cmd.speed_mmps==2000&&cmd.yaw_rate_mrad_s==400)O; else B("300p");

    int32_t bk=0; q.brake_req_kpa=&bk;
    f.id=0x301; f.dlc=4; f.put_i32(0,8000);
    rt::route_frame(f,true,q);
    C("0x301 on high -> brake_kpa=8000"); if(bk==8000)O; else B("301");

    bk=0; rt::route_frame(f,false,q);
    C("0x301 on low -> ignored"); if(bk==0)O; else B("301lo");

    H("Category 3: Bus-local");
    uint8_t mode=0xFF; q.mode_from_sys=&mode;
    f.id=0x110; f.dlc=1; f.put_u8(0,1);
    rt::route_frame(f,false,q);
    C("0x110 on low -> mode=Auto"); if(mode==1)O; else B("110");

    gw={}; rt::route_frame(f,false,q);
    C("0x200 on low -> NOT forwarded"); if(gw.id==0)O; else B("200fwd");

    int16_t ang=0; q.steer_feedback_angle=&ang;
    f.id=0x201; f.data[2]=0xC7; f.data[3]=0x01;
    rt::route_frame(f,false,q);
    C("0x201 on low -> angle=455"); if(ang==455)O; else B("201");

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
