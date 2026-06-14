#pragma once
#include "can/can_protocol.h"
namespace rt {
struct GatewayQueues {
    can::Frame* gw_tx_low=nullptr;
    can::Frame* gw_tx_high=nullptr;
    can::HostDriveCmd* cmd=nullptr;
    int32_t* brake_req_kpa=nullptr;
    bool* estop_flag=nullptr;
    uint8_t* mode_from_sys=nullptr;
    int16_t* steer_feedback_angle=nullptr; // from 0x201 SES_StrAngle raw value
};
inline void route_frame(const can::Frame& f, bool is_high_bus, GatewayQueues& q) {
    switch (f.id) {
    case 0x001: if(q.estop_flag)*q.estop_flag=true;
        // Gateway both directions — caller handles
        break;
    case 0x011: case 0x120: case 0x600:
        if (!is_high_bus && q.gw_tx_high) *q.gw_tx_high = f; break;
    case 0x302:
        if (is_high_bus && q.gw_tx_low) *q.gw_tx_low = f; break;
    case 0x300:
        if (is_high_bus && q.cmd) { q.cmd->speed_mmps=f.i32_at(0); q.cmd->yaw_rate_mrad_s=f.i32_at(4); } break;
    case 0x301:
        if (is_high_bus && q.brake_req_kpa) *q.brake_req_kpa = f.i32_at(0); break;
    case 0x110:
        if (!is_high_bus && q.mode_from_sys) *q.mode_from_sys = f.u8_at(0); break;
    case 0x201:
        if (!is_high_bus && q.steer_feedback_angle)
            *q.steer_feedback_angle = int16_t(f.data[2]|(f.data[3]<<8)); break;
    }
}
}
