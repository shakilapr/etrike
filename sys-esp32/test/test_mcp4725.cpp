#include <cstdio>
#include "mcp4725_dac.h"
int main(){printf("Phase 11: MCP4725 DAC\n\n");sys::Mcp4725Dac d;d.init();int f=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++f;}while(0)
C("init=0");if(d.value()==0)O;else B("init");
d.write(2048);C("write 2048");if(d.value()==2048)O;else B("w");
d.write(5000);C("clamp >4095");if(d.value()==4095)O;else B("clamp");
d.set_speed_mmps(1500);C("speed 1500->2047");if(d.value()==2047)O;else B("s1");
d.set_speed_mmps(3000);C("speed 3000->4095");if(d.value()==4095)O;else B("s2");
d.set_speed_mmps(0);C("speed 0->0");if(d.value()==0)O;else B("s3");
d.set_speed_mmps(-1500);C("speed -1500->2047");if(d.value()==2047)O;else B("neg");
printf("\n  Result: %d failures\n",f);return f?1:0;}
