#pragma once
#include <cstdint>
#include "protocol/compat/can.hpp"
namespace rt {
struct GatewayQueues {
    can::Frame* gw_tx_low=nullptr;
    can::Frame* gw_tx_high=nullptr;
    can::gen::HostDriveCmd* cmd=nullptr;
    int32_t* brake_req_kpa=nullptr;
    bool* estop_flag=nullptr;
    uint8_t* mode_from_sys=nullptr;
    uint16_t* steer_feedback_angle=nullptr; // from 0x201 SES_StrAngle raw value (CSV: Unsigned)
    uint8_t* steer_angle_status=nullptr;   // 0x201 byte0 bit0: 0=center finding, 1=found (gap C2)
};
inline can::gen::CodecStatus route_frame(const can::Frame& f, bool is_high_bus, GatewayQueues& q) {
    // ── Specific handlers for non-forwarding frames ──────────────────
    switch (f.id) {
    case can::kIdSafetyEstop:  // SAFETY_ESTOP — bidirectional forward handled by caller
        if (q.estop_flag) *q.estop_flag = true;
        return can::gen::CodecStatus::Ok;  // caller handles gateway copies
    case can::kIdHostDriveCmd:  // HOST_DRIVE_CMD — consumed by RT
        if (is_high_bus && q.cmd) {
            can::gen::HostDriveCmd decoded{};
            auto status = can::decode_frame(f, decoded);
            if (status != can::gen::CodecStatus::Ok) return status;
            *q.cmd = decoded;
        }
        return can::gen::CodecStatus::Ok;
    case can::kIdHostBrakeReq:  // HOST_BRAKE_REQ — consumed by RT
        if (is_high_bus && q.brake_req_kpa) {
            can::gen::HostBrakeReq decoded{};
            auto status = can::decode_frame(f, decoded);
            if (status != can::gen::CodecStatus::Ok) return status;
            *q.brake_req_kpa = decoded.brake_pressure_kpa;
        }
        return can::gen::CodecStatus::Ok;
    // HMI/Host controls for SYS are transparent High→Low gateway traffic.
    // Keep these explicit rather than relying only on metadata lookup: mode
    // selection is a prerequisite for RT motion authority, so a route-table
    // mismatch must never silently strand RT in MANUAL.
    case can::kIdHmiModeReq:
    case can::kIdHmiPwrReq:
    case can::kIdHostLightCmd:
        if (is_high_bus && q.gw_tx_low) *q.gw_tx_low = f;
        return can::gen::CodecStatus::Ok;
    case can::kIdSysModeCmd:  // SYS_MODE_CMD — consumed by RT
        if (!is_high_bus && q.mode_from_sys) {
            can::gen::SysModeCmd decoded{};
            auto status = can::decode_frame(f, decoded);
            if (status != can::gen::CodecStatus::Ok) return status;
            *q.mode_from_sys = decoded.mode;
        }
        return can::gen::CodecStatus::Ok;
    case can::kIdSbwStatus:  // SES_STATUS — consumed by RT (steering feedback)
        if (!is_high_bus) {
            can::custom::ses::Status value{};
            auto status = can::custom::ses::decode_status(f.view(), value);
            if (status != can::gen::CodecStatus::Ok) return status;
            if (q.steer_feedback_angle) *q.steer_feedback_angle = value.steering_angle_raw;
            if (q.steer_angle_status) *q.steer_angle_status = value.angle_aligned;
        }
        return can::gen::CodecStatus::Ok;
    case can::kIdBbwStatus:  // SEB_STATUS — L3 errors checked after checksum validation in dispatch
        return can::gen::CodecStatus::Ok;
    }
    // ── Transparent forwarding (all remaining IDs) ───────────────────
    // Uses the canonical protocol forwarding rules.
    // Low→High: 0x001,0x011,0x120,0x206,0x600.  High→Low: 0x001,0x302.
    // 0x001 is handled above; the rest fall through to here.
    if (can::is_forwarded_low_to_high(f.id) && !is_high_bus && q.gw_tx_high) {
        *q.gw_tx_high = f;
    }
    if (can::is_forwarded_high_to_low(f.id) && is_high_bus && q.gw_tx_low) {
        *q.gw_tx_low = f;
    }
    return can::gen::CodecStatus::Ok;
}
}
