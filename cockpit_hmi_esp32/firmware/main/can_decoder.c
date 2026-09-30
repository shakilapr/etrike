/**
 * @file can_decoder.c
 * @brief High-performance zero-copy CAN frame decoder implementation
 */

#include "can_decoder.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static vehicle_telemetry_t s_telemetry;
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t s_frame_counter = 0;
static int64_t s_last_rate_calc_time = 0;
static uint32_t s_frames_in_window = 0;

void can_decoder_init(void) {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    s_telemetry.gear = GEAR_N;
    s_telemetry.mode = VEHICLE_MODE_MANUAL;
    s_last_rate_calc_time = esp_timer_get_time();
}

void can_decoder_process_frame(const twai_message_t *frame) {
    if (!frame) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    s_frame_counter++;
    s_frames_in_window++;
    s_telemetry.total_frames = s_frame_counter;

    // Calculate update rate every 1 second
    int64_t now = esp_timer_get_time();
    if ((now - s_last_rate_calc_time) >= 1000000) {
        s_telemetry.bus_rate_hz = (float)s_frames_in_window / ((now - s_last_rate_calc_time) / 1000000.0f);
        s_frames_in_window = 0;
        s_last_rate_calc_time = now;
    }

    const uint8_t *d = frame->data;
    uint8_t dlc = frame->data_length_code;
    uint32_t id = frame->identifier;

    switch (id) {
        // 0x011: SYS_SAFETY_STS
        case 0x011:
            if (dlc >= 1) {
                s_telemetry.estop_active = (d[0] != 0);
            }
            if (dlc >= 3) {
                uint8_t b = d[2];
                s_telemetry.lights_act.left = (b & 0x01) != 0;
                s_telemetry.lights_act.right = (b & 0x02) != 0;
                s_telemetry.lights_act.hazard = ((b & 0x03) == 0x03);
                s_telemetry.lights_act.brake = (b & 0x04) != 0;
                s_telemetry.lights_act.headlight = (b & 0x08) != 0;
            }
            break;

        // 0x121: RT_MOTION_RPT (Speed, Gear, Yaw Rate)
        case 0x121:
            if (dlc >= 4) {
                // speed_mmps (int32 big endian)
                int32_t speed_mmps = (int32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                s_telemetry.speed_act_kmh = (float)speed_mmps * 0.0036f;
                if (s_telemetry.speed_act_kmh < 0.0f) s_telemetry.speed_act_kmh = 0.0f;
                s_telemetry.motor_rpm = (uint16_t)(s_telemetry.speed_act_kmh * 110.0f);
                s_telemetry.throttle_percent = (uint8_t)fminf((s_telemetry.speed_act_kmh / 45.0f) * 100.0f, 100.0f);
            }
            if (dlc >= 6) {
                // yaw_rate_mrad_s (int16 big endian)
                s_telemetry.yaw_rate_mrad_s = (int16_t)((d[4] << 8) | d[5]);
            }
            if (dlc >= 7) {
                uint8_t g = d[6];
                s_telemetry.gear = (g == 1) ? GEAR_D : (g == 2) ? GEAR_S : (g == 3) ? GEAR_R : GEAR_N;
            }
            break;

        // 0x210: RT_STATE_RPT (Mode & ESTOP reason)
        case 0x210:
            if (dlc >= 1) {
                s_telemetry.mode = (d[0] == 1) ? VEHICLE_MODE_AUTO :
                                   (d[0] == 2) ? VEHICLE_MODE_ESTOP : VEHICLE_MODE_MANUAL;
            }
            if (dlc >= 2) {
                s_telemetry.estop_reason = (d[1] >> 4) & 0x0F;
            }
            break;

        // 0x211: RT_DIAG_RPT (CAN Bus Health)
        case 0x211:
            if (dlc >= 3) {
                s_telemetry.bus_off = (d[0] & 0x01) != 0;
                s_telemetry.twai_tec = d[1];
                s_telemetry.twai_rec = d[2];
            }
            break;

        // 0x300: HOST_DRIVE_CMD (Commanded Speed & Gear)
        case 0x300:
            if (dlc >= 4) {
                int32_t cmd_speed_mmps = (int32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                s_telemetry.speed_cmd_kmh = (float)cmd_speed_mmps * 0.0036f;
            }
            if (dlc >= 8) {
                uint8_t g = d[7];
                s_telemetry.gear = (g == 1) ? GEAR_D : (g == 2) ? GEAR_S : (g == 3) ? GEAR_R : GEAR_N;
            }
            break;

        // 0x301: HOST_BRAKE_REQ (Commanded brake pressure)
        case 0x301:
            if (dlc >= 4) {
                uint32_t p = (uint32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                s_telemetry.brake_cmd_kpa = (float)p;
            }
            break;

        // 0x302: HOST_LIGHT_CMD (Commanded lights)
        case 0x302:
            if (dlc >= 1) {
                uint8_t b = d[0];
                s_telemetry.lights_cmd.left = (b & 0x01) != 0;
                s_telemetry.lights_cmd.right = (b & 0x02) != 0;
                s_telemetry.lights_cmd.hazard = ((b & 0x03) == 0x03);
                s_telemetry.lights_cmd.brake = (b & 0x04) != 0;
                s_telemetry.lights_cmd.headlight = (b & 0x08) != 0;
            }
            break;

        // 0x303: HOST_STEER_CMD (Commanded steering angle)
        case 0x303:
            if (dlc >= 2) {
                int16_t st_raw = (int16_t)((d[0] << 8) | d[1]);
                s_telemetry.steer_cmd_deg = (float)st_raw * 0.1f;
            }
            break;

        // 0x310: STEER_DIAG (Feedback angle, current, temp, fault)
        case 0x310:
            if (dlc >= 2) {
                uint16_t raw_ang = (uint16_t)((d[0] << 8) | d[1]);
                s_telemetry.steer_act_deg = ((float)raw_ang - 3000.0f) * 0.1f;
            }
            if (dlc >= 3) {
                s_telemetry.steer_fault = (d[2] & 0x01) != 0;
            }
            if (dlc >= 4) {
                s_telemetry.steer_current_a = (float)d[3] * 0.1f;
            }
            if (dlc >= 5) {
                s_telemetry.steer_temp_c = (int16_t)d[4];
            }
            break;

        // 0x311: BRAKE_DIAG (Feedback pressure, temp, fault)
        case 0x311:
            if (dlc >= 2) {
                uint16_t p_raw = (uint16_t)((d[0] << 8) | d[1]);
                s_telemetry.brake_act_kpa = (float)p_raw * 50.0f;
            }
            if (dlc >= 3) {
                s_telemetry.brake_fault = (d[2] & 0x01) != 0;
            }
            if (dlc >= 4) {
                s_telemetry.brake_temp_c = (int16_t)d[3];
            }
            break;

        // 0x600: SYS_DIAG_RPT (Free heap, heartbeat, counters)
        case 0x600:
            if (dlc >= 6) {
                s_telemetry.heartbeat_ok = (d[2] & 0x01) != 0;
                s_telemetry.sys_free_heap_kb = (uint16_t)((d[4] << 8) | d[5]);
            }
            if (dlc >= 8) {
                s_telemetry.twai_tec = d[6];
                s_telemetry.twai_rec = d[7];
            }
            break;

        default:
            break;
    }

    xSemaphoreGive(s_mutex);
}

void can_decoder_get_telemetry(vehicle_telemetry_t *dest) {
    if (!dest) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(dest, &s_telemetry, sizeof(vehicle_telemetry_t));
        xSemaphoreGive(s_mutex);
    }
}
