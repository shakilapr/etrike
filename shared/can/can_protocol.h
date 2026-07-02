#pragma once
// CAN protocol definitions — five-node distributed architecture.
// Low-level CAN (500 kbit/s): RT, SYS, PWT, EPS-C, SEB.
// High-level CAN (500 kbit/s): RT, Host.
// Powertrain CAN (250 kbit/s): PWT, DC-DC converter, motor controller.
// RT bridges selected IDs between high and low buses (§2.3 architecture.md).
// PWT bridges selected IDs between low and powertrain buses (pwt-esp32/pwt-architecture.md).
// PWT bridges selected IDs between low and powertrain buses (pwt-esp32/pwt-architecture.md).

#include <cstdint>
#include "endian.h"

namespace can {

// ╔══════════════════════════════════════════════════════════════════════╗
// ║  OUR CAN IDs — we control these, can reassign as needed            ║
// ╚══════════════════════════════════════════════════════════════════════╝

// ── Low-level bus (our IDs) ───────────────────────────────────────

constexpr uint32_t kIdSafetyEstop       = 0x001;  // any→all, bridged to high
constexpr uint32_t kIdSysSafetySts      = 0x011;  // SYS→RT (→Host), 5 Hz
constexpr uint32_t kIdSysDcdcCmd        = 0x012;  // SYS→PWT→DC-DC converter, on change. PWT bridges 500k→250k.
constexpr uint32_t kIdSysModeCmd        = 0x110;  // SYS→RT, on change
constexpr uint32_t kIdSysThrottleSts    = 0x120;  // MTR(STM32)→RT (→Host), 100 Hz (SYS_ prefix is historical)
constexpr uint32_t kIdRtDriveCmd        = 0x204;  // RT→SYS motor speed+gear, 100 Hz
constexpr uint32_t kIdRtBrakeCmd        = 0x205;  // RT→SYS brake pressure kPa, 50 Hz
constexpr uint32_t kIdMtrMotorFbk       = 0x206;  // MTR(STM32)→SYS+RT (→Host via RT forwarding), 50 Hz
constexpr uint32_t kIdHostLightCmd      = 0x302;  // RT(fwd)→SYS light bitfield, on change
constexpr uint32_t kIdSysDiagRpt        = 0x600;  // SYS→RT (→Host via RT forwarding), 1 Hz
constexpr uint32_t kIdRtHeartbeatLow    = 0x7FD;  // RT→SYS alive counter, 2 Hz
constexpr uint32_t kIdSysHeartbeat      = 0x7FE;  // SYS→RT alive counter, 10 Hz

// ── High-level bus (our IDs) ──────────────────────────────────────

constexpr uint32_t kIdRtStateRpt        = 0x210;  // RT→Host, 10 Hz
constexpr uint32_t kIdRtPidRpt          = 0x220;  // RT→Host, reserved (future PID)
constexpr uint32_t kIdHostDriveCmd      = 0x300;  // Host→RT, ≤100 Hz
constexpr uint32_t kIdHostBrakeReq      = 0x301;  // Host→RT, on demand
constexpr uint32_t kIdHostObstacleDist  = 0x400;  // Host→RT, 10 Hz
constexpr uint32_t kIdRtHeartbeatHigh   = 0x7FD;  // RT→Host alive counter, 2 Hz
constexpr uint32_t kIdHostHeartbeat     = 0x7FC;  // Host→RT alive counter, 2 Hz
constexpr uint32_t kIdSteerDiag         = 0x310;  // RT→Host steering telemetry, 10 Hz
constexpr uint32_t kIdBrakeDiag         = 0x311;  // RT→Host brake telemetry, 10 Hz

// ╔══════════════════════════════════════════════════════════════════════╗
// ║  steer-by-wire CAN IDs — factory-programmed, CANNOT be changed          ║
// ║  Steering: EPS-C.  Brake: SEB.  Source: docs/by-wire-*.csv       ║
// ╚══════════════════════════════════════════════════════════════════════╝

// ── steer-by-wire unit (steering) ──────────────────────────────────────

constexpr uint32_t kIdSbwCmd      = 0x169;  // RT→EPS-C: VCU_SES_Req, 50 Hz (factory default)
constexpr uint32_t kIdSbwStatus   = 0x201;  // EPS-C→RT: SES_Status, 100 Hz (factory default)
constexpr uint32_t kIdSbwErrInfo  = 0x202;  // EPS-C→RT: SES_ErrInfo, 100 ms (factory default)
constexpr uint32_t kIdSbwVersion  = 0x203;  // EPS-C→RT: SES_Version, 1000 ms (factory default)
constexpr uint32_t kIdSbwTest     = 0x6FA;  // EPS-C→RT: SES_Test, 100 Hz (factory default)

// ── brake-by-wire unit (brake) ───────────────────────────────────────────

constexpr uint32_t kIdBbwCmd      = 0x7B9;  // SYS→SEB: VCU_SEB_Req, 50 Hz (factory default)
constexpr uint32_t kIdBbwStatus   = 0x721;  // SEB→SYS: SEB_Status, 100 Hz (factory default)
constexpr uint32_t kIdBbwErrInfo  = 0x731;  // SEB→SYS: SEB_ErrInfo, 100 ms (factory default)
constexpr uint32_t kIdBbwTest     = 0x6FB;  // SEB→SYS: SEB_Test, 100 Hz (factory default)
constexpr uint32_t kIdBbwVersion  = 0x741;  // SEB→SYS: SEB_Version, 1000 ms (factory default)

// ───────────────────────────────────────────────────────────────────
// Aliases — codebase migration compatibility.
// Preferred names are the canonical ones; aliases let existing code
// compile until it is updated to use the canonical identifiers.
// ───────────────────────────────────────────────────────────────────

// Single ESTOP ID — every node sends 0x001.

// ───────────────────────────────────────────────────────────────────
// Forwarded IDs (same ID on both buses, transparent)
// ───────────────────────────────────────────────────────────────────

// Low→High: kIdSafetyEstop, kIdSysSafetySts, kIdSysThrottleSts, kIdMtrMotorFbk, kIdSysDiagRpt
// High→Low: kIdSafetyEstop, kIdHostLightCmd
//
// Used by RT CAN router (can_rx_router.h). Keep in sync with the router dispatch.

inline bool is_estop_id(uint32_t id) { return id == kIdSafetyEstop; }

inline bool is_forwarded_low_to_high(uint32_t id) {
    return id == kIdSafetyEstop || id == kIdSysSafetySts
        || id == kIdSysThrottleSts || id == kIdMtrMotorFbk
        || id == kIdSysDiagRpt;
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

// 0x011 SYS_SAFETY_STS — SYS→RT (→Host)
struct SysSafetySts {
    bool estop_active = false;
    bool heartbeat_ok = false;   // RT alive counter incrementing
    uint8_t light_state = 0;     // v0.0.5: bit0=left, bit1=right, bit2=brake, bit3=head

    static SysSafetySts from_frame(const Frame& f) {
        SysSafetySts s{ f.u8_at(0) != 0, f.u8_at(1) != 0, 0 };
        if (f.dlc >= 3) s.light_state = f.u8_at(2);
        return s;
    }
    void to_frame(Frame& f) const {
        f.id = kIdSysSafetySts; f.dlc = 3;
        f.put_u8(0, estop_active ? 1 : 0);
        f.put_u8(1, heartbeat_ok ? 1 : 0);
        f.put_u8(2, light_state);
    }
};

// 0x110 SYS_MODE_CMD — SYS→RT
struct SysModeCmd {
    uint8_t mode = 0;   // Mode enum

    static SysModeCmd from_frame(const Frame& f) { if (f.dlc < 1) return {}; return { f.u8_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdSysModeCmd; f.dlc = 1;
        f.put_u8(0, mode);
    }
};

// 0x120 SYS_THROTTLE_STS — SYS→RT (→Host)
struct SysThrottleSts {
    int16_t speed_mmps = 0;

    static SysThrottleSts from_frame(const Frame& f) { if (f.dlc < 2) return {}; return { f.i16_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdSysThrottleSts; f.dlc = 2;
        f.put_i16(0, speed_mmps);
    }
};

// 0x310 STEER_DIAG — RT→Host steering telemetry @10Hz (v0.0.4)
struct SteerDiag {
    int16_t  angle_0_1deg;     // bytes 0-1: actual steering angle (0.1°/bit, offset -3000→subtract 30000)
    uint8_t  fault;            // byte 2: 0=OK, 1=EPS-C fault
    uint16_t motor_current;    // bytes 3-4: EPS-C motor current (0.01A/bit)
    uint16_t ecu_temp;         // bytes 5-6: EPS-C ECU temperature (0.1°C/bit)
    uint8_t  reserved;         // byte 7

    void to_frame(Frame& f) const {
        f.id = kIdSteerDiag; f.dlc = 8;
        f.put_i16(0, angle_0_1deg);
        f.put_u8(2, fault);
        f.put_i16(3, motor_current);
        f.put_i16(5, ecu_temp);
        f.put_u8(7, 0);
    }
    static SteerDiag from_frame(const Frame& f) { if (f.dlc < 8) return {}; return {f.i16_at(0), f.u8_at(2), uint16_t(f.i16_at(3)), uint16_t(f.i16_at(5)), f.u8_at(7)}; }
};

// 0x311 BRAKE_DIAG — RT→Host brake telemetry @10Hz (v0.0.4)
struct BrakeDiag {
    uint16_t pressure_raw;     // bytes 0-1: SEB pressure (SEB raw, 0.05 MPa/bit)
    uint8_t  fault;            // byte 2: 0=OK, 1=SEB fault
    uint16_t motor_current;    // bytes 3-4: SEB motor current (0.01A/bit)
    uint16_t ecu_temp;         // bytes 5-6: SEB ECU temperature (0.1°C/bit)
    uint8_t  reserved;         // byte 7

    void to_frame(Frame& f) const {
        f.id = kIdBrakeDiag; f.dlc = 8;
        f.put_i16(0, int16_t(pressure_raw));
        f.put_u8(2, fault);
        f.put_i16(3, motor_current);
        f.put_i16(5, ecu_temp);
        f.put_u8(7, 0);
    }
    static BrakeDiag from_frame(const Frame& f) { if (f.dlc < 8) return {}; return {uint16_t(f.i16_at(0)), f.u8_at(2), uint16_t(f.i16_at(3)), uint16_t(f.i16_at(5)), f.u8_at(7)}; }
};

// ══ steer-by-wire payload structs (factory-fixed, cannot change) ══

// 0x169 VCU_SES_REQ — RT→EPS-C (steer-by-wire  // CSV spec, Motorola LSB)
struct VcuSesReq {
    uint8_t  align_enable    : 1;   // Byte0,b0
    uint8_t  control_enable  : 1;   // Byte0,b1
    uint8_t  reserved_0      : 6;   // Byte0,b2-7
    uint8_t  reserved_1         = 0; // Byte1
    int16_t  target_angle       = 0; // Bytes2-3, i16 LE, scale 0.1 deg/bit, offset -3000 (CSV), ±700 deg physical
    uint16_t target_speed       = 328;// Bytes4-5, u16 LE, scale 1 deg/s, 125-525 (CSV), init 328 (0x148)
    // Byte5 also contains security bits (b40-47) — overlaid in pack()
    uint8_t  roll_cnt_enable  : 1;   // Byte5,b40
    uint8_t  checksum_enable  : 1;   // Byte5,b41
    uint8_t  speed_bits_9_8   : 2;   // Byte5,b42-43 (target_speed[9:8], overlaps with security bits)
    uint8_t  rolling_counter  : 4;   // Byte5,b44-47, 0-15
    uint8_t  vehicle_speed       = 0; // Byte6, u8, 0-255 km/h
    uint8_t  checksum           = 0; // Byte7, XOR(bytes[0..6]) ^ 0xFF

    void pack(uint8_t raw[8]) const;
    static VcuSesReq unpack(const uint8_t raw[8]);

    void to_frame(Frame& f) const {
        uint8_t raw[8]; pack(raw);
        f.id = kIdSbwCmd; f.dlc = 8;
        for (int i = 0; i < 8; ++i) f.data[i] = raw[i];
    }
};

// 0x204 RT_DRIVE_CMD — RT→SYS
struct RtDriveCmd {
    int32_t motor_speed_mmps = 0;   // [-500, 3000]
    uint8_t gear             = 0;   // Gear enum

    static RtDriveCmd from_frame(const Frame& f) {
        if (f.dlc < 5) return {};  // corrupt frame → safe default (zero speed, N gear)
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
        if (f.dlc < 4) return {};  // corrupt → safe default
        return {f.i16_at(0), f.u8_at(2), f.u8_at(3)};
    }
};

// 0x205 RT_BRAKE_CMD — RT→SYS
struct RtBrakeCmd {
    int32_t brake_pressure_kpa = 0;  // 0 = release

    static RtBrakeCmd from_frame(const Frame& f) {
        if (f.dlc < 4) return {};  // corrupt → zero brake
        return { f.i32_at(0) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdRtBrakeCmd; f.dlc = 4;
        f.put_i32(0, brake_pressure_kpa);
    }
};

// 0x302 HOST_LIGHT_CMD — Host→RT (→SYS, forwarded)
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

// 0x7B9 VCU_SEB_REQ — SYS→SEB (brake command, steer-by-wire protocol)
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
    uint16_t stroke_req       = 600; // raw: (mm+30)/0.05, 600=0mm (CSV init 0x0 = -30mm; 600 is safer — no unintended brake)
    uint8_t  pressure_req     = 0;   // u8: 0–100, scale 0.05 MPa/bit, 0–5 MPa
    uint8_t  reserved_4       = 0;   // byte 4 on wire
    uint8_t  roll_cnt_enable  : 1;   // bit 0 — MUST be 1
    uint8_t  checksum_enable  : 1;   // bit 1 — MUST be 1
    uint8_t  reserved_6_mid   : 2;   // bits 2-3 (unused)
    uint8_t  rolling_counter  : 4;   // bits 4-7, 0–15
    uint8_t  checksum         = 0;   // XOR(bytes[0..6]) ^ 0xFF

    void pack(uint8_t raw[8]) const;
    static VcuSebReq unpack(const uint8_t raw[8]);

    void to_frame(Frame& f) const {
        uint8_t raw[8]; pack(raw);
        f.id = kIdBbwCmd; f.dlc = 8;
        for (int i = 0; i < 8; ++i) f.data[i] = raw[i];
    }
};

// 0x600 SYS_DIAG_RPT — SYS→RT (→Host)
struct SysDiagRpt {
    uint8_t  mode          = 0;
    bool     brake_engaged = false;
    bool     brake_fault   = false;  // v0.0.5: brake following-error active or SEB L3
    bool     heartbeat_ok  = false;
    bool     estop_active  = false;
    uint8_t  rx_overflow   = 0;    // CAN RX overflow counter (6-bit, packed into byte 2 bits 1-6)
    uint16_t free_heap_kb  = 0;
    uint8_t  tec           = 0;
    uint8_t  rec           = 0;

    static SysDiagRpt from_frame(const Frame& f) {
        if (f.dlc < 8) return {};
        return {
            f.u8_at(0),
            (f.u8_at(1) & 1) != 0,
            (f.u8_at(1) & 2) != 0,   // brake_fault in byte1 bit1
            (f.u8_at(2) & 1) != 0,
            (f.u8_at(3) & 1) != 0,
            uint8_t((f.u8_at(2) >> 1) & 0x3F),  // rx_overflow in byte2 bits 1-6
            uint16_t(f.i16_at(4)),   // heap KB stored as BE i16
            f.u8_at(6),
            f.u8_at(7),
        };
    }
    void to_frame(Frame& f) const {
        f.id = kIdSysDiagRpt; f.dlc = 8;
        f.put_u8(0, mode);
        f.put_u8(1, (brake_engaged ? 1 : 0) | (brake_fault ? 2 : 0));
        f.put_u8(2, (heartbeat_ok ? 1 : 0) | ((rx_overflow & 0x3F) << 1));
        f.put_u8(3, estop_active  ? 1 : 0);
        f.put_i16(4, int16_t(free_heap_kb));
        f.put_u8(6, tec);
        f.put_u8(7, rec);
    }
};

// ───────────────────────────────────────────────────────────────────
// High-level CAN payload types
// ───────────────────────────────────────────────────────────────────

// ESTOP reason codes for RtStateRpt byte 1 bits 4-7
constexpr uint8_t kEstopReasonNone           = 0;
constexpr uint8_t kEstopReasonButton         = 1;  // physical ESTOP button
constexpr uint8_t kEstopReasonHeartbeat      = 2;  // SYS or Host heartbeat timeout
constexpr uint8_t kEstopReasonFollowingError = 3;  // steering following error
constexpr uint8_t kEstopReasonObstacle       = 4;  // obstacle within stop distance
constexpr uint8_t kEstopReasonCanEstop       = 5;  // 0x001 received from another node
constexpr uint8_t kEstopReasonBusOff         = 6;  // CAN bus-off persistent
constexpr uint8_t kEstopReasonInternal       = 7;  // SEB L3, EPS-C L3 fault, etc.

// 0x210 RT_STATE_RPT — RT→Host + SYS (low bus)
struct RtStateRpt {
    uint8_t mode         = 0;   // Mode enum (0=Manual,1=Auto,2=ESTOP)
    uint8_t safety_state = 0;   // byte 1 bits 0-1: 0=Normal,1=InternalEstop,2=Fault
    bool    reversing    = false;
    uint8_t rx_overflow  = 0;   // High CAN RX overflow counter (wraps at 256)

    uint8_t task_health  = 0;   // byte 4: bitmask of alive tasks (bits 0-3)
    uint8_t estop_reason = 0;   // byte 1 bits 4-7: reason for ESTOP state
    uint8_t steer_state  = 0;   // byte 5: steering state machine state

    void to_frame(Frame& f) const {
        f.id = kIdRtStateRpt; f.dlc = 6;
        f.put_u8(0, mode);
        f.put_u8(1, (safety_state & 0x03) | ((estop_reason & 0x0F) << 4));
        f.put_u8(2, reversing ? 1 : 0);
        f.put_u8(3, rx_overflow);
        f.put_u8(4, task_health);
        f.put_u8(5, steer_state);
    }
    static RtStateRpt from_frame(const Frame& f) {
        if (f.dlc < 4) return {};
        uint8_t b1 = f.u8_at(1);
        return {
            f.u8_at(0),
            uint8_t(b1 & 0x03),                          // safety_state bits 0-1
            f.u8_at(2) != 0,
            f.u8_at(3),
            f.dlc >= 5 ? f.u8_at(4) : uint8_t(0),       // task_health
            uint8_t((b1 >> 4) & 0x0F),                   // estop_reason bits 4-7
            f.dlc >= 6 ? f.u8_at(5) : uint8_t(0)         // steer_state
        };
    }
};

// 0x220 RT_PID_RPT — RT→Host (reserved, inactive until encoders fitted)
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

// 0x300 HOST_DRIVE_CMD — Host→RT
// Wire format: i32 speed (bytes 0-3), i24 yaw (bytes 4-6), u8 gear (byte 7). DLC=8.
struct HostDriveCmd {
    int32_t speed_mmps      = 0;   // [-500, 3000]
    int32_t yaw_rate_mrad_s = 0;   // [-3000, 3000]
    uint8_t gear            = 0;   // Gear enum (N=0,D=1,S=2,R=3)

    static HostDriveCmd from_frame(const Frame& f) {
        if (f.dlc < 8) return {};  // corrupt → safe default
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

// 0x301 HOST_BRAKE_REQ — Host→RT
struct HostBrakeReq {
    int32_t brake_pressure_kpa = 0;

    static HostBrakeReq from_frame(const Frame& f) {
        if (f.dlc < 4) return {};  // corrupt → zero brake
        return { f.i32_at(0) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdHostBrakeReq; f.dlc = 4;
        f.put_i32(0, brake_pressure_kpa);
    }
};

// 0x400 HOST_OBSTACLE_DIST — Host→RT (perception pipeline)
struct HostObstacleDist {
    uint32_t distance_mm = 0;   // UINT32_MAX = no reading

    static HostObstacleDist from_frame(const Frame& f) { if (f.dlc < 4) return {UINT32_MAX}; return { f.u32_at(0) }; }
    void to_frame(Frame& f) const {
        f.id = kIdHostObstacleDist; f.dlc = 4;
        f.put_u32(0, distance_mm);
    }
};

// ───────────────────────────────────────────────────────────────────
// steer-by-wire little-endian pack/unpack (Motorola LSB)
// ───────────────────────────────────────────────────────────────────

inline void VcuSesReq::pack(uint8_t raw[8]) const {
    raw[0] = (align_enable & 1) | ((control_enable & 1) << 1)
           | ((reserved_0 & 0x3F) << 2);
    raw[1] = reserved_1;
    raw[2] = target_angle & 0xFF;
    raw[3] = (target_angle >> 8) & 0xFF;
    raw[4] = target_speed & 0xFF;
    // Byte 5: security signals overlay target_speed bits 8-15.
    // Effective speed is 10-bit: bits 0-7 in byte 4, bits 8-9 in byte 5 bits 2-3.
    // Bits 10-15 are overlaid by security signals (enable bits + rolling counter).
    raw[5] = (roll_cnt_enable & 1)                      // bit 0 = RollCnt_Enable
           | ((checksum_enable & 1) << 1)                // bit 1 = CheckSum_Enable
           | (((target_speed >> 8) & 0x3) << 2)          // bits 2-3 = speed bits 9-8
           | ((rolling_counter & 0xF) << 4);              // bits 4-7 = RollCnt
    raw[6] = vehicle_speed & 0xFF;
    // Checksum: XOR(bytes 0-6) ^ 0xFF (per steer-by-wire CSV spec)
    uint8_t cksum = 0;
    for (int i = 0; i < 7; ++i) cksum ^= raw[i];
    raw[7] = cksum ^ 0xFF;
}

inline VcuSesReq VcuSesReq::unpack(const uint8_t raw[8]) {
    VcuSesReq r;
    r.align_enable   = raw[0] & 1;
    r.control_enable = (raw[0] >> 1) & 1;
    r.reserved_0     = (raw[0] >> 2) & 0x3F;
    r.reserved_1     = raw[1];
    r.target_angle   = int16_t(raw[2] | (raw[3] << 8));
    r.target_speed   = uint16_t(raw[4] | ((raw[5] & 0x0C) << 6)); // byte4 + bits 2-3 of byte5 = 10-bit speed
    r.roll_cnt_enable = raw[5] & 1;
    r.checksum_enable = (raw[5] >> 1) & 1;
    r.speed_bits_9_8 = (raw[5] >> 2) & 3;
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
    if (control_mode == 0) {
        raw[3] = (stroke_req >> 8) & 0xFF;   // byte 3: stroke high byte in Stroke mode
    } else {
        raw[3] = pressure_req;                // byte 3: pressure value in Pressure mode
    }
    raw[4] = reserved_4;                      // reserved
    raw[5] = 0;                               // reserved
    raw[6] = (roll_cnt_enable & 1)            // bit 0 = RollCnt_Enable
           | ((checksum_enable & 1) << 1)     // bit 1 = CheckSum_Enable
           | ((rolling_counter & 0xF) << 4);  // bits 4-7 = RollCnt
    // Checksum: XOR(bytes 0-6) ^ 0xFF (per steer-by-wire CSV spec)
    uint8_t cksum = 0;
    for (int i = 0; i < 7; ++i) cksum ^= raw[i];
    raw[7] = cksum ^ 0xFF;
}

inline VcuSebReq VcuSebReq::unpack(const uint8_t raw[8]) {
    VcuSebReq r;
    r.align_enable    = raw[0] & 1;
    r.control_enable  = (raw[0] >> 1) & 1;
    r.control_mode    = (raw[0] >> 2) & 1;  //  // CSV: 1-bit
    r.auto_brake      = (raw[0] >> 3) & 1;
    r.reserved_0      = (raw[0] >> 4) & 0xF;
    r.reserved_1      = raw[1];
    if (r.control_mode == 0) {
        r.stroke_req   = uint16_t(raw[2] | (raw[3] << 8)); // bytes 2-3 LE: full 16-bit stroke
        r.pressure_req = 0;
    } else {
        r.stroke_req   = raw[2];                            // byte 2 only: stroke low byte in Pressure mode
        r.pressure_req = raw[3];                            // byte 3: pressure value
    }
    r.reserved_4      = raw[4];
    r.reserved_6_mid  = (raw[6] >> 2) & 3;                  // bits 2-3 (unused)
    r.roll_cnt_enable = raw[6] & 1;
    r.checksum_enable = (raw[6] >> 1) & 1;
    r.rolling_counter = (raw[6] >> 4) & 0xF;
    r.checksum        = raw[7];
    return r;
}

// 0x201 SES_Status — EPS-C→RT (steer-by-wire status frame, per docs/by-wire - steering.csv)
struct SesStatus {
    uint8_t  angle_status      : 1;  // bit 0: 0=center finding, 1=found
    uint8_t  control_mode_sts  : 2;  // bits 1-2: 0=manual, 1=automatic
    uint8_t  reserved_0        : 3;  // bits 3-5
    uint8_t  error_status      : 2;  // bits 6-7: 0=normal, 1=L1, 2=L2, 3=L3
    uint8_t  reserved_1;
    uint16_t str_angle;             // bytes 2-3: steering angle (Unsigned per CSV, 0.1 deg/bit, offset -3000)
    int16_t  tgt_angle_spd;         // bytes 4-5: target angle speed (0.5 deg/s/bit, signed)
    uint8_t  steering_torq;         // byte 5 (overlaps tgt_angle_spd MSB) — scale 0.1, offset -12.1 Nm
    uint8_t  roll_cnt_enable_sts : 1; // byte 6 bit 0
    uint8_t  checksum_enable_sts : 1; // byte 6 bit 1
    uint8_t  reserved_6          : 2; // byte 6 bits 2-3
    uint8_t  roll_cnt_sts        : 4; // byte 6 bits 4-7
    uint8_t  checksum_sts;           // byte 7

    static SesStatus from_frame(const Frame& f);
};

inline SesStatus SesStatus::from_frame(const Frame& f) {
    if (f.dlc < 8) return {};
    SesStatus r;
    const uint8_t* raw = f.data;
    r.angle_status        = raw[0] & 1;
    r.control_mode_sts    = (raw[0] >> 1) & 3;
    r.reserved_0          = (raw[0] >> 3) & 7;
    r.error_status        = (raw[0] >> 6) & 3;
    r.reserved_1          = raw[1];
    r.str_angle           = uint16_t(raw[2] | (raw[3] << 8));  // LE (Motorola LSB), Unsigned per CSV
    r.tgt_angle_spd       = int16_t(raw[4] | (raw[5] << 8));  // LE, signed
    r.steering_torq       = raw[5];  // overlaps tgt_angle_spd MSB — per CSV
    r.roll_cnt_enable_sts = raw[6] & 1;
    r.checksum_enable_sts = (raw[6] >> 1) & 1;
    r.reserved_6          = (raw[6] >> 2) & 3;
    r.roll_cnt_sts        = (raw[6] >> 4) & 0xF;
    r.checksum_sts        = raw[7];
    return r;
}

// 0x721 SEB_Status — SEB→SYS (steer-by-wire status frame, per docs/by-wire - brake.csv)
struct SebStatus {
    uint8_t  alignment_status   : 1;  // bit 0
    uint8_t  control_enable_sts : 1;  // bit 1
    uint8_t  control_mode_sts   : 2;  // bits 2-3
    uint8_t  auto_brake_sts     : 1;  // bit 4
    uint8_t  reserved_0         : 1;  // bit 5
    uint8_t  error_status       : 2;  // bits 6-7: 0=normal, 1=L1, 2=L2, 3=L3
    uint8_t  reserved_1;
    uint16_t stroke_value;           // bytes 2-3: stroke (0.05mm/bit, offset -30mm)
    uint8_t  pressure_value;         // byte 3 (overlaps stroke MSB — mode-dependent)
    int16_t  angle_value;            // bytes 5-6: angle feedback (0.5/bit, -150 to 840)
    uint8_t  roll_cnt_enable_sts : 1; // byte 6 bit 0
    uint8_t  checksum_enable_sts : 1; // byte 6 bit 1
    uint8_t  reserved_6          : 2; // byte 6 bits 2-3
    uint8_t  roll_cnt_sts        : 4; // byte 6 bits 4-7
    uint8_t  checksum_sts;           // byte 7

    static SebStatus from_frame(const Frame& f);
};

inline SebStatus SebStatus::from_frame(const Frame& f) {
    if (f.dlc < 8) return {};
    SebStatus r;
    const uint8_t* raw = f.data;
    r.alignment_status    = raw[0] & 1;
    r.control_enable_sts  = (raw[0] >> 1) & 1;
    r.control_mode_sts    = (raw[0] >> 2) & 3;
    r.auto_brake_sts      = (raw[0] >> 4) & 1;
    r.reserved_0          = (raw[0] >> 5) & 1;
    r.error_status        = (raw[0] >> 6) & 3;
    r.reserved_1          = raw[1];
    r.stroke_value        = uint16_t(raw[2] | (raw[3] << 8));  // LE
    r.pressure_value      = raw[3];  // overlaps stroke MSB
    r.angle_value         = int16_t(raw[5] | (raw[6] << 8));   // LE, signed — shares byte 6 with status bits
    r.roll_cnt_enable_sts = raw[6] & 1;
    r.checksum_enable_sts = (raw[6] >> 1) & 1;
    r.reserved_6          = (raw[6] >> 2) & 3;
    r.roll_cnt_sts        = (raw[6] >> 4) & 0xF;
    r.checksum_sts        = raw[7];
    return r;
}

// ───────────────────────────────────────────────────────────────────
// Type aliases — migration compatibility.
// Code references these names; the canonical names are defined above.
// ───────────────────────────────────────────────────────────────────



} // namespace can
