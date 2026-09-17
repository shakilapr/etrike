// RT ESP32-S3 ? Realtime Physics, Steering & CAN Gateway.
// Architecture: architecture.md ?7.  8 FreeRTOS tasks.

// Runtime System Mode Configuration
#include "system_mode.h"
#include "bypass_modes.h"

// Define runtime bypass flags
bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = false;
bool g_bypass_seb_sync = false;
bool g_bypass_mtr_absent = false;

#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "ESP-IDF 5.0 or later required"
#endif

#include "config.h"
#include "rt_state.h"
#include "safety_stream_loss.h" // 0x011 freshness fail-safe (unit-tested)
#include "build_config.h"      // compile-time feature flags (validated)
#include "resolver_config.h"   // rt::ActiveResolver (type alias, zero overhead)
#include "calculated_speed.h"  // CalculatedSpeedEstimator (SpeedFeedbackSource::Calculated)
#include "can_driver_twai.h"
#include "can_rx_router.h"
#include "brake_arbitration.h"
#include "seb_request.h"
#include "brake_fallback.h"
#include "encoder_pcnt.h"
#include "phase2_motion.h"

static const char* TAG = "rt";

// ???????????????????????????????????????????????????????????????????????
// Definitions for all extern declarations in rt_state.h.
// This is the single translation unit that owns the global state.
// ???????????????????????????????????????????????????????????????????????

// ?? CAN drivers ????????????????????????????????????????????????????
rt::Mcp2515Driver g_can_high;

// ?? Application objects ????????????????????????????????????????????
// g_resolver is typed as rt::ActiveResolver ? resolved at compile time from
// ETRIKE_RT_KINEMATICS_RESOLVER via resolver_config.h. Zero runtime overhead.
rt::ActiveResolver  g_resolver;
rt::SpeedController g_speed_ctrl;
rt::CalculatedSpeedEstimator g_calc_speed;  // used when SpeedFeedbackSource::Calculated
rt::SteeringControl g_steering;
rt::DualHeartbeat   g_heartbeat;
rt::CmdWatchdog     g_watchdog;

// ?? Safety event queue ?????????????????????????????????????????????
QueueHandle_t g_safety_evt_q = nullptr;  // depth 16, SafetyEvent
std::atomic<bool>     g_pending_estop_event{false};
std::atomic<int16_t>  g_pending_mode_event{-1};
std::atomic<uint32_t> g_safety_event_drops{0};
std::atomic<bool>     g_pending_safety_clear{false};
std::atomic<bool>     g_steering_estop_request{false};
std::atomic<bool>     g_steering_exit_request{false};
std::atomic<bool>     g_sys_clear_in_progress{false};

// ?? Shared state (atomics for sensor / latest-value data) ???????????
std::atomic<int32_t>  g_brake_request_kpa{0};
std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
std::atomic<int32_t>  g_ses_angle_0_1deg{INT16_MIN};
std::atomic<uint8_t>  g_ses_angle_status{0};
std::atomic<int32_t>  g_brake_kpa_to_send{0};
std::atomic<int32_t>  g_mtr_motor_command_speed_mmps{0};
std::atomic<uint8_t>  g_mtr_gear_state{uint8_t(can::Gear::N)};
std::atomic<int32_t>  g_encoder_speed_mmps{0};
std::atomic<int32_t>  g_direct_steer_angle_0_1deg{0};
std::atomic<bool>     g_direct_steer_valid{false};
std::atomic<int64_t>  g_last_direct_steer_us{-1};
std::atomic<int64_t>  g_last_mtr_feedback_us{-1};
std::atomic<int64_t>  g_last_ses_feedback_us{-1};
std::atomic<int64_t>  g_last_0x7B9_rx_us{-1};
std::atomic<int64_t>  g_last_nonzero_cmd_us{-1};

// ?? Derived state (written by control, read by tx tasks) ????????????
std::atomic<uint8_t>  g_mode_current{0};
std::atomic<bool>     g_seb_takeover{false};
std::atomic<bool>     g_mtr_unavailable{false};
std::atomic<uint8_t>  g_brake_fallback_state{0};

// ?? Heartbeat tracking ?????????????????????????????????????????????
std::atomic<int64_t>  g_last_sys_hb_us{0};
std::atomic<int64_t>  g_last_host_hb_us{0};
std::atomic<int64_t>  g_last_low_peer_us{0};
std::atomic<int64_t>  g_last_high_peer_us{0};
std::atomic<int64_t>  g_last_sys_safety_sts_us{0};
std::atomic<int64_t>  g_last_estop_sent_us{0};

// Issue #10: SYS 0x011 safety-authority derived flag. Default = no authority
// (boot never grants it); control_task flips it once the 0x011 stream is acquired.
// g_safety_authority (the supervisor) is defined in namespace rt below.
namespace rt {
std::atomic<uint8_t> g_ready_mask{0};
std::atomic<bool>    g_no_sys_authority{true};
}

// ── Mailboxes & Queues (3-Task Architecture) ────────────────────────
QueueHandle_t g_host_cmd_mailbox      = nullptr;  // depth 1, rt::HostDriveSnapshot (overwrite)
QueueHandle_t g_feedback_mailbox      = nullptr;  // depth 1, rt::ActuatorFeedbackSnapshot (overwrite)
QueueHandle_t g_motion_output_mailbox  = nullptr;  // depth 1, rt::MotionOutputSnapshot (overwrite)
QueueHandle_t g_high_to_low_gw_q      = nullptr;  // depth 8, rt::GatewayFrame
std::atomic<uint32_t> g_gw_drop_count{0};

// False when MCP2515 is missing — can_high_task does not transmit.
static std::atomic<bool> g_high_can_present{false};

// High CAN health, surfaced on the Low bus via RT_HEARTBEAT.health_flags.can_ok.
// False when the MCP2515/SPI stops responding even if bus_off() never latches.
std::atomic<bool> g_can_high_healthy{true};

// ── ESTOP reason atomic (written by safety/health, read by tx) ──────
std::atomic<uint8_t>  g_estop_reason{0};

// ── Telemetry atomics ───────────────────────────────────────────────
std::atomic<int16_t>  g_last_cmd_angle_0_1deg{0};
std::atomic<int16_t>  g_pid_output_mmps{0};
std::atomic<int32_t>  g_last_speed_setpoint_mmps{0};
std::atomic<bool>     g_reversing{false};
std::atomic<uint16_t> g_ses_motor_current{0};
std::atomic<uint16_t> g_ses_ecu_temp{0};
std::atomic<uint16_t> g_ses_pow_volt{0};
std::atomic<uint8_t>  g_ses_error_status{0};
std::atomic<uint16_t> g_seb_pressure_raw{0};
std::atomic<uint8_t>  g_seb_error_status{0};
std::atomic<uint16_t> g_seb_motor_current{0};
std::atomic<uint16_t> g_seb_ecu_temp_c{0};
std::atomic<uint8_t>  g_heartbeat_flags{0};

std::atomic<int64_t>  g_task_alive_high_us{0};
std::atomic<int64_t>  g_task_alive_low_us{0};
std::atomic<int64_t>  g_task_alive_control_us{0};

// ── Safety monitor & Phase B diagnostics ────────────────────────────
#include "safety_monitor.h"
#include "diag_rt.h"
#include "task_health.h"
#include "protocol/compat/can.hpp"

namespace rt {
MtrHealthSupervisor g_mtr_health;
SebBrakeFallback    g_brake_fallback;
SafetyStreamSupervisor g_safety_authority;  // issue #10 (0x011 authority acquisition)
}  // namespace rt

// ── CAN TX helpers ──────────────────────────────────────────────────
static uint32_t g_can_tx_fail_low = 0, g_can_tx_fail_high = 0;
static uint32_t g_can_tx_ok_low = 0, g_can_tx_ok_high = 0;
static bool g_can_tx_had_fail_low = false, g_can_tx_had_fail_high = false;
static uint32_t g_can_tx_consec_fail_low = 0;

static bool send_can_low(can::Frame& fr) {
    auto* drv = rt::can_low_driver();
    if (!drv) return false;
    if (drv->send(fr, 2)) {
        if (g_can_tx_had_fail_low) {
            ESP_LOGI(TAG, "Low CAN TX recovered — fail=%lu ok=%lu",
                     static_cast<unsigned long>(g_can_tx_fail_low),
                     static_cast<unsigned long>(g_can_tx_ok_low));
            g_can_tx_had_fail_low = false;
        }
        g_can_tx_ok_low++;
        g_can_tx_consec_fail_low = 0;
        return true;
    }
    g_can_tx_fail_low++;
    g_can_tx_consec_fail_low++;
    uint32_t state = 0, tec = 0, rec = 0;
    drv->status(state, tec, rec);
    if (!g_can_tx_had_fail_low || (g_can_tx_fail_low % 100 == 0)) {
        ESP_LOGW(TAG, "Low CAN TX failed (n=%lu consec=%lu state=%lu tec=%lu rec=%lu id=0x%lX)",
                 static_cast<unsigned long>(g_can_tx_fail_low),
                 static_cast<unsigned long>(g_can_tx_consec_fail_low),
                 static_cast<unsigned long>(state),
                 static_cast<unsigned long>(tec),
                 static_cast<unsigned long>(rec),
                 static_cast<unsigned long>(fr.id));
        g_can_tx_had_fail_low = true;
    }
    return false;
}

static bool send_can_high(can::Frame& fr) {
    if (!g_can_high.can_transmit()) {
        return false;
    }
    if (!g_can_high.send(fr)) {
        g_can_tx_fail_high++;
        if (!g_can_tx_had_fail_high) { ESP_LOGW(TAG, "High CAN TX failed"); g_can_tx_had_fail_high = true; }
        return false;
    }
    if (g_can_tx_had_fail_high) {
        ESP_LOGI(TAG, "High CAN TX recovered — fail=%lu ok=%lu", g_can_tx_fail_high, g_can_tx_ok_high);
        g_can_tx_had_fail_high = false;
    }
    g_can_tx_ok_high++;
    return true;
}

inline bool enqueue_safety_event(const rt::SafetyEvent& evt, TickType_t timeout) {
    if (g_safety_evt_q && xQueueSend(g_safety_evt_q, &evt, timeout) == pdTRUE) return true;

    g_safety_event_drops.fetch_add(1, std::memory_order_relaxed);
    if (evt.type == rt::SafetyEvent::ESTOP) {
        g_pending_estop_event.store(true, std::memory_order_release);
    } else if (evt.type == rt::SafetyEvent::SAFETY_CLEAR) {
        g_pending_safety_clear.store(true, std::memory_order_release);
    } else {
        g_pending_mode_event.store(evt.payload, std::memory_order_release);
    }
    return false;
}

inline bool post_gateway_frame(const can::Frame& fr) {
    if (!g_high_to_low_gw_q) return false;
    rt::GatewayFrame gw{fr, esp_timer_get_time()};
    if (xQueueSend(g_high_to_low_gw_q, &gw, 0) != pdTRUE) {
        g_gw_drop_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

// ── CAN bus health monitor ──────────────────────────────────────────
#include "can_health.h"

static void update_low_can_tx_admission(int64_t now_us) {
    auto* drv = rt::can_low_driver();
    if (!drv) return;
    drv->set_tx_admission(true);

    const int64_t last_peer = g_last_low_peer_us.load(std::memory_order_acquire);
    bool peer_fresh = last_peer > 0
        && now_us - last_peer <= int64_t(rt::kLowCanPeerTimeoutMs) * 1000;

    if (!peer_fresh && last_peer > 0) {
        rt::diag().raise(etrike::diagnostics::DiagId::RtLowCanPeerTimeout,
                         static_cast<std::uint16_t>((now_us - last_peer) / 1000));
    }
}

static void pump_diagnostics() {
    static int64_t last_pump_us = 0;
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_pump_us < 100'000) return;
    last_pump_us = now_us;

    etrike::diagnostics::DiagReport rpt{};
    uint8_t budget = 8;
    while (budget-- > 0 && rt::diag().pop_pending_report(rpt)) {
        can::gen::RtDiagEventRpt out{};
        out.diag_id = static_cast<std::uint16_t>(rpt.id);
        out.state = static_cast<std::uint8_t>(rpt.state);
        out.occurrence_count = rpt.occurrence_count;
        out.report_counter = rpt.report_counter;
        out.flags = rpt.flags;
        out.snapshot_data = rpt.snapshot_data;
        can::Frame fr{};
        if (can::gen::encode_rt_diag_event_rpt(out, fr) == can::gen::CodecStatus::Ok) {
            send_can_high(fr);
        }
    }
    rt::diag().replay_active_set();
}

static can::gen::RtNodeStatus build_rt_node_status() {
    can::gen::RtNodeStatus ns{};
    const uint8_t mode = g_mode_current.load();
    const uint8_t reason = g_estop_reason.load();
    const bool estop = reason != rt::kEstopReasonNone;
    const bool no_auth = g_no_sys_authority.load(std::memory_order_relaxed);
    if (estop) {
        ns.node_state = can::gen::RtNodeStatus::kNodeStateEstop;
    } else if (no_auth) {
        ns.node_state = can::gen::RtNodeStatus::kNodeStateInhibited;
    } else if (mode == uint8_t(can::Mode::Auto)) {
        ns.node_state = can::gen::RtNodeStatus::kNodeStateActive;
    } else {
        ns.node_state = can::gen::RtNodeStatus::kNodeStateStandby;
    }
    ns.block_mask = static_cast<uint16_t>((no_auth ? 1u : 0u) | (estop ? 2u : 0u));
    ns.estop_active = estop;
    ns.estop_latched = estop;
    ns.ready = !estop && !no_auth;
    ns.command_received = mode == uint8_t(can::Mode::Auto);
    ns.command_nonzero = ns.command_received
                      && g_last_speed_setpoint_mmps.load() != 0;
    ns.output_enabled = ns.ready && mode == uint8_t(can::Mode::Auto);
    ns.degraded = g_steering.state() == rt::SteerState::STEER_FAULT;
    const uint8_t cur_mask = g_ready_mask.load(std::memory_order_relaxed);
    ns.recovery_pending = !estop && !no_auth && rt::is_sys_authority_ready(cur_mask)
                          && !rt::is_motion_ready(cur_mask);
    return ns;
}

static can::gen::RtDiagRpt build_rt_diag_rpt() {
    can::gen::RtDiagRpt dr{};
    dr.mcp_bus_off     = g_can_high.bus_off();
    dr.mcp_recovering  = g_can_high.is_recovering();
    uint8_t eflg = 0, tec = 0, rec = 0;
    if (g_can_high.read_bus_diag(eflg, tec, rec)) {
        dr.mcp_eflg = eflg;
        dr.mcp_tec  = tec;
        dr.mcp_rec  = rec;
    }
    const uint32_t spi_total = g_can_high.spi_failure_count();
    static uint32_t last_spi_total = 0;
    static bool     last_spi_valid = false;
    const uint32_t raw_delta = last_spi_valid
        ? (spi_total >= last_spi_total ? spi_total - last_spi_total : spi_total)
        : 0;
    last_spi_total = spi_total;
    last_spi_valid = true;
    dr.spi_fault_delta = static_cast<uint8_t>(raw_delta > 255 ? 255 : raw_delta);
    dr.spi_fault_since_report = dr.spi_fault_delta != 0;
    dr.mcp_recovery_attempts = static_cast<uint8_t>(g_can_high.recovery_attempts());
    dr.mtr_unavailable = g_mtr_unavailable.load(std::memory_order_relaxed);
    dr.no_sys_authority = g_no_sys_authority.load(std::memory_order_relaxed);
    dr.brake_fallback_state = g_brake_fallback_state.load(std::memory_order_relaxed);
    return dr;
}

static void send_seb_req(rt::TwaiDriver& drv, can::Frame& fr,
                         can::custom::seb::Command seb, uint8_t& rolling_counter) {
    seb.control_enable = 1;
    seb.rolling_counter = rolling_counter;
    rolling_counter = (rolling_counter + 1) & 0x0F;
    if (can::custom::seb::encode_command(seb, fr) != can::gen::CodecStatus::Ok) return;
    drv.send(fr);
}

static uint8_t task_health_snapshot() {
    return rt::task_health_from_timestamps(
        esp_timer_get_time(),
        g_task_alive_high_us.load(std::memory_order_relaxed),
        g_task_alive_low_us.load(std::memory_order_relaxed),
        g_task_alive_control_us.load(std::memory_order_relaxed));
}// ── TASK 1: can_high_task (Core 0, Priority 4) ──────────────────────
[[noreturn]] void can_high_task(void*) {
    can::Frame fr;
    TickType_t last_100hz = xTaskGetTickCount();
    TickType_t last_10hz  = xTaskGetTickCount();
    int64_t    last_1hz_us = esp_timer_get_time();
    int64_t    last_2hz_us = esp_timer_get_time();
    uint8_t    motion_counter = 0;
    uint8_t    node_status_roll_high = 0;
    uint8_t    diag_counter = 0;
    uint32_t   rpt_fail_count = 0;
    uint32_t   diag_fail_count = 0;
    int64_t    last_high_init_retry_us = esp_timer_get_time();

    rt::HostDriveSnapshot host_snap{};

    while (true) {
        g_task_alive_high_us.store(esp_timer_get_time(), std::memory_order_relaxed);
        if (!g_can_high.is_initialized()) {
            const int64_t retry_now_us = esp_timer_get_time();
            if (retry_now_us - last_high_init_retry_us >= 1'000'000) {
                last_high_init_retry_us = retry_now_us;
                const bool initialized = g_can_high.init();
                g_high_can_present.store(initialized, std::memory_order_relaxed);
                if (initialized) {
                    ESP_LOGI(TAG, "High CAN MCP2515 recovered");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 1. Sleep waiting for MCP2515 INT pin (GPIO 47) or 10 ms timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        // Diagnostics reports are High-CAN traffic and therefore must be
        // pumped by the owner task after every periodic wake.
        pump_diagnostics();
        // 2. Bounded RX Drain (budget = 8 frames)
        for (unsigned i = 0; i < 8; ++i) {
            if (!g_can_high.receive(fr, 0)) break;
            const int64_t now_us = esp_timer_get_time();

            if (can::is_known_frame_on_bus(fr.id, fr.extended, fr.dlc, can::Bus::High)) {
                g_last_high_peer_us.store(now_us, std::memory_order_release);
            }

            // Check for Host Heartbeat (0x7FD)
            if (fr.id == can::kIdHostHeartbeat) {
                can::gen::HostHeartbeat hb{};
                if (can::decode_frame(fr, hb) == can::gen::CodecStatus::Ok) {
                    static uint8_t last_host_ctr = 0;
                    static bool host_first = true;
                    uint8_t delta = hb.alive_ctr - last_host_ctr;
                    if (host_first || delta != 0) {
                        host_first = false;
                        last_host_ctr = hb.alive_ctr;
                        g_last_host_hb_us.store(now_us);
                    }
                }
                continue;
            }

            // Check for SAFETY_ESTOP (0x001)
            if (fr.id == can::kIdSafetyEstop) {
                g_estop_reason.store(rt::kEstopReasonCanEstop);
                g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_HOST), std::memory_order_release);
                rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, rt::kEstopReasonCanEstop};
                enqueue_safety_event(evt, pdMS_TO_TICKS(10));

                can::Frame estop = can::Frame::standard(can::kIdSafetyEstop, 0);
                post_gateway_frame(estop);
                continue;
            }

            // Check for HOST_DRIVE_CMD (0x300)
            if (fr.id == can::kIdHostDriveCmd) {
                can::gen::HostDriveCmd cmd{};
                if (can::decode_frame(fr, cmd) == can::gen::CodecStatus::Ok) {
                    host_snap.speed_mmps = cmd.speed_mmps;
                    host_snap.yaw_rate_mrad_s = cmd.yaw_rate_mrad_s;
                    host_snap.gear_override = cmd.gear;
                    host_snap.timestamp_us = now_us;
                    host_snap.drive_cmd_valid = true;
                    g_ready_mask.fetch_or(rt::READY_BIT_HOST, std::memory_order_release);
                    if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &host_snap);
                    g_watchdog.feed(now_us);
                    g_steering_exit_request.store(true);
                }
                continue;
            }

            // Check for HOST_STEER_CMD (0x303)
            if (fr.id == can::kIdHostSteerCmd) {
                can::gen::HostSteerCmd scmd{};
                if (can::decode_frame(fr, scmd) == can::gen::CodecStatus::Ok) {
                    static uint8_t last_counter = 0;
                    static bool first = true;
                    const uint8_t delta = scmd.rolling_counter - last_counter;
                    if (first || delta != 0) {
                        first = false;
                        last_counter = scmd.rolling_counter;
                        g_direct_steer_angle_0_1deg.store(scmd.steer_angle_0_1deg);
                        g_direct_steer_valid.store(scmd.angle_valid);
                        g_last_direct_steer_us.store(now_us);

                        host_snap.direct_steer_0_1deg = scmd.steer_angle_0_1deg;
                        host_snap.direct_steer_valid = scmd.angle_valid;
                        if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &host_snap);
                    }
                }
                continue;
            }

            // Check for HOST_BRAKE_REQ (0x301)
            if (fr.id == can::kIdHostBrakeReq) {
                can::gen::HostBrakeReq b_req{};
                if (can::decode_frame(fr, b_req) == can::gen::CodecStatus::Ok) {
                    g_brake_request_kpa.store(b_req.brake_pressure_kpa);
                }
                continue;
            }

            // Check for HOST_OBSTACLE_DIST (0x320)
            if (fr.id == can::kIdHostObstacleDist) {
                can::gen::HostObstacleDist od{};
                if (can::decode_frame(fr, od) == can::gen::CodecStatus::Ok) {
                    g_obstacle_mm.store(od.distance_mm);
                    host_snap.obstacle_distance_mm = od.distance_mm;
                    if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &host_snap);
                }
                continue;
            }

            // Transparent High -> Low Gateway forwarding
            if (fr.id == can::kIdHmiModeReq || fr.id == can::kIdHmiPwrReq ||
                fr.id == can::kIdHostLightCmd || fr.id == can::kIdHostEstopResetReq ||
                can::is_forwarded_high_to_low(fr.id)) {
                post_gateway_frame(fr);
            }
        }

        // 3. 100 Hz Periodic Output: 0x121 RT_MOTION_RPT
        if (xTaskGetTickCount() - last_100hz >= pdMS_TO_TICKS(10)) {
            last_100hz = xTaskGetTickCount();
            rt::MotionOutputSnapshot out{};
            rt::ActuatorFeedbackSnapshot fbk{};
            if (g_motion_output_mailbox) xQueuePeek(g_motion_output_mailbox, &out, 0);
            if (g_feedback_mailbox) xQueuePeek(g_feedback_mailbox, &fbk, 0);

            can::Frame motion_fr;
            auto rpt = rt::make_motion_report(
                esp_timer_get_time(),
                fbk.mtr_command_speed_mmps,
                fbk.mtr_gear_state,
                fbk.last_mtr_us,
                fbk.ses_angle_0_1deg,
                fbk.ses_angle_status,
                fbk.last_ses_us,
                motion_counter);
            if (can::encode_frame(rpt, motion_fr) == can::gen::CodecStatus::Ok) {
                if (send_can_high(motion_fr)) {
                    ++motion_counter;
                }
            }
        }

        // 4. 10 Hz Periodic Telemetry: 0x210, 0x501, 0x310, 0x311, 0x220
        if (xTaskGetTickCount() - last_10hz >= pdMS_TO_TICKS(100)) {
            last_10hz = xTaskGetTickCount();

            // 0x210 RT_STATE_RPT
            can::gen::RtStateRpt rpt{};
            rpt.mode = g_mode_current.load();
            auto ss = g_steering.state();
            rpt.safety_state = (ss == rt::SteerState::STEER_ACTIVE) ? 0 :
                               (ss == rt::SteerState::STEER_FAULT)   ? 2 : 1;
            rpt.reversing    = g_reversing.load();
            rpt.rx_overflow  = static_cast<uint8_t>(g_can_high.rx_overflow_count());
            rpt.estop_reason = g_estop_reason.load();
            rpt.steer_state  = static_cast<uint8_t>(ss);
            rpt.task_health  = task_health_snapshot();
#ifdef BENCH_BUILD_ACKNOWLEDGED
            rpt.task_health |= 0x80;
#endif
            can::Frame state_fr{};
            const auto state_status = can::encode_frame(rpt, state_fr);
            if (state_status == can::gen::CodecStatus::Ok) {
                send_can_high(state_fr);
            } else {
                static uint32_t state_rpt_encode_fail_high = 0;
                state_rpt_encode_fail_high++;
                if (state_rpt_encode_fail_high == 1 || state_rpt_encode_fail_high % 100 == 0) {
                    ESP_LOGE(TAG, "RT_STATE_RPT High encode failed: status=%d mode=%u reason=%u (count=%lu)",
                             static_cast<int>(state_status), rpt.mode, rpt.estop_reason, state_rpt_encode_fail_high);
                }
            }

            // 0x501 RT_NODE_STATUS
            can::gen::RtNodeStatus ns = build_rt_node_status();
            ns.rolling_counter = node_status_roll_high++;
            ns.e2e_crc = 0;
            can::Frame ns_fr{};
            if (can::encode_frame(ns, ns_fr) == can::gen::CodecStatus::Ok) {
                ns.e2e_crc = can::e2e::crc8_h2f(ns_fr.data.data(), 7u, 0u);
                if (can::encode_frame(ns, ns_fr) == can::gen::CodecStatus::Ok) {
                    send_can_high(ns_fr);
                }
            }

            // 0x310 STEER_DIAG
            int16_t angle = g_ses_angle_0_1deg.load();
            uint8_t s_fault = (g_ses_error_status.load() > 0) ? 1 : 0;
            uint16_t mtr_curr = uint16_t((g_ses_motor_current.load() * 25) / 32);
            uint16_t ecu_tmp = uint16_t(g_ses_ecu_temp.load() * 5);
            can::gen::SteerDiag s_diag{};
            s_diag.angle_0_1deg = angle * 0.1;
            s_diag.fault = s_fault;
            s_diag.motor_current = mtr_curr * 0.01;
            s_diag.ecu_temp = ecu_tmp * 0.1;
            can::Frame s_fr{};
            if (can::encode_frame(s_diag, s_fr) == can::gen::CodecStatus::Ok) {
                if (!g_can_high.send(s_fr)) {
                    diag_fail_count++;
                    if (diag_fail_count == 1 || diag_fail_count % 100 == 0) {
                        ESP_LOGW(TAG, "MCP2515 STEER_DIAG send failed (count=%lu)", diag_fail_count);
                    }
                } else if (diag_fail_count > 0) {
                    ESP_LOGI(TAG, "MCP2515 STEER_DIAG send recovered after %lu failures", diag_fail_count);
                    diag_fail_count = 0;
                }
            }

            // 0x311 BRAKE_DIAG
            uint16_t seb_pressure = g_seb_pressure_raw.load();
            uint8_t  seb_fault    = (g_seb_error_status.load() > 0) ? 1 : 0;
            uint16_t b_mtr_curr   = uint16_t((g_seb_motor_current.load() * 25) / 32);
            int32_t  b_ecu_tmp_raw = int32_t(g_seb_ecu_temp_c.load()) * 5 - 400;
            uint16_t b_ecu_tmp    = b_ecu_tmp_raw < 0 ? 0 : uint16_t(b_ecu_tmp_raw);
            can::gen::BrakeDiag b_diag{};
            b_diag.pressure_raw  = seb_pressure * 0.05;
            b_diag.fault         = seb_fault;
            b_diag.motor_current = b_mtr_curr * 0.01;
            b_diag.ecu_temp      = b_ecu_tmp * 0.1;
            can::Frame b_fr{};
            if (can::encode_frame(b_diag, b_fr) == can::gen::CodecStatus::Ok) {
                send_can_high(b_fr);
            }

            // 0x220 RT_PID_RPT
#if ETRIKE_RT_PID_MODE > 0
            int16_t setpoint = static_cast<int16_t>(std::clamp(
                g_last_speed_setpoint_mmps.load(), int32_t(-32768), int32_t(32767)));
            int16_t measured = g_mtr_motor_command_speed_mmps.load();
            int16_t pid      = g_pid_output_mmps.load();
            can::gen::RtPidRpt pid_msg{setpoint, measured, pid};
            can::Frame pid_fr{};
            if (can::encode_frame(pid_msg, pid_fr) == can::gen::CodecStatus::Ok) {
                send_can_high(pid_fr);
            }
#endif
        }

        // 5. 1 Hz Telemetry: 0x620 RT_DIAG_RPT
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_1hz_us >= 1'000'000) {
            last_1hz_us = now_us;
            can::gen::RtDiagRpt dr = build_rt_diag_rpt();
            dr.rolling_counter = diag_counter++;
            can::Frame d_fr{};
            if (can::encode_frame(dr, d_fr) == can::gen::CodecStatus::Ok) {
                send_can_high(d_fr);
            }
        }

        // 6. 2 Hz Heartbeat (0x7FC) & High CAN Bus Recovery
        if (now_us - last_2hz_us >= 500'000) {
            last_2hz_us = now_us;
            if (g_can_high.can_transmit()) {
                can::Frame h_fr{};
                g_heartbeat.tick_high(h_fr, g_heartbeat_flags.load(std::memory_order_relaxed));
                static uint32_t hb_fail_count = 0;
                if (!g_can_high.send(h_fr)) {
                    hb_fail_count++;
                    if (hb_fail_count == 1 || hb_fail_count % 100 == 0) {
                        ESP_LOGW(TAG, "MCP2515 heartbeat send failed (count=%lu)", hb_fail_count);
                    }
                } else if (hb_fail_count > 0) {
                    ESP_LOGI(TAG, "MCP2515 heartbeat send recovered after %lu failures", hb_fail_count);
                    hb_fail_count = 0;
                }
            }

            if (g_can_high.bus_off()) {
                static int64_t last_reinit_us = 0;
                if (last_reinit_us == 0 || now_us - last_reinit_us > 3'000'000) {
                    last_reinit_us = now_us;
                    uint8_t tec = 0, rec = 0;
                    g_can_high.get_error_counters(tec, rec);
                    rt::diag().raise(etrike::diagnostics::DiagId::RtCanHighBusOff,
                                     static_cast<std::uint16_t>((static_cast<std::uint16_t>(tec) << 8) | rec));
                    ESP_LOGE(TAG, "High CAN bus-off — controller recovery");
                    g_can_high.recover();
                }
            }

            // High CAN liveness watchdog. A silent MCP2515/SPI reads CANINTF as
            // 0x00, so EFLG.TXBO never sets, bus_off() never latches and the 3 s
            // recovery above never fires — the High bus is dead while RT still
            // reports healthy. Probe the MCP configuration registers; if they
            // stop reading back, force recovery regardless of bus_off(). Also
            // surfaced on the Low bus via RT_HEARTBEAT.health_flags.can_ok.
            const bool high_tx_capable = g_can_high.can_transmit();
            static int64_t probe_fail_since_us = 0;
            static int64_t last_wd_recover_us = 0;
            if (high_tx_capable) {
                if (g_can_high.health_probe()) {
                    probe_fail_since_us = 0;
                } else if (probe_fail_since_us == 0) {
                    probe_fail_since_us = now_us;
                }
            }
            const bool probe_dead = high_tx_capable && probe_fail_since_us != 0
                                 && now_us - probe_fail_since_us > 2'000'000;
            const bool high_healthy = g_can_high.is_initialized()
                                   && !g_can_high.bus_off()
                                   && !g_can_high.is_recovering()
                                   && !probe_dead;
            if (g_can_high_healthy.exchange(high_healthy, std::memory_order_relaxed)
                != high_healthy) {
                if (high_healthy) {
                    ESP_LOGI(TAG, "High CAN health restored");
                } else if (probe_dead) {
                    ESP_LOGE(TAG, "High CAN unhealthy — MCP2515/SPI not responding");
                } else {
                    ESP_LOGW(TAG, "High CAN unavailable (bus-off/recovering/uninitialized)");
                }
            }
            if (probe_dead) {
                if (last_wd_recover_us == 0 || now_us - last_wd_recover_us > 1'000'000) {
                    last_wd_recover_us = now_us;
                    rt::diag().raise(etrike::diagnostics::DiagId::RtCanHighBusOff,
                                     static_cast<std::uint16_t>(0xFFFF));
                    ESP_LOGE(TAG, "High CAN liveness watchdog — forcing MCP2515 recovery");
                    g_can_high.recover();
                }
            }
        }
    }
}

// ── TASK 2: can_low_task (Core 0, Priority 4) ───────────────────────
[[noreturn]] void can_low_task(void*) {
    can::Frame fr;
    TickType_t last_100hz = xTaskGetTickCount();
    TickType_t last_secondary = xTaskGetTickCount();
    TickType_t last_10hz  = xTaskGetTickCount();
    int64_t    last_low_hb_us = esp_timer_get_time();

    uint8_t    node_status_roll = 0;
    uint8_t    secondary_slot = 0;
    uint8_t    seb_roll = 0;
    auto*      drv = rt::can_low_driver();

    rt::ActuatorFeedbackSnapshot fbk_snap{};

    while (true) {
        g_task_alive_low_us.store(esp_timer_get_time(), std::memory_order_relaxed);
        if (!drv) {
            vTaskDelay(pdMS_TO_TICKS(10));
            drv = rt::can_low_driver();
            continue;
        }

        // 1. Drain incoming Low CAN frames from TWAI queue (1 ms timeout)
        while (drv->receive(fr, 1)) {
            const int64_t now_us = esp_timer_get_time();

            if (can::is_known_frame_on_bus(fr.id, fr.extended, fr.dlc, can::Bus::Low)) {
                g_last_low_peer_us.store(now_us, std::memory_order_release);
            }

            // SYS_HEARTBEAT (0x7FE)
            if (fr.id == can::kIdSysHeartbeat) {
                can::gen::SysHeartbeat heartbeat{};
                if (can::decode_frame(fr, heartbeat) == can::gen::CodecStatus::Ok) {
                    static uint8_t last_sys_ctr = 0;
                    static bool sys_first = true;
                    uint8_t delta = heartbeat.alive_ctr - last_sys_ctr;
                    if (sys_first || delta != 0) {
                        sys_first = false;
                        last_sys_ctr = heartbeat.alive_ctr;
                        g_last_sys_hb_us.store(now_us);
                        fbk_snap.last_sys_hb_us = now_us;
                        if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                    }
                }
                continue;
            }

            // SYS_SAFETY_STS (0x011)
            if (fr.id == can::kIdSysSafetySts) {
                can::gen::SysSafetySts ssts{};
                if (can::gen::decode_sys_safety_sts(fr.view(), ssts) == can::gen::CodecStatus::Ok) {
                    const uint8_t crc = can::e2e::sys_safety_sts_crc(fr.data.data());
                    static etrike::protocol::StreamValidity ssts_val;
                    static bool ssts_inited = false;
                    if (!ssts_inited) {
                        ssts_val.set_key(1, can::kIdSysSafetySts, 700);
                        ssts_inited = true;
                    }
                    if (crc != ssts.e2e_crc) {
                        ssts_val.invalidate_now();
                    } else {
                        const bool ok = ssts_val.observe(
                            static_cast<uint8_t>(ssts.rolling_counter), xTaskGetTickCount());
                        if (ok) {
                            g_last_sys_safety_sts_us.store(now_us, std::memory_order_release);
                            fbk_snap.last_sys_safety_sts_us = now_us;
                            if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);

                            static bool ssts_latched = false;
                            static uint8_t ssts_clear_confirm = 0;
                            static bool ssts_last_zero = false;
                            static uint8_t ssts_clear_last_ctr = 0;
                            if (ssts.estop_active) {
                                g_sys_clear_in_progress.store(false, std::memory_order_relaxed);
                                if (!ssts_latched) {
                                    ssts_latched = true;
                                    rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, rt::kEstopReasonCanEstop};
                                    enqueue_safety_event(evt, pdMS_TO_TICKS(10));
                                }
                                ssts_clear_confirm = 0;
                                ssts_last_zero = false;
                            } else if (!ssts_latched) {
                                g_sys_clear_in_progress.store(false, std::memory_order_relaxed);
                                ssts_clear_confirm = 0;
                                ssts_last_zero = true;
                                ssts_clear_last_ctr = static_cast<uint8_t>(ssts.rolling_counter);
                            } else {
                                g_sys_clear_in_progress.store(true, std::memory_order_relaxed);
                                const bool first_zero = !ssts_last_zero;
                                const bool advances = (ssts.rolling_counter ==
                                    static_cast<uint8_t>(ssts_clear_last_ctr + 1u));
                                if (first_zero) {
                                    ssts_clear_confirm = 1;
                                } else if (advances) {
                                    ++ssts_clear_confirm;
                                } else {
                                    ssts_clear_confirm = 1;
                                }
                                ssts_clear_last_ctr = static_cast<uint8_t>(ssts.rolling_counter);
                                ssts_last_zero = true;
                                if (ssts_clear_confirm >= 2) {
                                    ssts_latched = false;
                                    g_sys_clear_in_progress.store(false, std::memory_order_relaxed);
                                    rt::SafetyEvent clr{rt::SafetyEvent::SAFETY_CLEAR, 0};
                                    enqueue_safety_event(clr, 0);
                                    g_steering_exit_request.store(true);
                                }
                            }
                        }
                    }
                }
                // Forward SYS_SAFETY_STS Low→High so the host can observe it.
                send_can_high(fr);
                continue;
            }

            // SYS_MODE_CMD (0x110)
            if (fr.id == can::kIdSysModeCmd) {
                can::gen::SysModeCmd decoded{};
                if (can::decode_frame(fr, decoded) == can::gen::CodecStatus::Ok) {
                    static etrike::protocol::StreamValidity sval;
                    static bool inited = false;
                    if (!inited) {
                        sval.set_key(1, can::kIdSysModeCmd, can::gen::SysModeCmd::kCycleMs * 5);
                        inited = true;
                    }
                    const bool ok = sval.observe(
                        static_cast<std::uint8_t>(decoded.rolling_counter), xTaskGetTickCount());
                    fbk_snap.sys_mode_valid = ok;
                    if (ok) {
                        fbk_snap.sys_mode = decoded.mode;
                        g_ready_mask.fetch_or(rt::READY_BIT_MODE, std::memory_order_release);
                        rt::SafetyEvent evt{rt::SafetyEvent::MODE_CHANGE, decoded.mode};
                        enqueue_safety_event(evt, 0);
                    }
                    if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                }
                continue;
            }

            // SAFETY_ESTOP (0x001) from Low
            if (fr.id == can::kIdSafetyEstop) {
                g_estop_reason.store(rt::kEstopReasonCanEstop);
                g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_HOST), std::memory_order_release);
                rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, rt::kEstopReasonCanEstop};
                enqueue_safety_event(evt, pdMS_TO_TICKS(10));
                continue;
            }

            // MTR_MOTOR_FBK (0x206)
            if (fr.id == can::kIdMtrMotorFbk) {
                can::gen::MtrMotorFbk value{};
                if (can::decode_frame(fr, value) == can::gen::CodecStatus::Ok) {
                    g_mtr_motor_command_speed_mmps.store(value.motor_command_speed_mmps);
                    g_mtr_gear_state.store(value.gear_state);
                    g_last_mtr_feedback_us.store(now_us);

                    fbk_snap.mtr_command_speed_mmps = value.motor_command_speed_mmps;
                    fbk_snap.mtr_gear_state = value.gear_state;
                    fbk_snap.last_mtr_us = now_us;
                    if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                }
                // Forward MTR_MOTOR_FBK Low→High for host telemetry.
                send_can_high(fr);
                continue;
            }

            // SBW_STATUS (0x201)
            if (fr.id == can::kIdSbwStatus) {
                can::custom::ses::Status value{};
                if (can::custom::ses::decode_status(fr.view(), value) == can::gen::CodecStatus::Ok) {
                    static uint8_t last_eps_roll = 0;
                    static bool eps_first = true;
                    uint8_t roll = value.rolling_counter;
                    uint8_t delta = roll - last_eps_roll;
                    if (eps_first || delta != 0) {
                        eps_first = false;
                        last_eps_roll = roll;
                        int16_t angle = static_cast<int16_t>(value.steering_angle_raw - rt::kSbwAngleOffset);
                        g_ses_angle_0_1deg.store(angle);
                        g_ses_angle_status.store(value.angle_aligned);
                        g_last_ses_feedback_us.store(now_us);

                        fbk_snap.ses_angle_0_1deg = angle;
                        fbk_snap.ses_angle_status = value.angle_aligned;
                        fbk_snap.last_ses_us = now_us;
                    }
                    g_ses_error_status.store(value.error_status);
                    fbk_snap.ses_error_status = value.error_status;
                    if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                }
                continue;
            }

            // VCU_SEB_REQ (0x7B9) observation
            if (fr.id == can::kIdVcuSebReq) {
                g_last_0x7B9_rx_us.store(now_us, std::memory_order_relaxed);
                fbk_snap.last_0x7b9_rx_us = now_us;
                if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                continue;
            }

            // BBW_STATUS (0x721)
            if (fr.id == can::kIdBbwStatus) {
                can::custom::seb::Status value{};
                if (can::custom::seb::decode_status(fr.view(), value) == can::gen::CodecStatus::Ok) {
                    uint8_t seb_err = value.error_status;
                    if (seb_err == 3) {
                        rt::diag().raise(etrike::diagnostics::DiagId::RtSesL3Fault,
                                         static_cast<std::uint16_t>(seb_err));
                        g_estop_reason.store(rt::kEstopReasonInternal);
                        rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, rt::kEstopReasonInternal};
                        enqueue_safety_event(evt, pdMS_TO_TICKS(10));
                    }
                    uint16_t pres = (value.control_mode == 1 ? value.pressure_value_raw : 0);
                    g_seb_pressure_raw.store(pres);
                    g_seb_error_status.store(seb_err);

                    fbk_snap.seb_pressure_raw = pres;
                    fbk_snap.seb_error_status = seb_err;
                    if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                }
                continue;
            }

            // SBW_ERR_INFO (0x202)
            if (fr.id == can::kIdSbwErrInfo) {
                can::custom::ses::ErrorInfo error{};
                if (can::custom::ses::decode_error_info(fr.view(), error) == can::gen::CodecStatus::Ok) {
                    constexpr uint16_t kSesL3Mask = 0x3C0F;
                    const uint16_t fault_bits = static_cast<uint16_t>(error.raw[1]) |
                                                (static_cast<uint16_t>(error.raw[2]) << 8);
                    const uint16_t active_l3 = fault_bits & kSesL3Mask;
                    if (active_l3 != 0) {
                        ESP_LOGW(TAG, "SES_ErrInfo L3 fault: 0x%04X", active_l3);
                        rt::diag().raise(etrike::diagnostics::DiagId::RtSesL3Fault, active_l3);
                        g_estop_reason.store(rt::kEstopReasonInternal);
                        rt::SafetyEvent evt{rt::SafetyEvent::ESTOP, rt::kEstopReasonInternal};
                        enqueue_safety_event(evt, pdMS_TO_TICKS(10));
                    }
                }
                continue;
            }

            // SBW_TEST (0x6FA)
            if (fr.id == can::kIdSbwTest && fr.dlc >= 7) {
                can::custom::ses::TestTelemetry telemetry{};
                if (can::custom::ses::decode_test(fr.view(), telemetry) == can::gen::CodecStatus::Ok) {
                    g_ses_motor_current.store(telemetry.motor_current_raw);
                    g_ses_ecu_temp.store(telemetry.ecu_temperature_raw);
                    g_ses_pow_volt.store(telemetry.supply_voltage_raw);
                    fbk_snap.ses_motor_current = telemetry.motor_current_raw;
                    fbk_snap.ses_ecu_temp = telemetry.ecu_temperature_raw;
                    fbk_snap.ses_pow_volt = telemetry.supply_voltage_raw;
                    if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                }
                continue;
            }

            // BBW_TEST (0x6FB)
            if (fr.id == can::kIdBbwTest && fr.dlc >= 5) {
                can::custom::seb::TestTelemetry telemetry{};
                if (can::custom::seb::decode_test(fr.view(), telemetry) == can::gen::CodecStatus::Ok) {
                    g_seb_motor_current.store(telemetry.motor_current_raw);
                    g_seb_ecu_temp_c.store(telemetry.ecu_temperature_raw);
                    fbk_snap.seb_motor_current = telemetry.motor_current_raw;
                    fbk_snap.seb_ecu_temp_c = telemetry.ecu_temperature_raw;
                    if (g_feedback_mailbox) xQueueOverwrite(g_feedback_mailbox, &fbk_snap);
                }
                continue;
            }

            // Catch-all: forward any Low→High transparent frames that have no
            // local handler in this task (e.g. SYS_THROTTLE_STS, SYS_DIAG_RPT).
            if (can::is_forwarded_low_to_high(fr.id)) {
                send_can_high(fr);
            }
        }

        // 2. Forward Gateway Frames (High -> Low)
        if (g_high_to_low_gw_q) {
            rt::GatewayFrame gw{};
            const int64_t now_us = esp_timer_get_time();
            while (xQueueReceive(g_high_to_low_gw_q, &gw, 0) == pdTRUE) {
                if (now_us - gw.enqueued_us <= 500'000) {
                    drv->send(gw.frame, 2);
                }
            }
        }

        // 3. 100 Hz Actuator Output: 0x204 RT_DRIVE_CMD
        rt::MotionOutputSnapshot out{};
        if (xTaskGetTickCount() - last_100hz >= pdMS_TO_TICKS(10)) {
            last_100hz = xTaskGetTickCount();
            if (g_motion_output_mailbox && xQueuePeek(g_motion_output_mailbox, &out, 0) == pdTRUE) {
                can::gen::RtDriveCmd msg{out.motor_speed_mmps, out.motor_gear};
                if (can::encode_frame(msg, fr) == can::gen::CodecStatus::Ok) {
                    send_can_low(fr);
                }
            }
        }
        // 2 Hz heartbeat is Low-CAN traffic and remains owned by this task.
        const int64_t now_low_us = esp_timer_get_time();
        if (now_low_us - last_low_hb_us >= 500'000) {
            last_low_hb_us = now_low_us;
            can::Frame hb_frame{};
            g_heartbeat.tick_low(hb_frame, g_heartbeat_flags.load(std::memory_order_relaxed));
            send_can_low(hb_frame);
        }

        // 4. Actuator outputs: 0x205 + 0x169 at 50 Hz each, 0x7B9 emergency
        // fallback, and 0x501 node status at 50 Hz.
        // TWAI has one application TX slot, so secondary frames are alternated
        // on a 10 ms cadence — each frame therefore lands every 20 ms (50 Hz).
        const TickType_t tick_secondary = xTaskGetTickCount();
        if (tick_secondary - last_secondary >= pdMS_TO_TICKS(10)) {
            last_secondary = tick_secondary;
            if (g_motion_output_mailbox) xQueuePeek(g_motion_output_mailbox, &out, 0);
            can::Frame secondary_frame{};

            if (out.current_mode != uint8_t(can::Mode::Manual)) {
                if (out.seb_emergency_takeover) {
                    // RT emergency 0x7B9 fallback wins over the alternation.
                    send_seb_req(*drv, secondary_frame, rt::make_seb_takeover_req(), seb_roll);
                } else if ((secondary_slot & 0x1) == 0) {
                    // 0x205 RT_BRAKE_CMD (brake intent -> SYS -> SEB)
                    can::gen::RtBrakeCmd bmsg{out.brake_kpa};
                    if (can::encode_frame(bmsg, secondary_frame) == can::gen::CodecStatus::Ok) {
                        send_can_low(secondary_frame);
                    }
                } else if (out.steer_command_enable) {
                    // 0x169 VCU_SES_REQ (steer request -> SES)
                    can::custom::ses::Command smsg{};
                    const int64_t now_ms = esp_timer_get_time() / 1000;
                    if (g_steering.tick(g_ses_angle_0_1deg.load(), g_ses_angle_status.load(),
                                        now_ms, smsg)
                        && can::custom::ses::encode_command(smsg, secondary_frame)
                            == can::gen::CodecStatus::Ok) {
                        send_can_low(secondary_frame);
                    }
                }
            }
            secondary_slot++;

            // 0x501 RT_NODE_STATUS at 50 Hz (every 2nd secondary tick = 20 ms).
            if ((secondary_slot & 0x1) == 0) {
                can::gen::RtNodeStatus ns = build_rt_node_status();
                ns.rolling_counter = node_status_roll++;
                ns.e2e_crc = 0;
                can::Frame nfr{};
                if (can::encode_frame(ns, nfr) == can::gen::CodecStatus::Ok) {
                    ns.e2e_crc = can::e2e::crc8_h2f(nfr.data.data(), 7u, 0u);
                    if (can::encode_frame(ns, nfr) == can::gen::CodecStatus::Ok) {
                        send_can_low(nfr);
                    }
                }
            }
        }

        // 5. 10 Hz Periodic Telemetry on Low CAN: 0x210 RT_STATE_RPT
        if (xTaskGetTickCount() - last_10hz >= pdMS_TO_TICKS(100)) {
            last_10hz = xTaskGetTickCount();

            can::gen::RtStateRpt rpt{};
            rpt.mode = g_mode_current.load();
            auto ss = g_steering.state();
            rpt.safety_state = (ss == rt::SteerState::STEER_ACTIVE) ? 0 :
                               (ss == rt::SteerState::STEER_FAULT)   ? 2 : 1;
            rpt.reversing    = g_reversing.load();
            rpt.rx_overflow  = static_cast<uint8_t>(g_can_high.rx_overflow_count());
            rpt.estop_reason = g_estop_reason.load();
            rpt.steer_state  = static_cast<uint8_t>(ss);
            rpt.task_health  = task_health_snapshot();
#ifdef BENCH_BUILD_ACKNOWLEDGED
            rpt.task_health |= 0x80;
#endif
            can::Frame state_fr{};
            const auto state_status = can::encode_frame(rpt, state_fr);
            if (state_status == can::gen::CodecStatus::Ok) {
                send_can_low(state_fr);
            } else {
                static uint32_t state_rpt_encode_fail_low = 0;
                state_rpt_encode_fail_low++;
                if (state_rpt_encode_fail_low == 1 || state_rpt_encode_fail_low % 100 == 0) {
                    ESP_LOGE(TAG, "RT_STATE_RPT Low encode failed: status=%d mode=%u reason=%u (count=%lu)",
                             static_cast<int>(state_status), rpt.mode, rpt.estop_reason, state_rpt_encode_fail_low);
                }
            }
        }
    }
}
// ── TASK 3: control_task (Core 1, Priority 5) ───────────────────────
[[noreturn]] void control_task(void*) {
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick_counter = 0;

    rt::g_brake_fallback.init(esp_timer_get_time());
    rt::g_safety_authority.reset(esp_timer_get_time());

    bool     m_estop_pending = false;
    uint8_t  m_estop_reason  = rt::kEstopReasonCanEstop;
    uint8_t  m_current_mode  = 0;
    bool     m_seb_takeover  = false;

    while (true) {
        g_task_alive_control_us.store(esp_timer_get_time(), std::memory_order_relaxed);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
        tick_counter++;
        const int64_t now = esp_timer_get_time();

        if (g_steering_estop_request.exchange(false)) {
            g_steering.start_estop(false);
        }
        if (g_steering_exit_request.exchange(false)) {
            g_steering.exit_estop();
            // If steering faulted (e.g. SES sync timeout during bench startup),
            // reset it to LISTEN_SYNC so the system can re-acquire without reboot.
            g_steering.reset_to_listen(static_cast<uint32_t>(now / 1000));
        }

        // 1. Drain Safety Events
        rt::SafetyEvent evt;
        bool had_estop_this_cycle = g_pending_estop_event.exchange(false);
        if (had_estop_this_cycle) {
            m_estop_pending = true;
            const uint8_t fallback_reason = g_estop_reason.load();
            m_estop_reason = fallback_reason != rt::kEstopReasonNone
                ? fallback_reason : rt::kEstopReasonCanEstop;
        }
        int16_t pending_mode = g_pending_mode_event.exchange(-1);
        if (pending_mode >= 0) {
            m_current_mode = static_cast<uint8_t>(pending_mode);
        }
        if (g_pending_safety_clear.exchange(false)) {
            m_estop_pending = false;
            m_estop_reason = rt::kEstopReasonNone;
            g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_HOST),
                                   std::memory_order_release);
            rt::HostDriveSnapshot zero{};
            if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
        }
        while (g_safety_evt_q && xQueueReceive(g_safety_evt_q, &evt, 0) == pdTRUE) {
            switch (evt.type) {
            case rt::SafetyEvent::ESTOP:
                m_estop_pending = true;
                m_estop_reason = evt.payload != rt::kEstopReasonNone
                    ? evt.payload : rt::kEstopReasonCanEstop;
                had_estop_this_cycle = true;
                break;
            case rt::SafetyEvent::MODE_CHANGE:
                m_current_mode = evt.payload;
                break;
            case rt::SafetyEvent::SAFETY_CLEAR:
                m_estop_pending = false;
                m_estop_reason = rt::kEstopReasonNone;
                g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_HOST),
                                       std::memory_order_release);
                rt::HostDriveSnapshot zero{};
                if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
                break;
            }
        }

        // 2. 0x011 Safety Stream Authority Acquisition & Supervision
        {
            const auto sst = rt::g_safety_authority.update(
                now, g_last_sys_safety_sts_us.load());
            if (sst.estop_latch_required && !m_estop_pending) {
                m_estop_pending = true;
                m_estop_reason = rt::kEstopReasonCanEstop;
                rt::diag().raise(etrike::diagnostics::DiagId::RtSysSafetyStsLoss,
                                 static_cast<std::uint16_t>(
                                     (now - g_last_sys_safety_sts_us.load()) / 1000));
            }
            // Bench-solo SYS-absent self-grant (developer override): a SYS that
            // has NEVER appeared on the bus must not silence RT — self-grant
            // SAFETY|MODE authority (AUTO) so Host drive commands still reach
            // the Low bus, mirroring SYS's MTR/SEB peer bypasses. A SYS that
            // was seen and then died (LOST) still latches ESTOP, and the first
            // valid 0x011 hands authority back to the real stream.
            const bool solo_sys_absent = (sst.sys_absent_fault && g_bench_solo_mode);
            if (sst.state == rt::SafetyStreamState::LOST || m_estop_pending
                || (sst.sys_absent_fault && !g_bench_solo_mode)) {
                g_ready_mask.fetch_and(
                    static_cast<uint8_t>(~(rt::READY_BIT_SAFETY | rt::READY_BIT_MODE | rt::READY_BIT_HOST)),
                    std::memory_order_release);
            } else if (solo_sys_absent) {
                g_ready_mask.fetch_or(
                    static_cast<uint8_t>(rt::READY_BIT_SAFETY | rt::READY_BIT_MODE),
                    std::memory_order_release);
                m_current_mode = static_cast<uint8_t>(can::Mode::Auto);
            } else if (sst.motion_authorized) {
                g_ready_mask.fetch_or(rt::READY_BIT_SAFETY, std::memory_order_release);
            } else {
                g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_SAFETY),
                                       std::memory_order_release);
            }

            if (sst.sys_absent_fault) {
                g_no_sys_authority.store(!g_bench_solo_mode, std::memory_order_relaxed);
                rt::diag().raise(etrike::diagnostics::DiagId::RtSysSafetyStsLoss,
                                 static_cast<std::uint16_t>(rt::kSysSafetyAcquireTimeoutUs / 1000));
            } else {
                const uint8_t mask = g_ready_mask.load(std::memory_order_acquire);
                g_no_sys_authority.store(!rt::is_sys_authority_ready(mask),
                                         std::memory_order_relaxed);
            }
        }
        g_mode_current.store(m_current_mode);

        // 3. Read input snapshots
        rt::HostDriveSnapshot host{};
        rt::ActuatorFeedbackSnapshot fbk{};
        if (g_host_cmd_mailbox) xQueuePeek(g_host_cmd_mailbox, &host, 0);
        if (g_feedback_mailbox) xQueuePeek(g_feedback_mailbox, &fbk, 0);

        const uint8_t cur_ready_mask = g_ready_mask.load(std::memory_order_acquire);
        if (!rt::is_motion_ready(cur_ready_mask)) {
            host.speed_mmps = 0;
            host.yaw_rate_mrad_s = 0;
        }

        // 4. Resolve Kinematics & Limits
        rt::ResolvedSetpoint sp{};
        g_resolver.resolve({host.speed_mmps, host.yaw_rate_mrad_s}, sp);
        sp.cmd_gear = host.gear_override;

        can::gen::HostSteerCmd direct_steer{};
        direct_steer.steer_angle_0_1deg = static_cast<int16_t>(g_direct_steer_angle_0_1deg.load());
        direct_steer.angle_valid = g_direct_steer_valid.load();
        rt::apply_fresh_direct_steering(direct_steer,
            g_last_direct_steer_us.load(), now, sp);

        uint32_t obs = g_obstacle_mm.load();
        sp.motor_speed_mmps = rt::PhysicsModel::obstacle_limit(sp.motor_speed_mmps, obs);

#if ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 3
        g_calc_speed.update(sp.motor_speed_mmps, 0.01f);
#endif

#if ETRIKE_RT_ENCODERS == 1
        float enc_speed = rt::encoder_read_speed_mmps(0, 0.01f);
        g_encoder_speed_mmps.store(static_cast<int32_t>(enc_speed));
        rt::encoder_reset(0);
#endif

        // Dynamic angle clamp
        {
            float max_deg = rt::compute_dynamic_limit(static_cast<float>(std::abs(sp.motor_speed_mmps)));
            int32_t limit_mdeg = static_cast<int32_t>(max_deg * 1000.0f);
            sp.steer_angle_mdeg = std::clamp(sp.steer_angle_mdeg, -limit_mdeg, limit_mdeg);
        }

        int32_t obs_kpa = rt::PhysicsModel::obstacle_to_kpa(obs);
        int32_t bk = rt::brake_arbitrate(obs_kpa, g_brake_request_kpa.load());

        if (auto* drv = rt::can_low_driver()) {
            drv->service_recovery(now);
        }
        update_low_can_tx_admission(now);
        bool startup_grace = (now < int64_t(shared::kStartupGracePeriodMs) * 1000);

        if (m_current_mode == uint8_t(can::Mode::Auto)
            && std::abs(sp.motor_speed_mmps) > shared::kLowSpeedThreshMmps) {
            g_last_nonzero_cmd_us.store(now, std::memory_order_relaxed);
        }

        rt::SafetyResult sr = run_safety_checks(now, startup_grace, obs,
                                                m_estop_pending, m_current_mode, m_seb_takeover);
        if (m_estop_pending && sr.estop_reason == rt::kEstopReasonCanEstop) {
            sr.estop_reason = m_estop_reason;
        }

        // SEB Brake Fallback Supervisor
        {
            rt::SebFallbackInput fb_in;
            fb_in.now_us = now;
            const int64_t last_hb = g_last_sys_hb_us.load();
            const bool hb_lost = (!g_bench_solo_mode && (last_hb < 0
                || (now - last_hb) > int64_t(rt::kHeartbeatTimeoutMsSys) * 1000));
            fb_in.sys_hb_fresh = !hb_lost;
            const int64_t last_7b9 = g_last_0x7B9_rx_us.load();
            fb_in.sys_0x7B9_observed =
                (last_7b9 >= 0 && (now - last_7b9) < 100'000);
            fb_in.startup_grace_active = startup_grace;
            const auto fb_out = rt::g_brake_fallback.update(fb_in);
            m_seb_takeover = fb_out.emergency_tx_0x7B9;
        }
        g_seb_takeover.store(m_seb_takeover);
        g_mtr_unavailable.store(rt::g_mtr_health.mtr_unavailable, std::memory_order_relaxed);
        g_brake_fallback_state.store(static_cast<uint8_t>(rt::g_brake_fallback.state()),
                                     std::memory_order_relaxed);

        if (sr.estop_reason != 0) {
            g_estop_reason.store(sr.estop_reason);
        } else if (!sr.zero_setpoints && !sr.disable_steering
                   && m_current_mode != uint8_t(can::Mode::Estop)) {
            g_estop_reason.store(rt::kEstopReasonNone);
        }

        if (sr.zero_setpoints) {
            rt::HostDriveSnapshot zero{};
            if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
            sp = {};
#if ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 3
            g_calc_speed.reset();
#endif

            const bool is_active_local_trip = (sr.obstacle_triggered ||
                                               sr.estop_reason == rt::kEstopReasonFollowingError ||
                                               sr.estop_reason == rt::kEstopReasonBusOff ||
                                               sr.estop_reason == rt::kEstopReasonInternal ||
                                               m_seb_takeover);
            const bool sys_clear_in_progress = g_sys_clear_in_progress.load(std::memory_order_relaxed);
            if (is_active_local_trip && !sys_clear_in_progress && can_send_estop()) {
                can::Frame estop_frame;
                can::gen::SafetyEstop estop_msg{};
                if (can::gen::encode_safety_estop(estop_msg, estop_frame) == can::gen::CodecStatus::Ok) {
                    post_gateway_frame(estop_frame);
                }
            }
        }
        if (sr.brake_kpa) bk = sr.brake_kpa;
        if (sr.disable_steering) {
            g_steering.start_estop(sr.obstacle_triggered);
            if (sr.obstacle_triggered) {
                g_steering.set_estop_hold_time(now / 1000);
            }
        }

        g_brake_kpa_to_send.store(bk);

        [[maybe_unused]] int32_t measured_speed_mmps = 0;
#if ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 1
        measured_speed_mmps = g_mtr_motor_command_speed_mmps.load();
#elif ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 2
        measured_speed_mmps = g_encoder_speed_mmps.load();
#elif ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 3
        measured_speed_mmps = g_calc_speed.get();
#endif

#if ETRIKE_RT_PID_MODE > 0
        {
            int16_t pid_out = 0;
            g_speed_ctrl.update_shadow_pid(sp.motor_speed_mmps, measured_speed_mmps, 0.01f, pid_out);
            g_pid_output_mmps.store(pid_out);
            g_last_speed_setpoint_mmps.store(sp.motor_speed_mmps);

#if ETRIKE_RT_PID_MODE == 2
            if (sp.motor_speed_mmps != 0) {
                sp.motor_speed_mmps += pid_out;
                sp.motor_speed_mmps = std::clamp<int32_t>(sp.motor_speed_mmps,
                    -shared::kMaxSpeedRevMmps, shared::kMaxSpeedFwdMmps);
            }
#endif
        }
#endif

        if (m_current_mode == uint8_t(can::Mode::Auto)) {
            g_steering.set_target(sp.steer_angle_mdeg, g_mtr_motor_command_speed_mmps.load());
        }

        g_last_cmd_angle_0_1deg.store(static_cast<int16_t>(sp.steer_angle_mdeg / 100));
        g_reversing.store(sp.reversing);

        // 5. Publish Motion Output Snapshot
        rt::MotionOutputSnapshot motion_out{};
        const bool motion_mode = (m_current_mode == uint8_t(can::Mode::Auto));
        auto ss = g_steering.state();
        bool drive_allowed = motion_mode
            && (ss == rt::SteerState::STEER_ACTIVE
                || ss == rt::SteerState::ESTOP_RAMP_TO_ZERO
                || ss == rt::SteerState::ESTOP_HOLD_THEN_SILENT);

        motion_out.motor_speed_mmps = drive_allowed ? sp.motor_speed_mmps : 0;
        if (!drive_allowed) {
            motion_out.motor_gear = uint8_t(can::Gear::N);
        } else if (sp.cmd_gear != 0) {
            motion_out.motor_gear = sp.cmd_gear;
        } else if (sp.motor_speed_mmps > 0) {
            motion_out.motor_gear = uint8_t(can::Gear::D);
        } else if (sp.motor_speed_mmps < 0) {
            motion_out.motor_gear = uint8_t(can::Gear::R);
        } else {
            motion_out.motor_gear = uint8_t(can::Gear::N);
        }

        motion_out.steer_angle_0_1deg = static_cast<int16_t>(sp.steer_angle_mdeg / 100);
        motion_out.brake_kpa = bk;
        motion_out.current_mode = m_current_mode;
        motion_out.estop_reason = g_estop_reason.load();
        motion_out.safety_state = (ss == rt::SteerState::STEER_ACTIVE) ? 0 :
                                  (ss == rt::SteerState::STEER_FAULT)   ? 2 : 1;
        motion_out.seb_emergency_takeover = m_seb_takeover;
        motion_out.steer_command_enable = (m_current_mode != uint8_t(can::Mode::Manual));
        motion_out.reversing = sp.reversing;

        if (g_motion_output_mailbox) xQueueOverwrite(g_motion_output_mailbox, &motion_out);

        monitor_can_bus_off();

        // 6. Tick-Divided 10 Hz Staleness Check (Every 10 ticks)
        if (tick_counter % 10 == 0) {
            if (g_watchdog.is_stale(now)) {
                if (!g_bench_solo_mode) {
                    // Production: advertise loss, zero the command, request a
                    // steering ESTOP.
                    static int64_t last_stale_log_us = 0;
                    rt::diag().raise(etrike::diagnostics::DiagId::RtHostDriveCmdStale,
                                     static_cast<std::uint16_t>((now - g_watchdog.last_feed()) / 1000));
                    if (now - last_stale_log_us > 2'000'000) {
                        last_stale_log_us = now;
                        ESP_LOGW(TAG, "Command stale (no host drive)");
                    }
                    g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_HOST),
                                           std::memory_order_release);
                    rt::HostDriveSnapshot zero{};
                    if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
                    g_steering_estop_request.store(true);
                } else {
                    // Bench solo: fail-safe zero the command without latching an
                    // ESTOP or stopping steering. A resumed 0x300 stream re-arms
                    // READY_BIT_HOST on the next frame.
                    g_ready_mask.fetch_and(static_cast<uint8_t>(~rt::READY_BIT_HOST),
                                           std::memory_order_release);
                    rt::HostDriveSnapshot zero{};
                    if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
                }
            }
        }

        // 7. Tick-Divided 2 Hz Heartbeats (Every 50 ticks)
        if (tick_counter % 50 == 0) {
            uint8_t hf = 0;
            bool sys_alive = (g_last_sys_hb_us.load() > 0
                && (now - g_last_sys_hb_us.load()) <= int64_t(rt::kHeartbeatTimeoutMsSys) * 1000);
            bool host_alive = (g_last_host_hb_us.load() > 0
                && (now - g_last_host_hb_us.load()) <= int64_t(shared::kHeartbeatTimeoutMsHost) * 1000);
#ifdef BENCH_BUILD_ACKNOWLEDGED
            if (sys_alive) hf |= rt::kHbHealthBitHeartbeatOk;
#else
            if (sys_alive && (host_alive || g_bench_solo_mode)) hf |= rt::kHbHealthBitHeartbeatOk;
#endif
            if (g_steering.state() == rt::SteerState::ESTOP_RAMP_TO_ZERO
                || g_steering.state() == rt::SteerState::ESTOP_HOLD_THEN_SILENT
                || m_current_mode == uint8_t(can::Mode::Estop))
                hf |= rt::kHbHealthBitEstopActive;
            if (m_current_mode == uint8_t(can::Mode::Auto))
                hf |= rt::kHbHealthBitModeAuto;
            if (g_can_high_healthy.load(std::memory_order_relaxed))
                hf |= rt::kHbHealthBitCanOk;

            g_heartbeat_flags.store(hf, std::memory_order_relaxed);

        }
    }
}

// ── Application Entry Point ─────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "RT ESP32-S3 boot (3-Task Architecture)");

    // Evaluate System Run Mode
    {
        bool override_pin_low = false;
        if (SYSTEM_RUN_MODE == 1) {
            gpio_set_direction(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN), GPIO_MODE_INPUT);
            gpio_pullup_en(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN));
            vTaskDelay(pdMS_TO_TICKS(10));
            override_pin_low = (gpio_get_level(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN)) == 0);
        }

        const etrike::BypassState bypass = etrike::evaluate_run_mode_bypasses(SYSTEM_RUN_MODE, override_pin_low);
        g_bench_solo_mode = bypass.bench_solo_mode;
        g_bypass_eps_sync = bypass.bypass_eps_sync;
        g_bypass_seb_sync = bypass.bypass_seb_sync;
        g_bypass_mtr_absent = bypass.bypass_mtr_absent;

        if (g_bench_solo_mode) {
            ESP_LOGE(TAG, "***********************************");
            ESP_LOGE(TAG, "* DEVELOPER BYPASS ACTIVE         *");
            ESP_LOGE(TAG, "* BYPASSING SAFETY SYNC CHECKS!   *");
            ESP_LOGE(TAG, "* SYS-ABSENT: RT SELF-GRANTS AUTO *");
            ESP_LOGE(TAG, "***********************************");
        } else if (SYSTEM_RUN_MODE == 1) {
            ESP_LOGI(TAG, "Prototype mode: Override pin not jumped. Enforcing safety.");
        } else {
            ESP_LOGI(TAG, "Production mode: Safety checks enforced.");
        }
    }

    rt::can_low_init();
    if (auto* drv = rt::can_low_driver()) {
        drv->set_tx_admission(true);
        ESP_LOGI(TAG, "Low CAN transport initialized");
    }
    bool has_high_can = g_can_high.init();
    g_high_can_present.store(has_high_can, std::memory_order_relaxed);
    g_steering.init();
    g_heartbeat.init();
    g_watchdog.init();
#if ETRIKE_RT_ENCODERS == 1
    rt::encoder_init();
#endif

    // Mailboxes (Depth 1 Overwrite)
    g_host_cmd_mailbox      = xQueueCreate(1, sizeof(rt::HostDriveSnapshot));
    g_feedback_mailbox      = xQueueCreate(1, sizeof(rt::ActuatorFeedbackSnapshot));
    g_motion_output_mailbox = xQueueCreate(1, sizeof(rt::MotionOutputSnapshot));

    // Bounded Gateway Queue (Depth 8)
    g_high_to_low_gw_q      = xQueueCreate(8, sizeof(rt::GatewayFrame));

    // Safety Event Queue (Depth 16)
    g_safety_evt_q          = xQueueCreate(16, sizeof(rt::SafetyEvent));

    // Spawn only 3 tasks pinned to cores
    {
        TaskHandle_t h_can_high = nullptr;
        xTaskCreatePinnedToCore(can_high_task, "can_high", 4096, nullptr, 4, &h_can_high, 0);
        if (h_can_high) {
            g_can_high.set_rx_task_handle(h_can_high);
        }
    }

    xTaskCreatePinnedToCore(can_low_task, "can_low", 4096, nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(control_task, "control", 4096, nullptr, 5, nullptr, 1);

    ESP_LOGI(TAG, "Ready — 3 tasks pinned");
    vTaskDelete(nullptr);
}
