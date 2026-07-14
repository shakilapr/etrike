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

inline bool enqueue_safety_event(const rt::SafetyEvent& evt, TickType_t timeout) {
    if (xQueueSend(g_safety_evt_q, &evt, timeout) == pdTRUE) return true;

    g_safety_event_drops.fetch_add(1, std::memory_order_relaxed);
    if (evt.type == rt::SafetyEvent::ESTOP) {
        g_pending_estop_event.store(true, std::memory_order_release);
    } else {
        g_pending_mode_event.store(evt.payload, std::memory_order_release);
    }
    return false;
}

// ── Per-frame dispatch context ──────────────────────────────────────

struct DispatchContext {
    can::Frame        gw_lo;
    can::Frame        gw_hi;
    can::gen::HostDriveCmd cmd;
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
    // Frozen counter detection: use delta comparison to handle 8-bit
    // rollover correctly. Equality check (new != old) false-positives
    // when counter wraps from 0xFF back to a previously-seen value (bug M8).
    if (fr.id == can::kIdSysHeartbeat && !from_high) {
        can::gen::SysHeartbeat heartbeat{};
        if (can::decode_frame(fr, heartbeat) != can::gen::CodecStatus::Ok) return;
        static uint8_t last_sys_ctr = 0;
        static bool sys_first = true;
        uint8_t delta = heartbeat.alive_ctr - last_sys_ctr;
        if (sys_first || delta != 0) {
            sys_first = false;
            last_sys_ctr = heartbeat.alive_ctr;
            g_last_sys_hb_us.store(esp_timer_get_time());
        }
    } else if (fr.id == can::kIdHostHeartbeat && from_high) {
        can::gen::HostHeartbeat hb{};
        if (can::decode_frame(fr, hb) != can::gen::CodecStatus::Ok) return;
        static uint8_t last_host_ctr = 0;
        static bool host_first = true;
        uint8_t delta = hb.alive_ctr - last_host_ctr;
        if (host_first || delta != 0) {
            host_first = false;
            last_host_ctr = hb.alive_ctr;
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
    const auto route_status = rt::route_frame(fr, from_high, q);
    if (route_status != can::gen::CodecStatus::Ok) return;

    // ── Post-routing handlers ───────────────────────────────────────
    if (fr.id == can::kIdSafetyEstop) {
        ctx.gw_lo = fr;
        ctx.gw_hi = fr;
        g_estop_reason.store(rt::kEstopReasonCanEstop);
        // Enqueue ESTOP event with 10ms timeout (blocking — safety critical)
        rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, 0};
        enqueue_safety_event(evt, pdMS_TO_TICKS(10));
    }
    if (fr.id == can::kIdSbwStatus) {
        can::custom::ses::Status value{};
        if (can::custom::ses::decode_status(fr.view(), value) == can::gen::CodecStatus::Ok) {
            // Frozen rolling counter guard (D4): skip update if EPS-C counter
            // hasn't incremented, preventing stuck-CAN from masking actuator fault.
            static uint8_t last_eps_roll = 0;
            static bool eps_first = true;
            uint8_t roll = value.rolling_counter;
            uint8_t delta = roll - last_eps_roll;
            if (eps_first || delta != 0) {
                eps_first = false;
                last_eps_roll = roll;
                g_ses_angle_0_1deg.store(ctx.steer_feedback_angle - rt::kSbwAngleOffset);
                g_ses_angle_status.store(ctx.steer_angle_status);
            }
            g_ses_error_status.store(value.error_status);
        }
    }
    if (fr.id == can::kIdHostObstacleDist && from_high) {
        can::gen::HostObstacleDist value{};
        if (can::decode_frame(fr, value) == can::gen::CodecStatus::Ok) g_obstacle_mm.store(value.distance_mm);
    }
    if (fr.id == can::kIdMtrMotorFbk && !from_high) {
        can::gen::MtrMotorFbk value{};
        if (can::decode_frame(fr, value) == can::gen::CodecStatus::Ok) g_mtr_actual_speed_mmps.store(value.actual_speed_mmps);
    }

    // 0x202 SES_ErrInfo — L3 fault bits → ESTOP (arch §7.3)
    if (fr.id == can::kIdSbwErrInfo && !from_high) {
        can::custom::ses::ErrorInfo error{};
        if (can::custom::ses::decode_error_info(fr.view(), error) != can::gen::CodecStatus::Ok) return;
        uint8_t angle_faults  = error.raw[1] & 0x0F;
        uint8_t torque_faults = (error.raw[2] >> 2) & 0x0F;
        if (angle_faults || torque_faults) {
            ESP_LOGW(TAG_DISP, "SES_ErrInfo L3 fault: angle=0x%X torque=0x%X", angle_faults, torque_faults);
            g_estop_reason.store(rt::kEstopReasonInternal);
            rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, 0};
            // Use timeout to avoid silent ESTOP drop when queue is full (bug B4)
            enqueue_safety_event(evt, pdMS_TO_TICKS(10));
        }
    }
    // 0x203 SES_Version — log SW/HW once (arch §7.3)
    if (fr.id == can::kIdSbwVersion && !from_high) {
        can::custom::ses::VersionRaw version{};
        if (can::custom::ses::decode_version(fr.view(), version) != can::gen::CodecStatus::Ok) return;
        static bool ses_version_logged = false;
        if (!ses_version_logged) {
            ESP_LOGI(TAG_DISP, "SES_Version: SW=%u.%02u HW=%u.%u",
                     unsigned(version.raw[0] / 100), unsigned(version.raw[0] % 100),
                     unsigned(version.raw[1] / 10), unsigned(version.raw[1] % 10));
            ses_version_logged = true;
        }
    }
    // 0x6FA SES_Test — motor current + ECU temp + supply voltage
    if (fr.id == can::kIdSbwTest && !from_high && fr.dlc >= 7) {
        can::custom::ses::TestTelemetry telemetry{};
        if (can::custom::ses::decode_test(fr.view(), telemetry) != can::gen::CodecStatus::Ok) return;
        int16_t mc_raw = telemetry.motor_current_raw;
        uint16_t et_raw = telemetry.ecu_temperature_raw;
        uint16_t pv_raw = telemetry.supply_voltage_raw;
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
    if (fr.id == can::kIdBbwTest && !from_high && fr.dlc >= 5) {
        can::custom::seb::TestTelemetry telemetry{};
        if (can::custom::seb::decode_test(fr.view(), telemetry) != can::gen::CodecStatus::Ok) return;
        int16_t mc_raw = telemetry.motor_current_raw;
        uint16_t et_raw = telemetry.ecu_temperature_raw;
        g_seb_motor_current.store(mc_raw);
        g_seb_ecu_temp_c.store(et_raw);
    }
    // 0x721 SEB_STATUS — capture pressure + error for 0x311 BRAKE_DIAG
    if (fr.id == can::kIdBbwStatus && !from_high) {
        can::custom::seb::Status value{};
        if (can::custom::seb::decode_status(fr.view(), value) != can::gen::CodecStatus::Ok) return;

        // L3 error check AFTER checksum validation (bug 4.8).
        // Previously evaluated in route_frame() BEFORE checksum, so bus noise
        // flipping error bits to 3 would trigger spurious ESTOP on corrupt frames.
        uint8_t seb_err = value.error_status;
        if (seb_err == 3) {
            g_estop_reason.store(rt::kEstopReasonInternal);
            rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, 0};
            enqueue_safety_event(evt, pdMS_TO_TICKS(10));
        }

        // Byte 3 is pressure ONLY in Pressure mode (control_mode=1).
        // In Stroke mode it's Stroke[15:8] — not pressure data.
        g_seb_pressure_raw.store(value.control_mode == 1 ? value.pressure_value_raw : 0);
        g_seb_error_status.store(seb_err);
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
            enqueue_safety_event(evt, 0);
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
