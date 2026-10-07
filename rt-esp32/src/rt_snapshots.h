#pragma once

#include <cstdint>

namespace rt {

// 1. Host Drive Command Snapshot (High -> Control)
struct HostDriveSnapshot {
    int32_t speed_mmps{0};              // Command speed [-500, 3000]
    int32_t yaw_rate_mrad_s{0};         // Command yaw rate [-3000, 3000]
    int32_t direct_steer_0_1deg{0};     // Command direct steer angle (0.1 deg)
    int64_t timestamp_us{0};            // Monotonic arrival timestamp (esp_timer_get_time)
    uint32_t obstacle_distance_mm{0};   // Closest obstacle distance
    uint8_t gear_override{0};           // 0=None, 1=D, 2=S, 3=R
    bool direct_steer_valid{false};     // 0x303 validity flag
    bool drive_cmd_valid{false};        // 0x300 validity flag
};

// 2. Motion Output Snapshot (Control -> Low/High CAN)
struct MotionOutputSnapshot {
    int32_t motor_speed_mmps{0};        // Commanded motor setpoint for 0x204
    uint8_t motor_gear{0};              // Gear command for 0x204
    int16_t steer_angle_0_1deg{0};      // Commanded steer angle for 0x169
    uint16_t steer_slew_rate_deg_s{0};  // Dynamic steer slew rate for 0x169
    int32_t brake_kpa{0};               // Commanded brake pressure for 0x205
    uint8_t current_mode{0};            // Resolved mode
    uint8_t estop_reason{0};            // Latched ESTOP reason
    uint8_t safety_state{0};            // 0=Active, 1=Degraded, 2=Fault
    bool seb_emergency_takeover{false}; // Fallback SEB command active
    bool steer_command_enable{false};   // 0x169 transmission enable
    bool reversing{false};              // Reverse motion active
};

} // namespace rt
