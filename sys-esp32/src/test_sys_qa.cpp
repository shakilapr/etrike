// g++ -std=c++17 -include ../../rt-esp32/test/test_compat.h -I. -I../../shared test_sys_qa.cpp -o test_sys_qa && ./test_sys_qa
#include "../../rt-esp32/test/test_compat.h"
#include <cstdio>
#include <cstdint>
#include "can/can_protocol.h"
#include "config.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

int main() {
    printf("\n=== SYS QA Bugs Tests ===\n\n");

    printf("-- Bug 5.1 & 5.2: Brake Following Error & 0x721 Parsing Corruption --\n");
    // Simulate SYS being in Auto mode, receiving brake_kpa > 0.
    // In build_command:
    uint16_t g_cmd_stroke_raw = 0;
    int32_t brake_kpa = 2000;
    
    // SYS builds command:
    can::VcuSebReq out{};
    if (brake_kpa > 0) {
        out.control_mode  = 1;  
        // kStrokeRawZero calculation from brake_control.h
        uint16_t kStrokeRawZero = static_cast<uint16_t>((0.0f - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        out.stroke_req    = kStrokeRawZero; // 600
        g_cmd_stroke_raw  = out.stroke_req;
    }
    
    // Simulate SEB 0x721 Status response in Pressure mode. 
    // Actual stroke is 10mm (800 raw), and pressure is 40 raw (2 MPa).
    // SEB byte 2 = stroke low (800 & 0xFF = 0x20)
    // SEB byte 3 = pressure (40 = 0x28)
    uint8_t fr_data[8] = {0x01, 0x00, 0x20, 0x28, 0x00, 0x00, 0x00, 0x00};
    
    // SYS parsing logic in task_dispatch:
    uint16_t actual_raw = uint16_t(fr_data[2] | (fr_data[3] << 8));
    
    // SYS following error logic:
    uint16_t cmd = g_cmd_stroke_raw;
    uint16_t diff = (cmd > actual_raw) ? (cmd - actual_raw) : (actual_raw - cmd);
    
    printf("Cmd Stroke: %d, Actual Raw (Corrupted): %d, Diff: %d\n", cmd, actual_raw, diff);
    if (diff > sys::kBrakeFollowingErrRaw) {
        pass++; // It triggers a false fault
    } else {
        fail++;
        fprintf(stderr, "FAIL: Expected false following error fault to trigger, but it didn't.\n");
    }

    printf("-- Bug 5.3: ESTOP RX Rate-Limiting Bypass --\n");
    // Simulate 3 ESTOP frames arriving in same window
    int estop_rx_count = 0;
    int accepted_estops = 0;
    int dropped_estops = 0;
    
    for (int i=0; i<3; i++) {
        if (++estop_rx_count <= sys::kEstopRateLimitMax) {
            accepted_estops++;
        } else {
            dropped_estops++;
        }
    }
    
    // Check if the 3rd frame is dropped (bypassing force_estop)
    if (dropped_estops > 0) {
        pass++;
    } else {
        fail++;
        fprintf(stderr, "FAIL: Expected ESTOP frame to be dropped.\n");
    }

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail?1:0;
}
