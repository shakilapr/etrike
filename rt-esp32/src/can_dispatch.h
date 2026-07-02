#pragma once
// CAN dispatch — routes incoming CAN frames to the correct consumer.
//
// process_frame() classifies each frame, updates sensor atomics, and
// enqueues safety events (ESTOP, MODE_CHANGE) via g_safety_evt_q.
// t_dispatch() is the FreeRTOS task (prio 4) that reads both CAN RX
// queues and drives the pipeline.
//
// Architecture §2.3: gateway forwarding categories (transparent,
// consumed→regenerated, bus-local).

#include <cstdint>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rt_state.h"
#include "safety_monitor.h"
#include "can_rx_router.h"
#include "watchdog.h"

static const char* TAG_DISP = "rt-dispatch";

// ── Per-frame dispatch context ──────────────────────────────────────

struct DispatchContext {
    can::Frame        gw_lo;
    can::Frame        gw_hi;
    can::HostDriveCmd cmd;
    int32_t           brake_req_kpa = 0;
    bool              estop_flag    = false;
    uint8_t           mode_from_sys = 0;
    uint16_t          steer_feedback_angle = 0;
    uint8_t           steer_angle_status   = 0;
    bool              has_mode  = false;
    bool              has_brake = false;
    bool              has_cmd   = false;
};

// ── Frame processor ─────────────────────────────────────────────────

static void process_frame(const can::Frame& fr, bool from_high, DispatchContext& ctx) {
    // Frozen counter detection: skip timestamp update if alive counter
    // hasn't changed (prevents stuck CAN controller from masking a hung peer).
    if (fr.id == can::kIdSysHeartbeat && !from_high) {
        static uint8_t last_sys_ctr = 0;
        static bool sys_first = true;
        if (sys_first || fr.data[0] != last_sys_ctr) {
            sys_first = false;
            last_sys_ctr = fr.data[0];
            g_last_sys_hb_us.store(esp_timer_get_time());
        }
    } else if (fr.id == can::kIdHostHeartbeat && from_high) {
        static uint8_t last_host_ctr = 0;
        static bool host_first = true;
        if (host_first || fr.data[0] != last_host_ctr) {
            host_first = false;
            last_host_ctr = fr.data[0];
            g_last_host_hb_us.store(esp_timer_get_time());
        }
    }

    rt::GatewayQueues q;
    q.gw_tx_low  = &ctx.gw_lo;
    q.gw_tx_high = &ctx.gw_hi;
    q.cmd = &ctx.cmd;
    q.brake_req_kpa = &ctx.brake_req_kpa;
    q.estop_flag = &ctx.estop_flag;
    q.mode_from_sys = &ctx.mode_from_sys;
    q.steer_feedback_angle = &ctx.steer_feedback_angle;
    q.steer_angle_status   = &ctx.steer_angle_status;
    rt::route_frame(fr, from_high, q);

    // ── Post-routing handlers ───────────────────────────────────────
    if (fr.id == can::kIdSafetyEstop) {
        ctx.gw_lo = fr;
        ctx.gw_hi = fr;
        // Enqueue ESTOP event with 10ms timeout (blocking — safety critical)
        rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, 0};
        if (xQueueSend(g_safety_evt_q, &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
            // Queue full — overwrite latest ESTOP to avoid loss
            xQueueOverwrite(g_safety_evt_q, &evt);
        }
    }
    if (fr.id == can::kIdSbwStatus) {
        // steer-by-wire checksum: XOR(bytes 0-6) ^ 0xFF must equal byte 7
        uint8_t cksum = 0;
        for (int i = 0; i < 7 && i < fr.dlc; ++i) cksum ^= fr.data[i];
        if (fr.dlc >= 8 && (cksum ^ 0xFF) == fr.data[7]) {
            g_ses_angle_0_1deg.store(ctx.steer_feedback_angle - rt::kSbwAngleOffset);
            g_ses_angle_status.store(ctx.steer_angle_status);
            g_ses_error_status.store((fr.data[0] >> 6) & 0x03);
        }
    }
    if (fr.id == can::kIdHostObstacleDist && from_high) { g_obstacle_mm.store(fr.u32_at(0)); }
    if (fr.id == can::kIdMtrMotorFbk && !from_high) { g_mtr_actual_speed_mmps.store(fr.i16_at(0)); }

    // 0x202 SES_ErrInfo — L3 fault bits → ESTOP (arch §7.3)
    if (fr.id == can::kIdSbwErrInfo && !from_high) {
        uint8_t angle_faults  = fr.data[1] & 0x0F;
        uint8_t torque_faults = (fr.data[2] >> 2) & 0x0F;
        if (angle_faults || torque_faults) {
            ESP_LOGW(TAG_DISP, "SES_ErrInfo L3 fault: angle=0x%X torque=0x%X", angle_faults, torque_faults);
            rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, 0};
            xQueueSend(g_safety_evt_q, &evt, 0);
        }
    }
    // 0x203 SES_Version — log SW/HW once (arch §7.3)
    if (fr.id == can::kIdSbwVersion && !from_high) {
        static bool ses_version_logged = false;
        if (!ses_version_logged) {
            ESP_LOGI(TAG_DISP, "SES_Version: SW=%02X.%02X HW=%02X.%02X",
                     fr.data[0], fr.data[1], fr.data[2], fr.data[3]);
            ses_version_logged = true;
        }
    }
    // 0x6FA SES_Test — motor current + ECU temp + supply voltage
    if (fr.id == can::kIdSbwTest && !from_high) {
        int16_t  mc_raw = int16_t((uint16_t(fr.data[2]) << 8) | fr.data[1]);
        uint16_t et_raw = (uint16_t(fr.data[4]) << 8) | fr.data[3];
        uint16_t pv_raw = (uint16_t(fr.data[6]) << 8) | fr.data[5];
        g_ses_motor_current.store(mc_raw);
        g_ses_ecu_temp.store(et_raw);
        g_ses_pow_volt.store(pv_raw);
        float mc_a = mc_raw * 0.0078125f;
        float et_c = et_raw * 0.5f;
        float pv_v = pv_raw * 0.00390625f;
        if (et_c > 85.0f)  ESP_LOGW(TAG_DISP, "SES ECU temp high: %.1f°C", et_c);
        if (mc_a > 30.0f)  ESP_LOGW(TAG_DISP, "SES motor current high: %.1f A", mc_a);
        if (pv_v < 10.0f)  ESP_LOGW(TAG_DISP, "SES supply voltage low: %.2f V", pv_v);
    }
    // 0x6FB SEB_Test — motor current + ECU temp (for 0x311 BRAKE_DIAG)
    if (fr.id == can::kIdBbwTest && !from_high) {
        int16_t  mc_raw = int16_t((uint16_t(fr.data[2]) << 8) | fr.data[1]);
        uint16_t et_raw = (uint16_t(fr.data[4]) << 8) | fr.data[3];
        g_seb_motor_current.store(mc_raw);
        g_seb_ecu_temp_c.store(et_raw);
    }
    // 0x721 SEB_STATUS — capture pressure + error for 0x311 BRAKE_DIAG
    if (fr.id == can::kIdBbwStatus && !from_high) {
        // Validate checksum before using data (matching SYS pattern)
        uint8_t cksum = 0;
        for (int i = 0; i < 7 && i < fr.dlc; ++i) cksum ^= fr.data[i];
        if (fr.dlc >= 8 && (cksum ^ 0xFF) != fr.data[7]) return;  // drop corrupt frame

        // Byte 3 is pressure ONLY in Pressure mode (control_mode=1).
        // In Stroke mode it's Stroke[15:8] — not pressure data.
        uint8_t seb_mode = (fr.data[0] >> 2) & 1;  // 0=Stroke, 1=Pressure
        g_seb_pressure_raw.store(seb_mode == 1 ? fr.data[3] : 0);
        g_seb_error_status.store((fr.data[0] >> 6) & 0x03);
    }
    // Track reception flags (fix #3: 0=Manual/0=release are valid values)
    if (fr.id == can::kIdSysModeCmd && !from_high)   { ctx.has_mode = true; }
    if (fr.id == can::kIdHostBrakeReq && from_high)  { ctx.has_brake = true; }
    if (fr.id == can::kIdHostDriveCmd && from_high)  { ctx.has_cmd = true; }
}

// ── Dispatch task (prio 4) ──────────────────────────────────────────

[[noreturn]] static void t_dispatch(void*) {
    can::Frame fr;
    while (1) {
        g_alive_dispatch.store(xTaskGetTickCount(), std::memory_order_relaxed);
        bool from_high = false;
        if (xQueueReceive(g_can_rx_low_q, &fr, 0) == pdTRUE) {
            from_high = false;
        } else if (xQueueReceive(g_can_rx_high_q, &fr, 0) == pdTRUE) {
            from_high = true;
        } else {
            // Both queues empty — block on low with short timeout to avoid
            // starving the high bus (portMAX_DELAY blocks forever on low).
            if (xQueueReceive(g_can_rx_low_q, &fr, pdMS_TO_TICKS(10)) == pdTRUE) {
                from_high = false;
            } else if (xQueueReceive(g_can_rx_high_q, &fr, pdMS_TO_TICKS(10)) == pdTRUE) {
                from_high = true;
            } else {
                continue;
            }
        }

        DispatchContext ctx{};
        process_frame(fr, from_high, ctx);

        // Gateway forwarding — ESTOP (0x001) skips to front of queue
        // but rate-limited to max 1 per 100ms per bus to prevent a faulty
        // node from starving all other gateway traffic (bug 4.5).
        if (ctx.gw_lo.id) {
            bool is_estop = (ctx.gw_lo.id == can::kIdSafetyEstop);
            if (is_estop) {
                static int64_t last_estop_fwd_lo_us = 0;
                int64_t now_us = esp_timer_get_time();
                if (now_us - last_estop_fwd_lo_us < 100000) {
                    ctx.gw_lo.id = 0;  // suppress — rate limited
                } else {
                    last_estop_fwd_lo_us = now_us;
                }
            }
            if (ctx.gw_lo.id) {
                if (!(is_estop ? xQueueSendToFront(g_gw_tx_low_q, &ctx.gw_lo, 0)
                              : xQueueSend(g_gw_tx_low_q, &ctx.gw_lo, 0))) {
                    static uint32_t gw_lo_drops = 0; gw_lo_drops++;
                }
            }
        }
        if (ctx.gw_hi.id) {
            bool is_estop = (ctx.gw_hi.id == can::kIdSafetyEstop);
            if (is_estop) {
                static int64_t last_estop_fwd_hi_us = 0;
                int64_t now_us = esp_timer_get_time();
                if (now_us - last_estop_fwd_hi_us < 100000) {
                    ctx.gw_hi.id = 0;  // suppress — rate limited
                } else {
                    last_estop_fwd_hi_us = now_us;
                }
            }
            if (ctx.gw_hi.id) {
                if (!(is_estop ? xQueueSendToFront(g_gw_tx_high_q, &ctx.gw_hi, 0)
                              : xQueueSend(g_gw_tx_high_q, &ctx.gw_hi, 0))) {
                    static uint32_t gw_hi_drops = 0; gw_hi_drops++;
                }
            }
        }

        // Mode change → safety event queue (guaranteed delivery)
        if (ctx.has_mode) {
            rt::SafetyEvent evt{rt::SafetyEvent::MODE_CHANGE, ctx.mode_from_sys};
            xQueueSend(g_safety_evt_q, &evt, 0);
            // Also clear ESTOP if exiting ESTOP mode
            if (ctx.mode_from_sys != uint8_t(can::Mode::Estop)) {
                g_steering.exit_estop();
            }
        }

        // Brake request → atomic (latest-value OK — max-select in control)
        if (ctx.has_brake)   g_brake_request_kpa.store(ctx.brake_req_kpa);

        // Drive command → queue (already queue-based via g_cmd_q)
        if (ctx.has_cmd) {
            xQueueOverwrite(g_cmd_q, &ctx.cmd);
            g_watchdog.feed(esp_timer_get_time());
            g_steering.exit_estop();
        }
    }
}
