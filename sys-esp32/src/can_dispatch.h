#pragma once
// SYS CAN dispatch — routes incoming frames to queues/atomics.
#include "can/can_protocol.h"
#include "config.h"
namespace sys {
struct DispatchTargets {
    can::RtDriveCmd* setpoint = nullptr;  // from 0x204
    int32_t* brake_kpa = nullptr;         // from 0x205
    uint8_t* light_bits = nullptr;        // from 0x302
    bool* estop_flag = nullptr;           // from 0x001
    uint8_t* seb_status_raw = nullptr;    // from 0x721 (8 bytes)
    uint8_t* rt_hb_ctr = nullptr;         // from 0x7FD
    bool* rt_hb_received = nullptr;
};
inline void dispatch_frame(const can::Frame& f, DispatchTargets& t) {
    switch(f.id) {
    case 0x204: if(t.setpoint) *t.setpoint = can::RtDriveCmd::from_frame(f); break;
    case 0x205: if(t.brake_kpa) *t.brake_kpa = can::RtBrakeCmd::from_frame(f).brake_pressure_kpa; break;
    case 0x302: if(t.light_bits) *t.light_bits = f.u8_at(0); break;
    case 0x001: if(t.estop_flag) *t.estop_flag = true; break;
    case 0x721: if(t.seb_status_raw) for(int i=0;i<8&&i<f.dlc;++i) t.seb_status_raw[i]=f.data[i]; break;
    case 0x7FD: if(t.rt_hb_ctr) *t.rt_hb_ctr=f.u8_at(0); if(t.rt_hb_received)*t.rt_hb_received=true; break;
    }
}
}
