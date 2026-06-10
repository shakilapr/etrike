#pragma once
// Direct RT<->SYS protocol for the dedicated inter-MCU link.
// Transport can be high-speed UART or SPI; payload format is transport-neutral.

#include <cstdint>
#include "os/endian.h"

namespace inter_mcu {

constexpr uint8_t kSof0 = 0xA5;
constexpr uint8_t kSof1 = 0x5A;
constexpr int kMaxPayload = 16;
constexpr int32_t kMotorEffortMax = 8191;  // 13-bit signed PWM effort limit

enum class MessageType : uint8_t {
    RtToSysSetpoint = 0x10,
    RtHeartbeat     = 0x11,
    RtObstacleDist  = 0x12,
    SysToRtStatus   = 0x20,
    SysHeartbeat    = 0x21,
};

constexpr uint8_t kFlagEstop       = 1u << 0;
constexpr uint8_t kFlagAutoEnable  = 1u << 1;
constexpr uint8_t kFlagBrakeEnable = 1u << 2;
constexpr uint8_t kFlagEpsEnable   = 1u << 3;

struct Frame {
    MessageType type = MessageType::RtHeartbeat;
    uint8_t seq = 0;
    uint8_t dlc = 0;
    uint8_t data[kMaxPayload] = {};

    void put_i32(int offset, int32_t v) { os::write_be32(&data[offset], v); }
    void put_i16(int offset, int16_t v) { os::write_be16(&data[offset], v); }
    void put_u8(int offset, uint8_t v) { data[offset] = v; }

    int32_t i32_at(int offset) const { return os::read_be32(&data[offset]); }
    int16_t i16_at(int offset) const { return os::read_be16(&data[offset]); }
    uint8_t u8_at(int offset) const { return data[offset]; }
};

struct RtToSysSetpoint {
    int32_t motor_effort_pwm = 0;       // signed traction effort [-8191, +8191]
    int32_t steer_angle_mdeg = 0;       // EPS-C target angle, +right
    int32_t brake_pressure_kpa = 0;     // SEB pressure target; 0 releases pressure
    uint8_t flags = 0;                  // kFlag*

    static RtToSysSetpoint from_frame(const Frame& f) {
        return {
            f.i32_at(0),
            f.i32_at(4),
            f.i32_at(8),
            f.u8_at(12),
        };
    }

    void to_frame(Frame& f) const {
        f.type = MessageType::RtToSysSetpoint;
        f.dlc = 13;
        f.put_i32(0, motor_effort_pwm);
        f.put_i32(4, steer_angle_mdeg);
        f.put_i32(8, brake_pressure_kpa);
        f.put_u8(12, flags);
    }
};

struct SysToRtStatus {
    uint8_t mode = 0;                   // 0=Manual, 1=Auto, 2=Estop
    bool estop_active = false;
    bool heartbeat_ok = false;
    bool brake_engaged = false;
    int32_t actual_steer_angle_mdeg = 0;
    int32_t brake_pressure_kpa = 0;
    uint16_t syntree_fault_bits = 0;

    static SysToRtStatus from_frame(const Frame& f) {
        return {
            f.u8_at(0),
            f.u8_at(1) != 0,
            f.u8_at(2) != 0,
            f.u8_at(3) != 0,
            f.i32_at(4),
            f.i32_at(8),
            static_cast<uint16_t>(f.i16_at(12)),
        };
    }

    void to_frame(Frame& f) const {
        f.type = MessageType::SysToRtStatus;
        f.dlc = 14;
        f.put_u8(0, mode);
        f.put_u8(1, estop_active ? 1 : 0);
        f.put_u8(2, heartbeat_ok ? 1 : 0);
        f.put_u8(3, brake_engaged ? 1 : 0);
        f.put_i32(4, actual_steer_angle_mdeg);
        f.put_i32(8, brake_pressure_kpa);
        f.put_i16(12, static_cast<int16_t>(syntree_fault_bits));
    }
};

struct RtObstacleDistance {
    uint32_t distance_mm = 0;

    static RtObstacleDistance from_frame(const Frame& f) {
        return { static_cast<uint32_t>(f.i32_at(0)) };
    }

    void to_frame(Frame& f) const {
        f.type = MessageType::RtObstacleDist;
        f.dlc = 4;
        f.put_i32(0, static_cast<int32_t>(distance_mm));
    }
};

inline Frame heartbeat(MessageType type) {
    Frame f;
    f.type = type;
    f.dlc = 0;
    return f;
}

inline uint8_t crc8_update(uint8_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1) ^ 0x07u)
                            : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

inline uint8_t crc8(const uint8_t* data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        crc = crc8_update(crc, data[i]);
    }
    return crc;
}

}  // namespace inter_mcu
