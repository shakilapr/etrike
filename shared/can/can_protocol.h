#pragma once
// CAN protocol definitions — three-node distributed architecture.
// Low-level CAN (500 kbit/s): RT, SYS, EPS-C, SEB, DC-DC converter.
// High-level CAN (500 kbit/s): RT, Jetson.
// RT bridges selected IDs between buses (§2.3 architecture.md).

#include <cstdint>
#include "endian.h"

namespace can {

// ╔══════════════════════════════════════════════════════════════════════╗
// ║  OUR CAN IDs — we control these, can reassign as needed            ║
// ╚══════════════════════════════════════════════════════════════════════╝

// ── Low-level bus (our IDs) ───────────────────────────────────────

constexpr uint32_t kIdSafetyEstop       = 0x001;  // any→all, bridged to high
constexpr uint32_t kIdSysSafetySts      = 0x011;  // SYS→RT (→Jetson), 5 Hz
constexpr uint32_t kIdSysDcdcCmd        = 0x012;  // SYS→DC-DC converter, on change
constexpr uint32_t kIdSysModeCmd        = 0x110;  // SYS→RT, on change
constexpr uint32_t kIdSysThrottleSts    = 0x120;  // SYS→RT (→Jetson), 100 Hz
constexpr uint32_t kIdRtDriveCmd        = 0x204;  // RT→SYS motor speed+gear, 100 Hz
constexpr uint32_t kIdRtBrakeCmd        = 0x205;  // RT→SYS brake pressure kPa, 50 Hz
constexpr uint32_t kIdMtrMotorFbk       = 0x206;  // MTR(STM32)->SYS+RT motor feedback, 50 Hz
constexpr uint32_t kIdHostLightCmd      = 0x302;  // RT(fwd)→SYS light bitfield, on change
constexpr uint32_t kIdSysDiagRpt        = 0x600;  // SYS→RT (→Jetson), 1 Hz
constexpr uint32_t kIdRtHeartbeatLow    = 0x7FD;  // RT→SYS alive counter, 2 Hz
constexpr uint32_t kIdSysHeartbeat      = 0x7FE;  // SYS→RT alive counter, 2 Hz

// ── High-level bus (our IDs) ──────────────────────────────────────

constexpr uint32_t kIdRtStateRpt        = 0x210;  // RT→Jetson, 10 Hz
constexpr uint32_t kIdRtPidRpt          = 0x220;  // RT→Jetson, reserved (future PID)
constexpr uint32_t kIdHostDriveCmd      = 0x300;  // Jetson→RT, ≤100 Hz
constexpr uint32_t kIdHostBrakeReq      = 0x301;  // Jetson→RT, on demand
constexpr uint32_t kIdHostObstacleDist  = 0x400;  // Jetson→RT, 10 Hz
constexpr uint32_t kIdRtHeartbeatHigh   = 0x7FD;  // RT→Jetson alive counter, 2 Hz
constexpr uint32_t kIdJetsonHeartbeat   = 0x7FC;  // Jetson→RT alive counter, 2 Hz

// ╔══════════════════════════════════════════════════════════════════════╗
// ║  SYNTREE CAN IDs — factory-programmed, CANNOT be changed          ║
// ║  Steering: EPS-C.  Brake: SEB.  Source: docs/by-wire-*.csv       ║
// ╚══════════════════════════════════════════════════════════════════════╝

// ── SYNTREE EPS-C (steering) ──────────────────────────────────────

constexpr uint32_t kIdSyntreeEpsCmd      = 0x169;  // RT→EPS-C: VCU_SES_Req, 50 Hz (factory default)
constexpr uint32_t kIdSyntreeEpsStatus   = 0x201;  // EPS-C→RT: SES_Status, 100 Hz (factory default)
constexpr uint32_t kIdSyntreeEpsErrInfo  = 0x202;  // EPS-C→RT: SES_ErrInfo, 100 ms (factory default)
constexpr uint32_t kIdSyntreeEpsVersion  = 0x203;  // EPS-C→RT: SES_Version, 1000 ms (factory default)

// ── SYNTREE SEB (brake) ───────────────────────────────────────────

constexpr uint32_t kIdSyntreeSebCmd      = 0x7B9;  // SYS→SEB: VCU_SEB_Req, 50 Hz (factory default)
constexpr uint32_t kIdSyntreeSebStatus   = 0x721;  // SEB→SYS: SEB_Status, 100 Hz (factory default)
constexpr uint32_t kIdSyntreeSebErrInfo  = 0x731;  // SEB→SYS: SEB_ErrInfo, 100 ms (factory default)

// ───────────────────────────────────────────────────────────────────
// Aliases — codebase migration compatibility.
// Preferred names are the canonical ones; aliases let existing code
// compile until it is updated to use the canonical identifiers.
// ───────────────────────────────────────────────────────────────────

constexpr auto kIdHostBrakeRequest  = kIdHostBrakeReq;
constexpr auto kIdHeartbeat         = kIdRtHeartbeatLow;

// Single ESTOP ID — every node sends 0x001.
// Code that distinguishes sender can use these for clarity.
constexpr auto kIdSysEstop  = kIdSafetyEstop;
constexpr auto kIdRtEstop   = kIdSafetyEstop;
constexpr auto kIdHostEstop = kIdSafetyEstop;

// ───────────────────────────────────────────────────────────────────
// Forwarded IDs (same ID on both buses, transparent)
// ───────────────────────────────────────────────────────────────────

// Low→High: kIdSafetyEstop, kIdSysSafetySts, kIdSysThrottleSts, kIdSysDiagRpt
// High→Low: kIdSafetyEstop, kIdHostLightCmd

inline bool is_estop_id(uint32_t id) { return id == kIdSafetyEstop; }

inline bool is_forwarded_low_to_high(uint32_t id) {
    return id == kIdSafetyEstop || id == kIdSysSafetySts
        || id == kIdSysThrottleSts || id == kIdSysDiagRpt;
}

inline bool is_forwarded_high_to_low(uint32_t id) {
    return id == kIdSafetyEstop || id == kIdHostLightCmd;
}

// ───────────────────────────────────────────────────────────────────
// CAN frame (hardware-independent)
// ───────────────────────────────────────────────────────────────────

struct Frame {
    uint32_t id       = 0;
    bool     extended = false;
    uint8_t  dlc      = 0;
    uint8_t  data[8]  = {};

    void put_i32(int offset, int32_t v) { os::write_be32(&data[offset], v); }
    void put_u32(int offset, uint32_t v) { os::write_be32(&data[offset], int32_t(v)); }
    void put_i16(int offset, int16_t v) { os::write_be16(&data[offset], v); }
    void put_u8(int offset, uint8_t v)  { data[offset] = v; }

    int32_t  i32_at(int offset) const { return os::read_be32(&data[offset]); }
    uint32_t u32_at(int offset) const { return uint32_t(os::read_be32(&data[offset])); }
    int16_t  i16_at(int offset) const { return os::read_be16(&data[offset]); }
    uint8_t  u8_at(int offset)  const { return data[offset]; }
};

// ───────────────────────────────────────────────────────────────────
// Shared enums
// ───────────────────────────────────────────────────────────────────

enum class Mode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear  : uint8_t { N = 0, D = 1, S = 2, R = 3 };

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Manual: return "MANUAL";
        case Mode::Auto:   return "AUTO";
        case Mode::Estop:  return "ESTOP";
    }
    return "?";
}

// ───────────────────────────────────────────────────────────────────
// ══ OUR payload structs (we control these) ══
// ───────────────────────────────────────────────────────────────────

// 0x011 SYS_SAFETY_STS — SYS→RT (→Jetson)
struct SysSafetySts {
    bool estop_active = false;
    bool heartbeat_ok = false;   // RT alive counter incrementing

    static SysSafetySts from_frame(const Frame& f) {
        return { f.u8_at(0) != 0, f.u8_at(1) != 0 };
    }
    void to_frame(Frame& f) const {
        f.id = kIdSysSafetySts; f.dlc = 2;
        f.put_u8(0, estop_active ? 1 : 0);
        f.put_u8(1, heartbeat_ok ? 1 : 0);
    }
};

// 0x110 SYS_MODE_CMD — SYS→RT
struct SysModeCmd {
    uint8_t mode = 0;   // Mode enum

    static SysModeCmd from_frame(const Frame& f) { return { f.u8_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdSysModeCmd; f.dlc = 1;
        f.put_u8(0, mode);
    }
};

// 0x120 SYS_THROTTLE_STS — SYS→RT (→Jetson)
struct SysThrottleSts {
    int16_t speed_mmps = 0;

    static SysThrottleSts from_frame(const Frame& f) { return { f.i16_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdSysThrottleSts; f.dlc = 2;
        f.put_i16(0, speed_mmps);
    }
};

// ══ SYNTREE payload structs (factory-fixed, cannot change) ══

// 0x169 VCU_SES_REQ — RT→EPS-C (SYNTREE  // CSV spec, Motorola LSB)
struct VcuSesReq {
    uint8_t  align_enable    : 1;   // Byte0,b0
    uint8_t  control_enable  : 1;   // Byte0,b1
    uint8_t  reserved_0      : 6;   // Byte0,b2-7
    uint8_t  reserved_1         = 0; // Byte1
    int16_t  target_angle       = 0; // Bytes2-3, i16 LE, scale 0.1 deg, -3000..780
    uint16_t target_speed       = 328;// Bytes4-5, u16 LE, scale 1 deg/s, 125-1250, init 0x0148
    // Byte5 also contains security bits (b40-47) — overlaid in pack()
    uint8_t  roll_cnt_enable  : 1;   // Byte5,b40
    uint8_t  checksum_enable  : 1;   // Byte5,b41
    uint8_t  reserved_2       : 2;   // Byte5,b42-43
    uint8_t  rolling_counter  : 4;   // Byte5,b44-47, 0-15
    uint8_t  vehicle_speed       = 0; // Byte6, u8, 0-255 km/h
    uint8_t  checksum           = 0; // Byte7, sum(bytes[0..6]) & 0xFF

    void pack(uint8_t raw[8]) const;
    static VcuSesReq unpack(const uint8_t raw[8]);

    void to_frame(Frame& f) const {
        uint8_t raw[8]; pack(raw);
        f.id = kIdSyntreeEpsCmd; f.dlc = 8;
        for (int i = 0; i < 8; ++i) f.data[i] = raw[i];
    }
};

// 0x204 RT_DRIVE_CMD — RT→SYS
struct RtDriveCmd {
    int32_t motor_speed_mmps = 0;   // [-500, 3000]
    uint8_t gear             = 0;   // Gear enum

    static RtDriveCmd from_frame(const Frame& f) {
        return { f.i32_at(0), f.u8_at(4) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdRtDriveCmd; f.dlc = 5;
        f.put_i32(0, motor_speed_mmps);
        f.put_u8(4, gear);
    }
};

// 0x206 MTR_MOTOR_FBK — MTR(STM32)→SYS+RT
struct MtrMotorFbk {
    int16_t actual_speed_mmps = 0;  // i16 (mm/s)
    uint8_t gear_state        = 0;  // u8 enum {N,D,S,R}
    uint8_t fault_flags       = 0;  // u8 bitmask (bit0=ESTOP_ACTIVE)

    void to_frame(Frame& f) const {
        f.id = kIdMtrMotorFbk; f.dlc = 4;
        f.put_i16(0, actual_speed_mmps);
        f.put_u8(2, gear_state);
        f.put_u8(3, fault_flags);
    }
    static MtrMotorFbk from_frame(const Frame& f) {
        return {f.i16_at(0), f.u8_at(2), f.u8_at(3)};
    }
};

// 0x205 RT_BRAKE_CMD — RT→SYS
struct RtBrakeCmd {
    int32_t brake_pressure_kpa = 0;  // 0 = release

    static RtBrakeCmd from_frame(const Frame& f) { return { f.i32_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdRtBrakeCmd; f.dlc = 4;
        f.put_i32(0, brake_pressure_kpa);
    }
};

// 0x302 HOST_LIGHT_CMD — Jetson→RT (→SYS, forwarded)
struct HostLightCmd {
    bool left_turn   = false;
    bool right_turn  = false;
    bool brake_light = false;
    bool headlight   = false;

    static HostLightCmd from_frame(const Frame& f) {
        uint8_t b = f.u8_at(0);
        return { bool(b & 0x01), bool(b & 0x02), bool(b & 0x04), bool(b & 0x08) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdHostLightCmd; f.dlc = 1;
        uint8_t b = 0;
        if (left_turn)   b |= 0x01;
        if (right_turn)  b |= 0x02;
        if (brake_light) b |= 0x04;
        if (headlight)   b |= 0x08;
        f.put_u8(0, b);
    }
};

// 0x7B9 VCU_SEB_REQ — SYS→SEB (brake command, SYNTREE protocol)
// Little-endian on the wire. Pack/unpack with explicit shifts.
// Wire format per docs/by-wire - brake.csv §VCU_SEB_Req:
//   Byte 0: ctrl bits, Byte 1: rsvd, Byte 2: stroke low, Byte 3: pressure u8,
//   Byte 4: rsvd, Byte 5: rsvd,
//   Byte 6: RollCntEn(1)+ChkSumEn(1)+rsvd(2)+RollCnt(4), Byte 7: checksum
struct VcuSebReq {
    uint8_t align_enable   : 1;   // Byte0,b0
    uint8_t control_enable : 1;   // Byte0,b1
    uint8_t control_mode   : 1;   // Byte0,b2: 0=Stroke, 1=Pressure (CSV: 1-bit)
    uint8_t auto_brake     : 1;   // Byte0,b3
    uint8_t reserved_0     : 4;   // Byte0,b4-7
    uint8_t reserved_1        = 0;
    uint16_t stroke_req       = 600; // raw: (mm+30)/0.05, 600=0mm
    uint8_t  pressure_req     = 0;   // u8: 0–100, scale 0.05 MPa/bit, 0–5 MPa
    uint8_t  reserved_byte5   = 0;
    uint8_t  reserved_6_lo    : 2;   // bits 0-1
    uint8_t  roll_cnt_enable  : 1;   // bit 0 — MUST be 1
    uint8_t  checksum_enable  : 1;   // bit 1 — MUST be 1
    uint8_t  rolling_counter  : 4;   // bits 4-7, 0–15
    uint8_t  checksum         = 0;   // XOR(bytes[0..6]) ^ 0xFF

    void pack(uint8_t raw[8]) const;
    static VcuSebReq unpack(const uint8_t raw[8]);

    void to_frame(Frame& f) const {
        uint8_t raw[8]; pack(raw);
        f.id = kIdSyntreeSebCmd; f.dlc = 8;
        for (int i = 0; i < 8; ++i) f.data[i] = raw[i];
    }
};

// 0x600 SYS_DIAG_RPT — SYS→RT (→Jetson)
struct SysDiagRpt {
    uint8_t  mode          = 0;
    bool     brake_engaged = false;
    bool     heartbeat_ok  = false;
    bool     estop_active  = false;
    uint16_t free_heap_kb  = 0;
    uint8_t  tec           = 0;
    uint8_t  rec           = 0;

    static SysDiagRpt from_frame(const Frame& f) {
        return {
            f.u8_at(0),
            f.u8_at(1) != 0,
            f.u8_at(2) != 0,
            f.u8_at(3) != 0,
            uint16_t(f.i16_at(4)),  // heap KB stored as BE i16
            f.u8_at(6),
            f.u8_at(7),
        };
    }
    void to_frame(Frame& f) const {
        f.id = kIdSysDiagRpt; f.dlc = 8;
        f.put_u8(0, mode);
        f.put_u8(1, brake_engaged ? 1 : 0);
        f.put_u8(2, heartbeat_ok  ? 1 : 0);
        f.put_u8(3, estop_active  ? 1 : 0);
        f.put_i16(4, int16_t(free_heap_kb));
        f.put_u8(6, tec);
        f.put_u8(7, rec);
    }
};

// ───────────────────────────────────────────────────────────────────
// High-level CAN payload types
// ───────────────────────────────────────────────────────────────────

// 0x210 RT_STATE_RPT — RT→Jetson
struct RtStateRpt {
    uint8_t mode        = 0;   // Mode enum
    bool    steer_valid = false;
    bool    reversing   = false;

    void to_frame(Frame& f) const {
        f.id = kIdRtStateRpt; f.dlc = 3;
        f.put_u8(0, mode);
        f.put_u8(1, steer_valid ? 1 : 0);
        f.put_u8(2, reversing   ? 1 : 0);
    }
};

// 0x220 RT_PID_RPT — RT→Jetson (reserved, inactive until encoders fitted)
struct RtPidRpt {
    int16_t speed_setpoint_mmps = 0;
    int16_t speed_measured_mmps = 0;
    int16_t pid_output          = 0;

    void to_frame(Frame& f) const {
        f.id = kIdRtPidRpt; f.dlc = 6;
        f.put_i16(0, speed_setpoint_mmps);
        f.put_i16(2, speed_measured_mmps);
        f.put_i16(4, pid_output);
    }
};

// 0x300 HOST_DRIVE_CMD — Jetson→RT
// Wire format: i32 speed (bytes 0-3), i24 yaw (bytes 4-6), u8 gear (byte 7). DLC=8.
struct HostDriveCmd {
    int32_t speed_mmps      = 0;   // [-500, 3000]
    int32_t yaw_rate_mrad_s = 0;   // [-3000, 3000]
    uint8_t gear            = 0;   // Gear enum (N=0,D=1,S=2,R=3)

    static HostDriveCmd from_frame(const Frame& f) {
        // Decode i24 big-endian from bytes 4-6, then sign-extend to i32
        int32_t yaw = (int32_t(f.u8_at(4)) << 16)
                    | (int32_t(f.u8_at(5)) << 8)
                    |  int32_t(f.u8_at(6));
        if (yaw & 0x800000) yaw |= 0xFF000000;  // sign-extend i24→i32
        return { f.i32_at(0), yaw, f.u8_at(7) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdHostDriveCmd; f.dlc = 8;
        f.put_i32(0, speed_mmps);
        // i24 yaw big-endian in bytes 4-6
        f.put_u8(4, (yaw_rate_mrad_s >> 16) & 0xFF);
        f.put_u8(5, (yaw_rate_mrad_s >> 8)  & 0xFF);
        f.put_u8(6,  yaw_rate_mrad_s        & 0xFF);
        f.put_u8(7, gear);
    }
};

// 0x301 HOST_BRAKE_REQ — Jetson→RT
struct HostBrakeReq {
    int32_t brake_pressure_kpa = 0;

    static HostBrakeReq from_frame(const Frame& f) { return { f.i32_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdHostBrakeReq; f.dlc = 4;
        f.put_i32(0, brake_pressure_kpa);
    }
};

// 0x400 HOST_OBSTACLE_DIST — Jetson→RT (perception pipeline)
struct HostObstacleDist {
    uint32_t distance_mm = 0;   // UINT32_MAX = no reading

    static HostObstacleDist from_frame(const Frame& f) { return { f.u32_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdHostObstacleDist; f.dlc = 4;
        f.put_u32(0, distance_mm);
    }
};

// ───────────────────────────────────────────────────────────────────
// SYNTREE little-endian pack/unpack (Motorola LSB)
// ───────────────────────────────────────────────────────────────────

inline void VcuSesReq::pack(uint8_t raw[8]) const {
    raw[0] = (align_enable & 1) | ((control_enable & 1) << 1)
           | ((reserved_0 & 0x3F) << 2);
    raw[1] = reserved_1;
    raw[2] = target_angle & 0xFF;
    raw[3] = (target_angle >> 8) & 0xFF;
    raw[4] = target_speed & 0xFF;
    raw[5] = (target_speed >> 8) & 0xFF;
    // Overlay security bits on Byte 5 (bits 40-47 of frame)
    raw[5] &= 0xF0;  // preserve upper nibble (rolling_counter), clear lower nibble for enable bits
    raw[5] |= (roll_cnt_enable & 1);          // bit 0 = RollCnt_Enable
    raw[5] |= (checksum_enable & 1) << 1;     // bit 1 = CheckSum_Enable
    raw[5] |= (rolling_counter & 0xF) << 4;   // bits 4-7 = RollCnt
    raw[6] = vehicle_speed & 0xFF;
    //  // Checksum: sum of bytes 0-6 (CSV spec: "CheckSum=Byte0+Byte1...")
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i) sum += raw[i];
    raw[7] = sum & 0xFF;
}

inline VcuSesReq VcuSesReq::unpack(const uint8_t raw[8]) {
    VcuSesReq r;
    r.align_enable   = raw[0] & 1;
    r.control_enable = (raw[0] >> 1) & 1;
    r.reserved_0     = (raw[0] >> 2) & 0x3F;
    r.reserved_1     = raw[1];
    r.target_angle   = int16_t(raw[2] | (raw[3] << 8));
    r.target_speed   = uint16_t(raw[4] | (raw[5] << 8)) & 0x0FFF; // lower 12 bits of Byte5
    r.roll_cnt_enable = raw[5] & 1;
    r.checksum_enable = (raw[5] >> 1) & 1;
    r.reserved_2     = (raw[5] >> 2) & 3;
    r.rolling_counter = (raw[5] >> 4) & 0xF;
    r.vehicle_speed  = raw[6];
    r.checksum       = raw[7];
    return r;
}

inline void VcuSebReq::pack(uint8_t raw[8]) const {
    raw[0] = (align_enable & 1) | ((control_enable & 1) << 1)
           | ((control_mode & 1) << 2) | ((auto_brake & 1) << 3)
           | ((reserved_0 & 0xF) << 4);
    raw[1] = reserved_1;
    raw[2] = stroke_req & 0xFF;
    raw[3] = pressure_req;                    // u8 pressure (0-100) at Byte3,b24
    raw[4] = reserved_byte5;                  // reserved
    raw[5] = 0;                               // reserved
    raw[6] = (roll_cnt_enable & 1)            // bit 0 = RollCnt_Enable
           | ((checksum_enable & 1) << 1)     // bit 1 = CheckSum_Enable
           | ((rolling_counter & 0xF) << 4);  // bits 4-7 = RollCnt
    // checksum
    uint8_t ck = 0;
    for (int i = 0; i < 7; ++i) ck ^= raw[i];
    raw[7] = ck ^ 0xFF;
}

inline VcuSebReq VcuSebReq::unpack(const uint8_t raw[8]) {
    VcuSebReq r;
    r.align_enable    = raw[0] & 1;
    r.control_enable  = (raw[0] >> 1) & 1;
    r.control_mode    = (raw[0] >> 2) & 1;  //  // CSV: 1-bit
    r.auto_brake      = (raw[0] >> 3) & 1;
    r.reserved_0      = (raw[0] >> 4) & 0xF;
    r.reserved_1      = raw[1];
    r.stroke_req      = uint16_t(raw[2] | (raw[3] << 8));  // bytes 2-3 LE (low byte first)
    r.pressure_req    = raw[3];                             // u8 pressure (also mapped to byte 3, overlaps stroke high byte)
    r.reserved_byte5  = raw[4];
    r.reserved_6_lo   = raw[6] & 3;
    r.roll_cnt_enable = raw[6] & 1;
    r.checksum_enable = (raw[6] >> 1) & 1;
    r.rolling_counter = (raw[6] >> 4) & 0xF;
    r.checksum        = raw[7];
    return r;
}

// ───────────────────────────────────────────────────────────────────
// Type aliases — migration compatibility.
// Code references these names; the canonical names are defined above.
// ───────────────────────────────────────────────────────────────────

using HostBrakeRequest = HostBrakeReq;
using RtObstacleDist   = HostObstacleDist;
using SysDiag          = SysDiagRpt;

} // namespace can
