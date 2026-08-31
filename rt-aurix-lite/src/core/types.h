#pragma once
// Typed domain inputs/outputs. Domain logic operates on these types only;
// it never sees CAN IDs, IPC, or wire encoding. Protocol adapters map
// CAN bytes <-> these types.

#include <cstdint>
#include <array>

namespace rta {

// ── Modes ─────────────────────────────────────────────────────────
enum class Mode : std::uint8_t {
    Manual = 0,
    Auto   = 1,
    Estop  = 2,
};

constexpr const char* mode_name(Mode m) noexcept {
    switch (m) {
        case Mode::Manual: return "MANUAL";
        case Mode::Auto:   return "AUTO";
        case Mode::Estop:  return "ESTOP";
    }
    return "?";
}

// ── Inputs ────────────────────────────────────────────────────────
struct DriveDemand {
    std::int32_t speed_mmps      = 0;  // [-500, 3000]
    std::int32_t yaw_rate_mrad_s = 0;  // [-3000, 3000]
    std::uint8_t gear            = 0;  // 0=N,1=D,2=S,3=R (0 = none)
};

struct MotorFeedback {
    std::int16_t actual_speed_mmps = 0;
    std::uint8_t gear_state        = 0;
    std::uint8_t fault_flags       = 0;  // shared::kMtrFault*
};

struct SteeringFeedback {
    bool        valid        = false;  // a 0x201 frame was received
    bool        angle_aligned= false;  // 0x201 byte0 bit0
    std::int16_t angle_0_1deg = 0;     // offset-free 0.1° units
    std::uint8_t error_status = 0;     // 0x201 byte0 bits 6-7
    std::uint8_t rolling_counter = 0;
};

struct BrakeFeedback {
    bool        valid        = false;  // a 0x721 frame was received
    bool        alignment    = false;  // 0x721 byte0 bit0
    std::uint16_t stroke_raw = 0;      // 0x721 bytes 2-3
    std::uint8_t error_status = 0;     // 0x721 byte0 bits 6-7
    std::uint8_t rolling_counter = 0;
};

struct ModeRequest {
    bool    valid = false;   // an HMI 0x111 request was received
    Mode    mode  = Mode::Manual;
};

// ── Outputs ───────────────────────────────────────────────────────
struct DriveCommand {           // 0x204 RTA_DRIVE_CMD
    std::int32_t motor_speed_mmps = 0;
    std::uint8_t gear             = 0;
};

struct SteeringCommand {         // 0x169 VCU_SES_REQ (typed values)
    bool        valid          = false;
    std::int16_t angle_0_1deg   = 0;  // offset-free 0.1° units
    std::uint8_t rolling_counter = 0;
    std::uint16_t speed_raw      = 0;  // slew-rate raw
    std::uint8_t vehicle_speed_raw = 0;
};

struct BrakeCommand {            // 0x7B9 VCU_SEB_REQ (typed values)
    bool        valid            = false;
    bool        stroke_mode      = true;   // true=Stroke, false=Pressure
    std::uint16_t stroke_raw     = 600;    // stroke mode request
    std::uint8_t pressure_raw    = 0;      // pressure mode request
    bool        auto_brake       = false;
    std::uint8_t rolling_counter = 0;
};

struct LightState {              // 0x011 / 0x302 bits
    bool left  = false;
    bool right = false;
    bool brake = false;
    bool head  = false;
};

}  // namespace rta
