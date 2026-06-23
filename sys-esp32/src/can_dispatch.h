#pragma once
// SYS CAN dispatch — routes incoming frames to queues/atomics.
#include "can/can_protocol.h"
#include "config.h"
namespace sys {
struct DispatchTargets {
    can::RtDriveCmd* setpoint = nullptr;  // from 0x204
    int32_t* brake_kpa = nullptr;         // from 0x205
    can::MtrMotorFbk* motor_fbk = nullptr; // from 0x206
    uint8_t* light_bits = nullptr;        // from 0x302
    bool* estop_flag = nullptr;           // from 0x001
    uint8_t* seb_status_raw = nullptr;    // from 0x721 (8 bytes)
    uint8_t* rt_hb_ctr = nullptr;         // from 0x7FD
    bool* rt_hb_received = nullptr;
    // New in architecture §8.3:
    int16_t* seb_motor_current = nullptr; // from 0x6FB
    int16_t* seb_ecu_temp = nullptr;      // from 0x6FB
    bool* seb_l3_fault = nullptr;         // from 0x731 (any L3 bit set)
    bool* seb_version_logged = nullptr;   // from 0x741 (first receipt flag)
};
inline void dispatch_frame(const can::Frame& f, DispatchTargets& t) {
    switch(f.id) {
    case 0x204: if(t.setpoint) *t.setpoint = can::RtDriveCmd::from_frame(f); break;
    case 0x205: if(t.brake_kpa) *t.brake_kpa = can::RtBrakeCmd::from_frame(f).brake_pressure_kpa; break;
    case 0x302: if(t.light_bits) *t.light_bits = f.u8_at(0); break;
    case 0x001: if(t.estop_flag) *t.estop_flag = true; break;
    case 0x721: if(t.seb_status_raw) for(int i=0;i<8&&i<f.dlc;++i) t.seb_status_raw[i]=f.data[i]; break;
    case 0x7FD: if(t.rt_hb_ctr) *t.rt_hb_ctr=f.u8_at(0); if(t.rt_hb_received)*t.rt_hb_received=true; break;
    case 0x206: if(t.motor_fbk) *t.motor_fbk = can::MtrMotorFbk::from_frame(f); break;
    case 0x6FB:
        if(t.seb_motor_current) *t.seb_motor_current = int16_t(f.data[1] | (f.data[2] << 8));
        if(t.seb_ecu_temp) {
            uint16_t raw = uint16_t(f.data[3] | (f.data[4] << 8));
            *t.seb_ecu_temp = int16_t(raw * 0.5f - 40.0f);
        }
        break;
    case 0x731:
        if(t.seb_l3_fault) {
            static const int kL3Bits[] = {2,3,4,5,6,7,8,9,10,11,13,17,18,20,21,22};
            for (int i = 0; i < 16; ++i) {
                int byte_idx = kL3Bits[i] / 8;
                if (byte_idx < f.dlc && (f.data[byte_idx] & (1 << (kL3Bits[i] % 8)))) {
                    *t.seb_l3_fault = true;
                    break;
                }
            }
        }
        break;
    case 0x741:
        if(t.seb_version_logged && !*t.seb_version_logged) {
            // First receipt — caller logs SW/HW
            *t.seb_version_logged = true;
        }
        break;
    }
}
}
