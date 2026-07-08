#pragma once

#include <algorithm>
#include <cstdint>
#include "can/can_protocol.h"
#include "config.h"

namespace rt {

inline can::VcuSebReq make_seb_takeover_req() {
    can::VcuSebReq seb{};
    seb.align_enable = 1;
    seb.control_mode = 0;    // Stroke mode
    seb.auto_brake   = 1;    // Emergency trigger
    seb.stroke_req   = 1140; // 27mm max stroke: (27+30)/0.05
    return seb;
}

inline can::VcuSebReq make_seb_auto_req(int32_t kpa) {
    can::VcuSebReq seb{};
    seb.align_enable = 1;  // Required by SEB protocol; otherwise SEB rejects frame.
    if (kpa > 0) {
        // Pressure Mode: kPa -> SEB raw (0.05 MPa/bit, 1 kPa = 0.02 raw)
        uint8_t pressure_raw = static_cast<uint8_t>(std::min(
            static_cast<int32_t>(kpa * 0.02f), int32_t(shared::kSebMaxPressureRaw)));
        seb.control_mode = 1;     // Pressure
        seb.pressure_req = pressure_raw;
        seb.stroke_req   = 600;   // 0mm baseline
        seb.auto_brake   = 1;     // automated braking
    } else {
        seb.control_mode = 0;     // Stroke
        seb.stroke_req   = 600;   // 0mm: (0+30)/0.05
    }
    return seb;
}

}  // namespace rt
