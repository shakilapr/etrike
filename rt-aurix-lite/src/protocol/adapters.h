#pragma once
// Protocol adapters — CAN bytes <-> typed domain I/O.
//
// This is the ONLY layer that knows CAN IDs and wire encoding. It uses the
// generated subset codecs (etrike_protocol.hpp: etrike::protocol::generated)
// and the shared vendor SES/SEB codecs (protocol/codecs/{ses,seb}.hpp).
// All codecs are read-only dependencies.

#include <cstdint>

#include "core/types.h"
#include "config/control_config.h"  // kSbwAngleOffset

#include "protocol/core/frame.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "etrike_protocol.hpp"  // subset generated codecs (rta/host/mtr/hmi)

namespace rta {

using Frame = etrike::protocol::Frame;
using FrameView = etrike::protocol::FrameView;
using CodecStatus = etrike::protocol::CodecStatus;
namespace gen = etrike::protocol::generated;
namespace ses = etrike::protocol::codecs::ses;
namespace seb = etrike::protocol::codecs::seb;

// ── Decode: CAN frame -> typed domain input ─────────────────────────

// Host drive command (0x300) -> DriveDemand.
inline bool decode_host_drive(FrameView frame, DriveDemand& out) {
    gen::HostDriveCmd cmd{};
    if (!etrike::protocol::succeeded(gen::decode(frame, cmd))) return false;
    out.speed_mmps = cmd.speed_mmps;
    out.yaw_rate_mrad_s = cmd.yaw_rate_mrad_s;
    out.gear = cmd.gear;
    return true;
}

// Host brake request (0x301) -> brake_kpa (returned via out.speed? no —
// use a dedicated output param).
// We expose a simple accessor instead: brake pressure is carried as an
// int32; map through a small struct.

struct DecodedBrakeReq {
    std::int32_t brake_kpa = 0;
    bool         valid     = false;
};

inline DecodedBrakeReq decode_host_brake(FrameView frame) {
    gen::HostBrakeReq req{};
    DecodedBrakeReq out;
    if (etrike::protocol::succeeded(gen::decode(frame, req))) {
        out.brake_kpa = req.brake_pressure_kpa;
        out.valid = true;
    }
    return out;
}

// HMI mode request (0x111) -> ModeRequest (typed).
inline bool decode_hmi_mode(FrameView frame, ModeRequest& out) {
    gen::HmiModeReq req{};
    if (!etrike::protocol::succeeded(gen::decode(frame, req))) return false;
    out.valid = true;
    out.mode = (req.req_mode == 0) ? Mode::Manual : Mode::Auto;
    return true;
}

// MTR motor feedback (0x206) -> MotorFeedback.
inline bool decode_mtr_motor(FrameView frame, MotorFeedback& out) {
    gen::MtrMotorFbk fbk{};
    if (!etrike::protocol::succeeded(gen::decode(frame, fbk))) return false;
    out.actual_speed_mmps = fbk.actual_speed_mmps;
    out.gear_state = fbk.gear_state;
    out.fault_flags = fbk.fault_flags;
    return true;
}

// SES status (0x201) -> SteeringFeedback.
inline bool decode_ses_status(FrameView frame, SteeringFeedback& out) {
    ses::Status st{};
    if (!etrike::protocol::succeeded(ses::decode_status(frame, st))) return false;
    out.valid = true;
    out.angle_aligned = st.angle_aligned;
    out.angle_0_1deg = static_cast<std::int16_t>(st.steering_angle_raw);
    out.error_status = st.error_status;
    out.rolling_counter = st.rolling_counter;
    return true;
}

// SEB status (0x721) -> BrakeFeedback.
inline bool decode_seb_status(FrameView frame, BrakeFeedback& out) {
    seb::Status st{};
    if (!etrike::protocol::succeeded(seb::decode_status(frame, st))) return false;
    out.valid = true;
    out.alignment = st.alignment_status;
    out.stroke_raw = st.stroke_value_raw;
    out.error_status = st.error_status;
    out.rolling_counter = st.rolling_counter;
    return true;
}

// ── Encode: typed domain output -> CAN frame ────────────────────────

// DriveCommand (0x204) -> Frame.
inline bool encode_drive_cmd(const DriveCommand& in, Frame& out) {
    gen::RtaDriveCmd cmd{};
    cmd.motor_speed_mmps = in.motor_speed_mmps;
    cmd.gear = in.gear;
    return etrike::protocol::succeeded(gen::encode(cmd, out));
}

// SteeringCommand (0x169) via SES codec (vendor).
inline bool encode_steer_cmd(const SteeringCommand& in, Frame& out) {
    ses::Command cmd{};
    cmd.alignment_enable = true;
    cmd.control_enable = true;
    cmd.target_angle_raw = in.angle_0_1deg + kSbwAngleOffset;  // 0.1° -> vendor offset
    cmd.target_speed_raw = in.speed_raw;
    cmd.rolling_counter = in.rolling_counter;
    cmd.vehicle_speed_raw = in.vehicle_speed_raw;
    return etrike::protocol::succeeded(ses::encode_command(cmd, out));
}

// BrakeCommand (0x7B9) via SEB codec (vendor).
inline bool encode_brake_cmd(const BrakeCommand& in, Frame& out) {
    seb::Command cmd{};
    cmd.alignment_enable = true;
    cmd.control_enable = true;
    cmd.control_mode = in.stroke_mode ? seb::ControlMode::Stroke
                                      : seb::ControlMode::Pressure;
    cmd.stroke_request_raw = in.stroke_raw;
    cmd.pressure_request_raw = in.pressure_raw;
    cmd.auto_brake = in.auto_brake;
    cmd.rolling_counter = in.rolling_counter;
    return etrike::protocol::succeeded(seb::encode_command(cmd, out));
}

// RTA heartbeat (0x7FD) -> Frame.
inline bool encode_heartbeat(std::uint8_t alive_ctr, std::uint8_t health_flags, Frame& out) {
    gen::RtaHeartbeat hb{};
    hb.alive_ctr = alive_ctr;
    hb.health_flags = health_flags;
    return etrike::protocol::succeeded(gen::encode(hb, out));
}

}  // namespace rta
