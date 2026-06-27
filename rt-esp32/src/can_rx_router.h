#pragma once
#include <cstdint>
#include "can/can_protocol.h"
namespace rt {
struct GatewayQueues {
    can::Frame* gw_tx_low=nullptr;
    can::Frame* gw_tx_high=nullptr;
    can::HostDriveCmd* cmd=nullptr;
    int32_t* brake_req_kpa=nullptr;
    bool* estop_flag=nullptr;
    uint8_t* mode_from_sys=nullptr;
    uint16_t* steer_feedback_angle=nullptr; // from 0x201 SES_StrAngle raw value (CSV: Unsigned)
    uint8_t* steer_angle_status=nullptr;   // 0x201 byte0 bit0: 0=center finding, 1=found (gap C2)
};
inline void route_frame(const can::Frame& f, bool is_high_bus, GatewayQueues& q) {
    // ── Specific handlers for non-forwarding frames ──────────────────
    switch (f.id) {
    case can::kIdSafetyEstop:  // SAFETY_ESTOP — bidirectional forward handled by caller
        if (q.estop_flag) *q.estop_flag = true;
        return;  // caller handles gw_tx_high + gw_tx_low push
    case can::kIdHostDriveCmd:  // HOST_DRIVE_CMD — consumed by RT
        if (is_high_bus && q.cmd) {
            *q.cmd = can::HostDriveCmd::from_frame(f);
        }
        return;
    case can::kIdHostBrakeReq:  // HOST_BRAKE_REQ — consumed by RT
        if (is_high_bus && q.brake_req_kpa) { *q.brake_req_kpa = f.i32_at(0); }
        return;
    case can::kIdSysModeCmd:  // SYS_MODE_CMD — consumed by RT
        if (!is_high_bus && q.mode_from_sys) { *q.mode_from_sys = f.u8_at(0); }
        return;
    case can::kIdSyntreeEpsStatus:  // SES_STATUS — consumed by RT (steering feedback)
        if (!is_high_bus) {
            if (q.steer_feedback_angle) {
                *q.steer_feedback_angle = uint16_t(f.data[2] | (f.data[3] << 8));
            }
            if (q.steer_angle_status) {
                *q.steer_angle_status = f.data[0] & 1;  // byte0 bit0: angle_status
            }
        }
        return;
    case can::kIdSyntreeSebStatus:  // SEB_STATUS — monitor L3 errors
        if (!is_high_bus && q.estop_flag) {
            uint8_t err = (f.data[0] >> 6) & 0x03;
            if (err == 3) *q.estop_flag = true;
        }
        return;
    }
    // ── Transparent forwarding (all remaining IDs) ───────────────────
    // Uses the shared forwarding rules from can_protocol.h.
    // Low→High: 0x001,0x011,0x120,0x206,0x600.  High→Low: 0x001,0x302.
    // 0x001 is handled above; the rest fall through to here.
    if (can::is_forwarded_low_to_high(f.id) && !is_high_bus && q.gw_tx_high) {
        *q.gw_tx_high = f;
    }
    if (can::is_forwarded_high_to_low(f.id) && is_high_bus && q.gw_tx_low) {
        *q.gw_tx_low = f;
    }
}
}
