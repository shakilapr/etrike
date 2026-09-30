/**
 * @file can_decoder.h
 * @brief High-performance zero-copy CAN frame decoder for vehicle telemetry
 *
 * Direct bit-level C decoder matching tools/can_watcher protocol contracts.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/twai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VEHICLE_MODE_MANUAL = 0,
    VEHICLE_MODE_AUTO   = 1,
    VEHICLE_MODE_ESTOP  = 2,
} vehicle_drive_mode_t;

typedef enum {
    GEAR_P = 0,
    GEAR_R = 1,
    GEAR_N = 2,
    GEAR_D = 3,
    GEAR_S = 4,
} vehicle_gear_t;

typedef struct {
    bool left;
    bool right;
    bool hazard;
    bool brake;
    bool headlight;
} vehicle_lights_t;

/**
 * @brief Unified telemetry snapshot shared with LVGL UI
 */
typedef struct {
    // Drive & Safety Mode
    vehicle_drive_mode_t mode;
    bool estop_active;
    uint8_t estop_reason;

    // Transmission & Speed
    vehicle_gear_t gear;
    float speed_act_kmh;
    float speed_cmd_kmh;

    // Steering Dynamics (0x303, 0x310)
    float steer_act_deg;
    float steer_cmd_deg;
    float steer_current_a;
    int16_t steer_temp_c;
    bool steer_fault;

    // Braking Dynamics (0x301, 0x311)
    float brake_act_kpa;
    float brake_cmd_kpa;
    int16_t brake_temp_c;
    bool brake_fault;

    // Motion & Powertrain (0x121)
    int16_t yaw_rate_mrad_s;
    uint16_t motor_rpm;
    uint8_t throttle_percent;

    // Lighting (0x011, 0x302)
    vehicle_lights_t lights_act;
    vehicle_lights_t lights_cmd;

    // CAN Controller Health (0x211, TWAI Status)
    uint32_t total_frames;
    float bus_rate_hz;
    uint8_t twai_tec;
    uint8_t twai_rec;
    bool bus_off;

    // System Health (0x600)
    uint16_t sys_free_heap_kb;
    bool heartbeat_ok;
} vehicle_telemetry_t;

/**
 * @brief Initialize the CAN decoder subsystem and telemetry cache.
 */
void can_decoder_init(void);

/**
 * @brief Process an incoming TWAI frame and update vehicle telemetry.
 * @param frame Pointer to the raw TWAI frame.
 */
void can_decoder_process_frame(const twai_message_t *frame);

/**
 * @brief Get a thread-safe atomic copy of current vehicle telemetry.
 * @param dest Output buffer for telemetry snapshot.
 */
void can_decoder_get_telemetry(vehicle_telemetry_t *dest);

#ifdef __cplusplus
}
#endif
