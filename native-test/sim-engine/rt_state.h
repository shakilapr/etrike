/**
 * Minimal rt_state.h for sim-engine-native.
 * Provides only the declarations needed for the physics model + safety monitor
 * to compile. Shadows the full rt_state.h in rt-esp32/src/.
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "can/can_protocol.h"
#include "physics_model.h"
#include "steering_control.h"

// ── FreeRTOS type stub ──
using QueueHandle_t = void*;

// ── Global objects ──
namespace rt {
struct Mcp2515Driver {};  // stub
struct SpeedController {}; // stub
struct DualHeartbeat {};   // stub
struct CmdWatchdog {};     // stub
}

extern rt::Mcp2515Driver g_can_high;
extern rt::PhysicsModel    g_physics;
extern rt::SpeedController g_speed_ctrl;
extern rt::SteeringControl g_steering;
extern rt::DualHeartbeat   g_heartbeat;
extern rt::CmdWatchdog     g_watchdog;

extern QueueHandle_t g_safety_evt_q;
extern QueueHandle_t g_can_rx_low_q;
extern QueueHandle_t g_can_rx_high_q;
extern QueueHandle_t g_cmd_q;
extern QueueHandle_t g_setpoint_q;
extern QueueHandle_t g_gw_tx_low_q;
extern QueueHandle_t g_gw_tx_high_q;

// ── Shared atomics ──
extern std::atomic<int32_t>  g_brake_request_kpa;
extern std::atomic<uint32_t> g_obstacle_mm;
extern std::atomic<int32_t>  g_ses_angle_0_1deg;
extern std::atomic<uint8_t>  g_ses_angle_status;
extern std::atomic<int32_t>  g_brake_kpa_to_send;
extern std::atomic<int32_t>  g_mtr_actual_speed_mmps;
extern std::atomic<uint8_t>  g_mode_current;
extern std::atomic<bool>     g_seb_takeover;
extern std::atomic<int64_t>  g_last_sys_hb_us;
extern std::atomic<int64_t>  g_last_host_hb_us;
extern std::atomic<int64_t>  g_last_estop_sent_us;
extern std::atomic<uint8_t>  g_estop_reason;
extern std::atomic<int16_t>  g_last_cmd_angle_0_1deg;
extern std::atomic<int16_t>  g_pid_output_mmps;
extern std::atomic<int32_t>  g_last_speed_setpoint_mmps;
extern std::atomic<bool>     g_reversing;
extern std::atomic<uint16_t> g_ses_motor_current;
extern std::atomic<uint16_t> g_ses_ecu_temp;
extern std::atomic<uint16_t> g_ses_pow_volt;
extern std::atomic<uint8_t>  g_ses_error_status;
extern std::atomic<uint16_t> g_seb_pressure_raw;
extern std::atomic<uint8_t>  g_seb_error_status;
extern std::atomic<uint16_t> g_seb_motor_current;
extern std::atomic<uint16_t> g_seb_ecu_temp_c;
