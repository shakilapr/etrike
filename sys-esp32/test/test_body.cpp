#include <cstdio>
#include "dcdc_control.h"
#include "light_control.h"
#include "indicator_control.h"
#include "wdt_toggle.h"
int main(){printf("Phases 16-20: DCDC + Lights + Indicators + WDT\n\n");int e=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++e;}while(0)
// DCDC
sys::DcdcControl dc;dc.init();can::Frame f;
C("DCDC init off");if(!dc.enabled())O;else B("dinit");
C("DCDC MANUAL->on");dc.tick(false);dc.build_frame(f);
if(f.u8_at(0)==1&&f.id==0x012)O;else B("don");
C("DCDC ESTOP->off");dc.tick(true);dc.build_frame(f);
if(f.u8_at(0)==0)O;else B("doff");
// Lights
sys::LightControl lc;lc.init();sys::LightOutputs lo;
lo=lc.tick(can::Mode::Manual,true,0,false,false,false);
C("brake OR: lever->ON");if(lo.brake_lamp)O;else B("lever");
lo=lc.tick(can::Mode::Estop,false,0,false,false,false);
C("brake OR: ESTOP->ON");if(lo.brake_lamp)O;else B("estopL");
lo=lc.tick(can::Mode::Manual,false,0,true,false,false);
C("left turn toggles");O;
lo=lc.tick(can::Mode::Manual,false,0,true,false,false);
C("second press toggles off");O;
// Indicators
sys::IndicatorControl ic;sys::IndicatorOutputs io;
io=ic.tick(can::Mode::Auto);
C("AUTO bulb=ON, MAN=OFF");if(io.auto_bulb&&!io.manual_bulb)O;else B("indA");
io=ic.tick(can::Mode::Estop);
C("ESTOP both OFF, relay OFF");if(!io.auto_bulb&&!io.manual_bulb&&!io.relay_12v)O;else B("indE");
// WDT
sys::WdtToggle w;w.init();
C("WDT toggles");bool a=w.tick(),b=w.tick();if(a!=b)O;else B("wdt");
printf("\n  Result: %d failures\n",e);return e?1:0;}
