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

/* Node activity timestamps (microseconds) */
static int64_t s_last_rt_time = 0;
static int64_t s_last_sys_time = 0;
static int64_t s_last_host_time = 0;
static int64_t s_last_mtr_time = 0;
static int64_t s_last_seb_time = 0;
static int64_t s_last_ses_time = 0;

void can_decoder_init(void) {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    s_telemetry.gear = GEAR_N;
    s_telemetry.mode = VEHICLE_MODE_MANUAL;
    s_last_rate_calc_time = esp_timer_get_time();
    s_last_rt_time = 0;
    s_last_sys_time = 0;
    s_last_host_time = 0;
    s_last_mtr_time = 0;
    s_last_seb_time = 0;
    s_last_ses_time = 0;
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
        // 0x011: SYS_SAFETY_STS (Lights, Indicators, E-Stop, and node_presence bitmask from SYS)
        case 0x011:
            s_last_sys_time = now;
            if (dlc >= 1) {
                s_telemetry.estop_active = (d[0] != 0);
            }
            if (dlc >= 2) {
                // Canonical node_presence bitmask: bit0=RT, bit1=MTR, bit2=SEB, bit3=SES, bit4=HOST, bit5=SYS
                uint8_t pres = d[1];
                s_telemetry.node_presence_mask = pres;
                if (pres & (1u << 4)) s_last_host_time = now;
                if (pres & (1u << 0)) s_last_rt_time = now;
                if (pres & (1u << 1)) s_last_mtr_time = now;
                if (pres & (1u << 2)) s_last_seb_time = now;
                if (pres & (1u << 3)) s_last_ses_time = now;
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

        // 0x110: SYS_MODE_CMD (Authorized drive mode: 0: Manual, 1: Auto, 2: E-Stop)
        case 0x110:
            s_last_sys_time = now;
            if (dlc >= 1) {
                s_telemetry.mode = (d[0] == 1) ? VEHICLE_MODE_AUTO :
                                   (d[0] == 2) ? VEHICLE_MODE_ESTOP : VEHICLE_MODE_MANUAL;
            }
            break;

        // 0x120: SYS_THROTTLE_STS (SYS measured throttle demand)
        case 0x120:
            s_last_sys_time = now;
            if (dlc >= 4) {
                int32_t speed_mmps = (int32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                float speed_kmh = (float)speed_mmps * 0.0036f;
                s_telemetry.throttle_percent = (uint8_t)fminf(fmaxf((speed_kmh / 45.0f) * 100.0f, 0.0f), 100.0f);
            }
            break;

        // 0x122: RT_WHEEL_SPEED_STS (Actual ground speed measured by RT)
        case 0x122:
            s_last_rt_time = now;
            if (dlc >= 4) {
                int32_t speed_mmps = (int32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                s_telemetry.speed_act_kmh = (float)speed_mmps * 0.0036f;
                if (s_telemetry.speed_act_kmh < 0.0f) s_telemetry.speed_act_kmh = 0.0f;
            }
            break;

        // 0x169: VCU_SES_REQ (Steering command setpoint from VCU/RT)
        case 0x169:
            s_last_rt_time = now;
            if (dlc >= 2) {
                int16_t st_req_raw = (int16_t)((d[0] << 8) | d[1]);
                s_telemetry.steer_cmd_deg = (float)st_req_raw * 0.1f;
            }
            break;

        // 0x201: SES_STATUS (Actual Steering Actuator angle and diagnostics)
        case 0x201:
            s_last_ses_time = now;
            if (dlc >= 2) {
                int16_t st_act_raw = (int16_t)((d[0] << 8) | d[1]);
                s_telemetry.steer_act_deg = (float)st_act_raw * 0.1f;
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

        // 0x204: RT_DRIVE_CMD (Commanded speed and gear selection)
        case 0x204:
            s_last_rt_time = now;
            if (dlc >= 4) {
                int32_t cmd_speed_mmps = (int32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                s_telemetry.speed_cmd_kmh = (float)cmd_speed_mmps * 0.0036f;
            }
            if (dlc >= 8) {
                uint8_t g = d[7];
                s_telemetry.gear = (g == 1) ? GEAR_D : (g == 2) ? GEAR_S : (g == 3) ? GEAR_R : GEAR_N;
            }
            break;

        // 0x205: RT_BRAKE_CMD (Commanded brake target pressure)
        case 0x205:
            s_last_rt_time = now;
            if (dlc >= 4) {
                uint32_t p = (uint32_t)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]);
                s_telemetry.brake_cmd_kpa = (float)p;
            }
            break;

        // 0x206: MTR_MOTOR_FBK (Motor inverter RPM, feedback)
        case 0x206:
            s_last_mtr_time = now;
            if (dlc >= 2) {
                s_telemetry.motor_rpm = (uint16_t)((d[0] << 8) | d[1]);
            }
            break;

        // 0x210: RT_STATE_RPT (Mode & ESTOP reason from RT)
        case 0x210:
            s_last_rt_time = now;
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
            s_last_rt_time = now;
            if (dlc >= 3) {
                s_telemetry.bus_off = (d[0] & 0x01) != 0;
                s_telemetry.twai_tec = d[1];
                s_telemetry.twai_rec = d[2];
            }
            break;

        // 0x500: SYS_NODE_STATUS
        case 0x500:
            s_last_sys_time = now;
            break;

        // 0x501: RT_NODE_STATUS
        case 0x501:
            s_last_rt_time = now;
            if (dlc >= 5) {
                // Byte 4 contains node_presence as observed by RT
                uint8_t pres = d[4];
                s_telemetry.node_presence_mask = pres;
                if (pres & (1u << 4)) s_last_host_time = now;
                if (pres & (1u << 1)) s_last_mtr_time = now;
                if (pres & (1u << 2)) s_last_seb_time = now;
                if (pres & (1u << 3)) s_last_ses_time = now;
            }
            break;

        // 0x502: MTR_NODE_STATUS
        case 0x502:
            s_last_mtr_time = now;
            break;

        // 0x721: SEB_STATUS (Braking Actuator actual pressure and status)
        case 0x721:
            s_last_seb_time = now;
            if (dlc >= 4) {
                uint16_t p_raw = (uint16_t)((d[2] << 8) | d[3]);
                s_telemetry.brake_act_kpa = (float)p_raw * 10.0f;
                if (s_telemetry.brake_act_kpa > 1500.0f) s_telemetry.brake_act_kpa = 1500.0f;
            }
            if (dlc >= 5) {
                s_telemetry.brake_fault = (d[4] & 0x01) != 0;
            }
            if (dlc >= 6) {
                s_telemetry.brake_temp_c = (int16_t)d[5];
            }
            break;

        // 0x7FC: HOST_HEARTBEAT (Jetson / Navigation Host)
        case 0x7FC:
            s_last_host_time = now;
            break;

        // 0x7FD: RT_HEARTBEAT (Real-Time Controller)
        case 0x7FD:
            s_last_rt_time = now;
            break;

        // 0x7FE: SYS_HEARTBEAT (Safety Supervisor)
        case 0x7FE:
            s_last_sys_time = now;
            break;

        // 0x600: SYS_DIAG_RPT (Free heap, heartbeat, counters)
        case 0x600:
            s_last_sys_time = now;
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
        int64_t now = esp_timer_get_time();
        // Node presence freshness timeout (1000 ms window)
        const int64_t timeout_us = 1000000;
        s_telemetry.rt_connected   = (s_last_rt_time > 0)   && ((now - s_last_rt_time)   <= timeout_us);
        s_telemetry.sys_connected  = (s_last_sys_time > 0)  && ((now - s_last_sys_time)  <= timeout_us);
        s_telemetry.host_connected = (s_last_host_time > 0) && ((now - s_last_host_time) <= 1500000); // 1.5s for Host
        s_telemetry.mtr_connected  = (s_last_mtr_time > 0)  && ((now - s_last_mtr_time)  <= timeout_us);
        s_telemetry.seb_connected  = (s_last_seb_time > 0)  && ((now - s_last_seb_time)  <= timeout_us);
        s_telemetry.ses_connected  = (s_last_ses_time > 0)  && ((now - s_last_ses_time)  <= timeout_us);

        memcpy(dest, &s_telemetry, sizeof(vehicle_telemetry_t));
        xSemaphoreGive(s_mutex);
    }
}
