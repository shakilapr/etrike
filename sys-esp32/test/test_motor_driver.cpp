#include <cstdio>
#include "motor_driver.h"
int main(){printf("Phase 12: Motor driver\n\n");sys::MotorDriver md;md.init();int f=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++f;}while(0)
md.throttle().tick(2048);
C("MANUAL pass-through");md.tick(can::Mode::Manual,nullptr);
if(md.dac().value()>1900&&md.dac().value()<2200)O;else B("man");
C("AUTO setpoint");can::RtDriveCmd sp{2000,0};
md.tick(can::Mode::Auto,&sp);
if(md.dac().value()>2600&&md.dac().value()<2900)O;else B("auto");
C("ESTOP zero");md.tick(can::Mode::Estop,nullptr);
if(md.dac().value()==0)O;else B("estop");
printf("\n  Result: %d failures\n",f);return f?1:0;}
