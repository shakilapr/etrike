// g++ -std=c++17 -include test_compat.h -I. -I../src -I../../shared test_qa_bugs.cpp ../src/physics_model.cpp -o test_qa_bugs && ./test_qa_bugs
#include "test_compat.h"
#include <cstdio>
#include <cmath>
#include "config.h"
#include "physics_model.h"
#include "can/can_protocol.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

int main() {
    printf("\n=== QA Bugs Tests ===\n\n");
    using namespace rt;
    PhysicsModel phys;

    printf("-- Bug 4.4: Reverse Steer Inversion --\n");
    rt::DriveCmd fwd_cmd { 1000, 500 };
    ResolvedSetpoint fwd_out;
    phys.resolve(fwd_cmd, fwd_out);
    
    rt::DriveCmd rev_cmd { -1000, 500 }; 
    ResolvedSetpoint rev_out;
    phys.resolve(rev_cmd, rev_out);

    printf("Fwd angle: %d, Rev angle: %d\n", fwd_out.steer_angle_mdeg, rev_out.steer_angle_mdeg);
    
    // Correct physics: w = v / L * tan(delta) -> delta = atan(L * w / v)
    // If v < 0 and w > 0, delta must be < 0. 
    // The bug uses abs(v), causing delta > 0.
    if (rev_out.steer_angle_mdeg < 0) {
        pass++;
    } else {
        fail++;
        fprintf(stderr,"FAIL Reverse steer bug: expected <0, got %d\n", rev_out.steer_angle_mdeg);
    }

    printf("-- Bug 4.5: Spontaneous Forward Lurch --\n");
    rt::DriveCmd zero_cmd { 0, 500 };
    ResolvedSetpoint zero_out;
    phys.resolve(zero_cmd, zero_out);
    printf("Zero speed cmd -> motor_speed: %d mm/s\n", zero_out.motor_speed_mmps);
    
    // Bug sets speed to non-zero when v=0 and w!=0.
    if (zero_out.motor_speed_mmps == 0) {
        pass++;
    } else {
        fail++;
        fprintf(stderr,"FAIL Lurch bug: expected 0 speed, got %d\n", zero_out.motor_speed_mmps);
    }

    printf("-- Bug 4.10: SEB Alignment Bit Uninitialized --\n");
    // Extracting make_seb_auto_req from main.cpp
    auto make_seb_auto_req = [](int32_t kpa) {
        can::VcuSebReq seb{};
        if (kpa > 0) {
            uint8_t pressure_raw = static_cast<uint8_t>(std::min(
                static_cast<int32_t>(kpa * 0.02f), int32_t(shared::kSebMaxPressureRaw)));
            seb.control_mode = 1;
            seb.pressure_req = pressure_raw;
            seb.stroke_req   = 600;
            seb.auto_brake   = 1;
        } else {
            seb.control_mode = 0;
            seb.stroke_req   = 600;
        }
        return seb;
    };
    
    can::VcuSebReq auto_req = make_seb_auto_req(2000);
    printf("SEB auto req align_enable: %d\n", auto_req.align_enable);
    if (auto_req.align_enable == 1) {
        pass++;
    } else {
        fail++;
        fprintf(stderr,"FAIL SEB Align bug: expected align_enable=1, got 0\n");
    }

    printf("\n=== %d pass, %d fail ===\n",pass,fail);
    return fail?1:0;
}
