#pragma once
// Shared state — snapshot mailboxes, gateway queues, and subsystem singletons.
// Follows update.md Section 3: Latest-value mailbox snapshots + depth-8 gateway queue.

#include <atomic>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "protocol/compat/can.hpp"
#include "physics_model.h"
#include "resolver_config.h"   // rt::ActiveResolver
#include "calculated_speed.h"  // rt::CalculatedSpeedEstimator
#include "steering_control.h"
#include "speed_controller.h"
#include "heartbeat.h"
#include "watchdog.h"
#include "can_driver_mcp2515.h"
#include "safety_stream_loss.h"

namespace rt {

// ── Snapshot Mailbox Definitions (update.md §3.1) ─────────────────

// 1. Host Drive Command Snapshot (High -> Control)
struct HostDriveSnapshot {
    int32_t speed_mmps;              // Command speed [-500, 3000]
    int32_t yaw_rate_mrad_s;         // Command yaw rate [-3000, 3000]
    int32_t direct_steer_0_1deg;     // Command direct steer angle (0.1 deg)
    int64_t timestamp_us;            // Monotonic arrival timestamp (esp_timer_get_time)
    uint32_t obstacle_distance_mm;   // Closest obstacle distance
    uint8_t gear_override;           // 0=None, 1=D, 2=S, 3=R
    bool direct_steer_valid;         // 0x303 validity flag
    bool drive_cmd_valid;            // 0x300 validity flag
};

// 2. Actuator & Bus Feedback Snapshot (Low -> Control)
struct ActuatorFeedbackSnapshot {
    int32_t mtr_command_speed_mmps;  // Echoed speed from 0x206
    uint8_t mtr_gear_state;          // Gear state from 0x206
    int16_t ses_angle_0_1deg;        // Measured steering angle from 0x201
    uint8_t ses_angle_status;        // 0=centering, 1=aligned
    uint8_t ses_error_status;        // L3 error byte
    uint16_t seb_pressure_raw;       // Brake pressure raw from 0x721
    uint8_t seb_error_status;        // SEB error status
    uint16_t ses_motor_current;      // from 0x6FA
    uint16_t ses_ecu_temp;          // from 0x6FA
    uint16_t ses_pow_volt;          // from 0x6FA
    uint16_t seb_motor_current;      // from 0x6FB
    uint16_t seb_ecu_temp_c;        // from 0x6FB
    int64_t last_mtr_us;             // 0x206 arrival timestamp
    int64_t last_ses_us;             // 0x201 arrival timestamp
    int64_t last_sys_hb_us;          // 0x7FE arrival timestamp
    int64_t last_sys_safety_sts_us;  // 0x011 arrival timestamp
    int64_t last_0x7b9_rx_us;        // Observed 0x7B9 arrival timestamp
    uint8_t sys_mode;                // Mode from 0x110
    bool sys_mode_valid;             // 0x110 rolling-counter validity
};

// 3. Motion Output Snapshot (Control -> Low/High CAN)
struct MotionOutputSnapshot {
    int32_t motor_speed_mmps;        // Commanded motor setpoint for 0x204
    uint8_t motor_gear;              // Gear command for 0x204
    int16_t steer_angle_0_1deg;      // Commanded steer angle for 0x169
    uint16_t steer_slew_rate_deg_s;  // Dynamic steer slew rate for 0x169
    int32_t brake_kpa;               // Commanded brake pressure for 0x205
    uint8_t current_mode;            // 0=Manual, 1=Auto, 2=Estop
    uint8_t estop_reason;            // Active ESTOP reason code
    uint8_t safety_state;            // 0=Normal, 1=InternalEstop, 2=Fault
    bool seb_emergency_takeover;     // True if RT owns 0x7B9 emergency transmission
    bool steer_command_enable;       // True if 0x169 should transmit
    bool reversing;                  // Vehicle reversing state
};

// 4. Bounded Gateway Frame (update.md §3.2)
struct GatewayFrame {
    can::Frame frame;
    int64_t enqueued_us;
};

// Multi-stream authority readiness barrier
extern std::atomic<uint8_t> g_ready_mask;
extern std::atomic<bool>    g_no_sys_authority;

}  // namespace rt

// Bring into global namespace for compatibility with existing modules
using rt::g_ready_mask;
using rt::g_no_sys_authority;

// ── Control/sensor atomics consumed by safety_monitor.h ─────────────
// (defined in main.cpp; declared here so the header is self-contained)
extern std::atomic<int64_t> g_last_mtr_feedback_us;
extern std::atomic<int32_t> g_brake_request_kpa;
extern std::atomic<int16_t> g_last_cmd_angle_0_1deg;
extern std::atomic<int32_t> g_ses_angle_0_1deg;
extern std::atomic<int32_t> g_mtr_motor_command_speed_mmps;
extern std::atomic<int64_t> g_last_0x7B9_rx_us;

// ── Global objects ──────────────────────────────────────────────────
extern rt::Mcp2515Driver             g_can_high;
extern rt::ActiveResolver            g_resolver;
extern rt::SpeedController           g_speed_ctrl;
extern rt::CalculatedSpeedEstimator  g_calc_speed;
extern rt::SteeringControl           g_steering;
extern rt::DualHeartbeat             g_heartbeat;
extern rt::CmdWatchdog               g_watchdog;

// ── Safety event signals ────────────────────────────────────────────
extern QueueHandle_t         g_safety_evt_q;
extern std::atomic<bool>     g_pending_estop_event;
extern std::atomic<int16_t>  g_pending_mode_event;
extern std::atomic<bool>     g_pending_safety_clear;
extern std::atomic<uint32_t> g_safety_event_drops;
extern std::atomic<bool>     g_steering_estop_request;
extern std::atomic<bool>     g_steering_exit_request;
extern std::atomic<bool>     g_sys_clear_in_progress;

// ── Mailboxes & Queues (3-Task Architecture) ────────────────────────
extern QueueHandle_t g_host_cmd_mailbox;       // depth 1, rt::HostDriveSnapshot (overwrite)
extern QueueHandle_t g_feedback_mailbox;       // depth 1, rt::ActuatorFeedbackSnapshot (overwrite)
extern QueueHandle_t g_motion_output_mailbox;   // depth 1, rt::MotionOutputSnapshot (overwrite)
extern QueueHandle_t g_high_to_low_gw_q;       // depth 8, rt::GatewayFrame

// ── Telemetry & Diagnostic state ────────────────────────────────────
extern std::atomic<uint8_t>  g_mode_current;
extern std::atomic<uint8_t>  g_estop_reason;
extern std::atomic<bool>     g_seb_takeover;
extern std::atomic<bool>     g_mtr_unavailable;
extern std::atomic<uint8_t>  g_brake_fallback_state;
extern std::atomic<int32_t>  g_last_speed_setpoint_mmps;
extern std::atomic<int16_t>  g_pid_output_mmps;
extern std::atomic<int32_t>  g_encoder_speed_mmps;
extern std::atomic<int64_t>  g_last_sys_hb_us;
extern std::atomic<int64_t>  g_last_host_hb_us;
extern std::atomic<int64_t>  g_last_low_peer_us;
extern std::atomic<int64_t>  g_last_high_peer_us;
extern std::atomic<int64_t>  g_last_sys_safety_sts_us;
extern std::atomic<int64_t>  g_last_estop_sent_us;
extern std::atomic<int64_t>  g_last_nonzero_cmd_us;
extern std::atomic<uint8_t>  g_heartbeat_flags;
extern std::atomic<int64_t>  g_task_alive_high_us;
extern std::atomic<int64_t>  g_task_alive_low_us;
extern std::atomic<int64_t>  g_task_alive_control_us;
