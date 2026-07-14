#pragma once
// Shared state — all cross-task atomics, queues, and global objects.
// Architecture principle #1: "Queues over shared state."
//
// Declarations live here (extern).  Definitions live in main.cpp (one place).
// Every task module includes this header to see the wiring.
//
// Sensor data uses atomics (latest-value semantics — 10ms staleness OK).
// Events use a bounded queue with atomic latest-state fallbacks on overflow.

#include <atomic>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "can/can_protocol.h"
#include "physics_model.h"
#include "steering_control.h"
#include "speed_controller.h"
#include "heartbeat.h"
#include "watchdog.h"
#include "can_driver_mcp2515.h"

// ── Global objects ──────────────────────────────────────────────────
extern rt::Mcp2515Driver g_can_high;
extern rt::PhysicsModel    g_physics;
extern rt::SpeedController g_speed_ctrl;
extern rt::SteeringControl g_steering;
extern rt::DualHeartbeat   g_heartbeat;
extern rt::CmdWatchdog     g_watchdog;

// ── Safety event queue (replaces g_estop_flag, g_mode_from_sys) ─
extern QueueHandle_t g_safety_evt_q;  // depth 16, SafetyEvent
extern std::atomic<bool>     g_pending_estop_event;
extern std::atomic<int16_t>  g_pending_mode_event;  // -1 when no fallback is pending
extern std::atomic<uint32_t> g_safety_event_drops;

// ── Shared state (atomics for sensor / latest-value data) ───────────
extern std::atomic<int32_t>  g_brake_request_kpa;
extern std::atomic<uint32_t> g_obstacle_mm;
extern std::atomic<int32_t>  g_ses_angle_0_1deg;
extern std::atomic<uint8_t>  g_ses_angle_status;
extern std::atomic<int32_t>  g_brake_kpa_to_send;
extern std::atomic<int32_t>  g_mtr_actual_speed_mmps;

// ── Derived state (written by control, read by tx tasks) ────────────
extern std::atomic<uint8_t>  g_mode_current;     // current mode (control publishes after event drain)
extern std::atomic<bool>     g_seb_takeover;     // SEB takeover active (control publishes after safety checks)

// ── Heartbeat tracking (written by dispatch, checked by control) ────
extern std::atomic<int64_t>  g_last_sys_hb_us;
extern std::atomic<int64_t>  g_last_host_hb_us;
extern std::atomic<int64_t>  g_last_estop_sent_us;  // 0x001 rate limiter

// ── ESTOP reason (written by dispatch/safety/health, read by tx) ───
extern std::atomic<uint8_t>  g_estop_reason;

// ── Telemetry atomics (written by control/dispatch, read by tx) ─────
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

// ── Queues ──────────────────────────────────────────────────────────
extern QueueHandle_t g_can_rx_low_q;   // 16 deep, can::Frame
extern QueueHandle_t g_can_rx_high_q;  // 16 deep
extern QueueHandle_t g_cmd_q;          // latest can::gen::HostDriveCmd (overwrite)
extern QueueHandle_t g_setpoint_q;     //  4 deep, rt::ResolvedSetpoint (overwrite)
extern QueueHandle_t g_gw_tx_low_q;    //  8 deep, can::Frame
extern QueueHandle_t g_gw_tx_high_q;   //  8 deep
