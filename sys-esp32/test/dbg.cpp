#include <cstdio>
#include "brake_control.h"
int main() {
    sys::BrakeControl bc; bc.init();
    can::VcuSebReq out;
    for(int i=0;i<25;++i) bc.tick(false,false,0,nullptr,out);
    uint8_t seb[8]={1};
    bc.tick(false,false,0,seb,out);
    bc.tick(false,false,5000,nullptr,out);
    printf("mode=%d press=%d (expect 2,100)\n", out.control_mode, out.pressure_req);
    float f = float(5000) * 0.02f;
    printf("float: %.1f int=%d\n", f, int32_t(f));
    return 0;
}
