#pragma once
// CAN protocol definitions.
// Bus 1 (public): Jetson <-> RT ESP32-S3.
// Bus 2 (private): SYS ESP32-S3 <-> Syntree EPS-C/SEB actuators.
// RT <-> SYS actuator setpoints use shared/intermcu, not CAN.

#include <cstdint>
#include "os/endian.h"

namespace can {

// ── Public CAN ID assignments: Jetson <-> RT ────────────────────
// Lower ID = higher arbitration priority on the bus.
// Safety-critical messages have the lowest IDs.

constexpr uint32_t kIdSysEstop         = 0x001;  // mirrored SYS emergency stop
constexpr uint32_t kIdRtEstop          = 0x002;  // RT emergency stop
constexpr uint32_t kIdHostEstop        = 0x003;  // Jetson emergency stop
constexpr uint32_t kIdSysSafetyStatus  = 0x011;  // RT mirrors SYS safety to Jetson
constexpr uint32_t kIdSysThrottlePos   = 0x120;  // RT mirrors SYS throttle to Jetson
constexpr uint32_t kIdRtStateReport    = 0x210;  // RT → Jetson
constexpr uint32_t kIdRtPidFeedback    = 0x220;  // RT → Jetson
constexpr uint32_t kIdHostDriveCmd     = 0x300;  // Jetson → RT: drive command
constexpr uint32_t kIdHostBrakeRequest = 0x301;  // Jetson → RT: brake pressure request
constexpr uint32_t kIdRtObstacleDist   = 0x400;  // RT → Jetson
constexpr uint32_t kIdSysDiag          = 0x600;  // RT mirrors SYS diagnostics to Jetson
constexpr uint32_t kIdHeartbeat        = 0x7FF;  // Jetson/RT public alive signal

// ── Private CAN ID assignments: SYS <-> Syntree actuators ───────
constexpr uint32_t kIdSyntreeEpsCommand = 0x169;  // SYS → EPS-C, 20 ms command
constexpr uint32_t kIdSyntreeEpsStatus  = 0x201;  // EPS-C → SYS, status feedback
constexpr uint32_t kIdSyntreeSebCommand = 0x7B0;  // SYS → SEB, 20 ms command
constexpr uint32_t kIdSyntreeSebStatus  = 0x721;  // SEB → SYS, status feedback

inline bool is_estop_id(uint32_t id) {
    return id == kIdSysEstop || id == kIdRtEstop || id == kIdHostEstop;
}

// ── CAN frame (hardware-independent) ────────────────────────────

struct Frame {
    uint32_t id       = 0;
    bool     extended = false;
    uint8_t  dlc      = 0;
    uint8_t  data[8]  = {};

    // Convenience: write int32_t at offset
    void put_i32(int offset, int32_t v) { os::write_be32(&data[offset], v); }
    void put_i16(int offset, int16_t v) { os::write_be16(&data[offset], v); }
    void put_u8(int offset, uint8_t v)  { data[offset] = v; }

    int32_t  i32_at(int offset) const { return os::read_be32(&data[offset]); }
    int16_t  i16_at(int offset) const { return os::read_be16(&data[offset]); }
    uint8_t  u8_at(int offset)  const { return data[offset]; }
};

// ── Frame payload types ─────────────────────────────────────────

// 0x300 HOST_DRIVE_CMD — Jetson → RT
struct HostDriveCmd {
    int32_t speed_mmps      = 0;   // linear.x  [mm/s]
    int32_t yaw_rate_mrad_s = 0;   // angular.z [millirad/s]

    static HostDriveCmd from_frame(const Frame& f) {
        return { f.i32_at(0), f.i32_at(4) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdHostDriveCmd; f.dlc = 8; f.extended = false;
        f.put_i32(0, speed_mmps); f.put_i32(4, yaw_rate_mrad_s);
    }
};

// 0x301 HOST_BRAKE_REQUEST — Jetson → RT
struct HostBrakeRequest {
    int32_t brake_pressure_kpa = 0;   // desired brake pressure [kPa]; 0 = release

    static HostBrakeRequest from_frame(const Frame& f) {
        return { f.i32_at(0) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdHostBrakeRequest; f.dlc = 4; f.extended = false;
        f.put_i32(0, brake_pressure_kpa);
    }
};

// 0x400 RT_OBSTACLE_DIST — RT → Jetson
struct RtObstacleDist {
    uint32_t distance_mm = 0;

    static RtObstacleDist from_frame(const Frame& f) {
        return { uint32_t(f.i32_at(0)) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdRtObstacleDist; f.dlc = 4; f.extended = false;
        f.put_i32(0, int32_t(distance_mm));
    }
};

// 0x011 SYS_SAFETY_STATUS — RT mirrors SYS status to Jetson
struct SysSafetyStatus {
    bool estop_active  = false;
    bool heartbeat_ok  = false;

    static SysSafetyStatus from_frame(const Frame& f) {
        return { f.u8_at(0) != 0, f.u8_at(1) != 0 };
    }
    void to_frame(Frame& f) const {
        f.id = kIdSysSafetyStatus; f.dlc = 2; f.extended = false;
        f.put_u8(0, estop_active ? 1 : 0);
        f.put_u8(1, heartbeat_ok  ? 1 : 0);
    }
};

// 0x169 EPS-C command on the private Syntree bus.
// Byte layout must be verified against the project-specific Syntree protocol
// before enabling transmission on hardware.
struct SyntreeEpsCommand {
    uint8_t raw[8] = {};

    void to_frame(Frame& f) const {
        f.id = kIdSyntreeEpsCommand; f.dlc = 8; f.extended = false;
        for (int i = 0; i < 8; ++i) f.data[i] = raw[i];
    }
};

// 0x7B0 SEB command on the private Syntree bus.
// Byte layout must be verified against the project-specific Syntree protocol
// before enabling transmission on hardware.
struct SyntreeSebCommand {
    uint8_t raw[8] = {};

    void to_frame(Frame& f) const {
        f.id = kIdSyntreeSebCommand; f.dlc = 8; f.extended = false;
        for (int i = 0; i < 8; ++i) f.data[i] = raw[i];
    }
};

// 0x120 SYS_THROTTLE_POS
struct SysThrottlePos {
    int16_t speed_mmps = 0;

    static SysThrottlePos from_frame(const Frame& f) {
        return { f.i16_at(0) };
    }
    void to_frame(Frame& f) const {
        f.id = kIdSysThrottlePos; f.dlc = 2; f.extended = false;
        f.put_i16(0, speed_mmps);
    }
};

// 0x210 RT_STATE_REPORT — RT → Jetson
struct RtStateReport {
    uint8_t mode       = 0;   // 0=Manual, 1=Auto, 2=Estop
    bool    steer_valid = false;
    bool    reversing   = false;

    void to_frame(Frame& f) const {
        f.id = kIdRtStateReport; f.dlc = 3; f.extended = false;
        f.put_u8(0, mode);
        f.put_u8(1, steer_valid ? 1 : 0);
        f.put_u8(2, reversing   ? 1 : 0);
    }
};

// 0x220 RT_PID_FEEDBACK — RT → Jetson
struct RtPidFeedback {
    int16_t speed_setpoint_mmps = 0;
    int16_t speed_measured_mmps = 0;
    int16_t pid_output          = 0;

    void to_frame(Frame& f) const {
        f.id = kIdRtPidFeedback; f.dlc = 6; f.extended = false;
        f.put_i16(0, speed_setpoint_mmps);
        f.put_i16(2, speed_measured_mmps);
        f.put_i16(4, pid_output);
    }
};

// 0x600 SYS_DIAG — SYS → Jetson
struct SysDiag {
    uint8_t  mode          = 0;
    bool     brake_engaged = false;
    bool     heartbeat_ok  = false;
    bool     estop_active  = false;
    uint16_t free_heap_kb  = 0;
    uint8_t  tec           = 0;   // TWAI transmit error counter
    uint8_t  rec           = 0;   // TWAI receive error counter

    void to_frame(Frame& f) const {
        f.id = kIdSysDiag; f.dlc = 8; f.extended = false;
        f.put_u8(0, mode);
        f.put_u8(1, brake_engaged ? 1 : 0);
        f.put_u8(2, heartbeat_ok  ? 1 : 0);
        f.put_u8(3, estop_active  ? 1 : 0);
        f.put_i16(4, int16_t(free_heap_kb));
        f.put_u8(6, tec);
        f.put_u8(7, rec);
    }
};

// ── Mode enum (shared) ──────────────────────────────────────────

enum class Mode : uint8_t {
    Manual = 0,
    Auto   = 1,
    Estop  = 2,
};

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Manual: return "MANUAL";
        case Mode::Auto:   return "AUTO";
        case Mode::Estop:  return "ESTOP";
    }
    return "?";
}

} // namespace can
