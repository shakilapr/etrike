#include <cstdio>
#include "throttle_input.h"
int main(){printf("Phase 10: Throttle ADC\n\n");
sys::ThrottleInput ti;ti.init();int f=0;
#define C(d) printf("  %-50s ",d)
#define O printf("PASS\n")
#define B(m) do{printf("FAIL: %s\n",m);++f;}while(0)
C("raw=0->0");if(ti.tick(0)==0)O;else B("0");
C("raw=150<DZ->0");if(ti.tick(150)==0)O;else B("dz");
C("raw=199<DZ->0");if(ti.tick(199)==0)O;else B("dz2");
C("raw=200==DZ->146");if(ti.tick(200)==146)O;else B("edge");
C("raw=2048->1500");if(ti.tick(2048)==1500)O;else B("mid");
C("raw=4095->3000");if(ti.tick(4095)==3000)O;else B("max");
C("speed_mmps() ok");if(ti.read_mmps()==3000)O;else B("last");
printf("\n  Result: %d failures\n",f);return f?1:0;}
