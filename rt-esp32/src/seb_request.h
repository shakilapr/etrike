#pragma once

#include <algorithm>
#include <cstdint>
#include "protocol/codecs/seb.hpp"
#include "config.h"

namespace rt {

inline etrike::protocol::codecs::seb::Command make_seb_takeover_req() {
    using etrike::protocol::codecs::seb::ControlMode;
    etrike::protocol::codecs::seb::Command seb{};
    seb.alignment_enable = true;
    seb.control_mode = ControlMode::Stroke;
    seb.auto_brake   = 1;    // Emergency trigger
    seb.stroke_request_raw = 1140; // 27mm max stroke: (27+30)/0.05
    return seb;
}

inline etrike::protocol::codecs::seb::Command make_seb_auto_req(int32_t kpa) {
    using etrike::protocol::codecs::seb::ControlMode;
    etrike::protocol::codecs::seb::Command seb{};
    seb.alignment_enable = true;  // Required by SEB protocol; otherwise SEB rejects frame.
    if (kpa > 0) {
        // Pressure Mode: kPa -> SEB raw (0.05 MPa/bit, 1 kPa = 0.02 raw)
        uint8_t pressure_raw = static_cast<uint8_t>(std::min(
            static_cast<int32_t>(kpa * 0.02f), int32_t(shared::kSebMaxPressureRaw)));
        seb.control_mode = ControlMode::Pressure;
        seb.pressure_request_raw = pressure_raw;
        seb.stroke_request_raw   = 600;   // 0mm baseline
        seb.auto_brake   = 1;     // automated braking
    } else {
        seb.control_mode = ControlMode::Stroke;
        seb.stroke_request_raw = 600;   // 0mm: (0+30)/0.05
    }
    return seb;
}

}  // namespace rt
