// RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway.
// Architecture: architecture.md §7.  8 FreeRTOS tasks.

// Runtime System Mode Configuration
#include "system_mode.h"

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
#include "can_driver_twai.h"
#include "can_rx_router.h"
#include "brake_arbitration.h"
#include "seb_request.h"

static const char* TAG = "rt";

// ═══════════════════════════════════════════════════════════════════════
// Definitions for all extern declarations in rt_state.h.
// This is the single translation unit that owns the global state.
// ═══════════════════════════════════════════════════════════════════════

// ── CAN drivers ────────────────────────────────────────────────────
rt::Mcp2515Driver g_can_high;

// ── Application objects ────────────────────────────────────────────
rt::PhysicsModel    g_physics;
rt::SpeedController g_speed_ctrl;
rt::SteeringControl g_steering;
rt::DualHeartbeat   g_heartbeat;
rt::CmdWatchdog     g_watchdog;

// ── Safety event queue ─────────────────────────────────────────────
QueueHandle_t g_safety_evt_q = nullptr;  // depth 16, SafetyEvent
std::atomic<bool>     g_pending_estop_event{false};
std::atomic<int16_t>  g_pending_mode_event{-1};
std::atomic<uint32_t> g_safety_event_drops{0};

// ── Shared state (atomics for sensor / latest-value data) ───────────
std::atomic<int32_t>  g_brake_request_kpa{0};
std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
std::atomic<int32_t>  g_ses_angle_0_1deg{INT16_MIN};
std::atomic<uint8_t>  g_ses_angle_status{0};
std::atomic<int32_t>  g_brake_kpa_to_send{0};
std::atomic<int32_t>  g_mtr_actual_speed_mmps{0};

// ── Derived state (written by control, read by tx tasks) ────────────
std::atomic<uint8_t>  g_mode_current{0};
std::atomic<bool>     g_seb_takeover{false};

// ── Heartbeat tracking ─────────────────────────────────────────────
std::atomic<int64_t>  g_last_sys_hb_us{0};
std::atomic<int64_t>  g_last_host_hb_us{0};
std::atomic<int64_t>  g_last_estop_sent_us{0};

// ── Per-task alive counters for multi-task watchdog (gap #5) ──────
static std::atomic<uint32_t> g_alive_control{0};
static std::atomic<uint32_t> g_alive_dispatch{0};
static std::atomic<uint32_t> g_alive_tx_low{0};
static std::atomic<uint32_t> g_alive_tx_high{0};
static void check_task_watchdog();

// ── ESTOP reason atomic (written by dispatch/safety/health, read by tx) ─
std::atomic<uint8_t>  g_estop_reason{0};

// ── Telemetry atomics ──────────────────────────────────────────────
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

// ── Queues ─────────────────────────────────────────────────────────
QueueHandle_t g_can_rx_low_q  = nullptr;
QueueHandle_t g_can_rx_high_q = nullptr;
QueueHandle_t g_cmd_q         = nullptr;
QueueHandle_t g_setpoint_q    = nullptr;
QueueHandle_t g_gw_tx_low_q   = nullptr;
QueueHandle_t g_gw_tx_high_q  = nullptr;

// ── CAN RX — unified (prio 5) ─────────────────────────────────────
using CanReceiveFn = bool (*)(can::Frame&, uint32_t);
struct CanRxParams { CanReceiveFn receive; QueueHandle_t queue; rt::Mcp2515Driver* overflow_drv = nullptr; };

static bool low_receive(can::Frame& fr, uint32_t timeout) {
    auto* drv = rt::can_low_driver();
    return drv && drv->receive(fr, timeout);
}

static bool high_receive(can::Frame& fr, uint32_t timeout) {
    return g_can_high.receive(fr, timeout);
}

[[noreturn]] static void task_can_rx(void* pv) {
    auto& p = *static_cast<CanRxParams*>(pv);
    can::Frame fr;
    while (1) {
        if (p.receive(fr, 100)) {
            // ESTOP (0x001) gets priority: use send-to-front to skip queue
            if (fr.id == can::kIdSafetyEstop) {
                xQueueSendToFront(p.queue, &fr, 0);
            } else if (xQueueSend(p.queue, &fr, 0) != pdTRUE && p.overflow_drv) {
                p.overflow_drv->record_rx_overflow();
                static bool warned = false;
                if (!warned) {
                    ESP_LOGW(TAG, "High CAN RX queue overflow — check RT_STATE_RPT byte 3");
                    warned = true;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));  // yield when silent or uninitialized
        }
    }
}

// ── Dispatch (extracted to can_dispatch.h) ──────────────────────────
#include "can_dispatch.h"
// ── Safety monitor (extracted to safety_monitor.h) ──────────────────
#include "safety_monitor.h"

// ── CAN TX helper — checks return, logs failure, detects recovery ────
static uint32_t g_can_tx_fail_low = 0, g_can_tx_fail_high = 0;
static uint32_t g_can_tx_ok_low = 0, g_can_tx_ok_high = 0;
static bool g_can_tx_had_fail_low = false, g_can_tx_had_fail_high = false;
static bool send_can_low(can::Frame& fr) {
    auto* drv = rt::can_low_driver();
    if (!drv || !drv->send(fr)) {
        g_can_tx_fail_low++;
        if (!g_can_tx_had_fail_low) { ESP_LOGW(TAG, "Low CAN TX failed"); g_can_tx_had_fail_low = true; }
        return false;
    }
    if (g_can_tx_had_fail_low) { ESP_LOGI(TAG, "Low CAN TX recovered — fail=%lu ok=%lu", g_can_tx_fail_low, g_can_tx_ok_low); g_can_tx_had_fail_low = false; }
    g_can_tx_ok_low++;
    return true;
}
static bool send_can_high(can::Frame& fr) {
    if (!g_can_high.send(fr)) {
        g_can_tx_fail_high++;
        if (!g_can_tx_had_fail_high) { ESP_LOGW(TAG, "High CAN TX failed"); g_can_tx_had_fail_high = true; }
        return false;
    }
    if (g_can_tx_had_fail_high) { ESP_LOGI(TAG, "High CAN TX recovered — fail=%lu ok=%lu", g_can_tx_fail_high, g_can_tx_ok_high); g_can_tx_had_fail_high = false; }
    g_can_tx_ok_high++;
    return true;
}

// ── CAN bus health monitor (extracted to can_health.h) ──────────────
#include "can_health.h"

// ── Control (prio 4, 100 Hz) ───────────────────────────────────────
[[noreturn]] static void t_control(void*) {
    TickType_t per = pdMS_TO_TICKS(10), last = xTaskGetTickCount();
    can::HostDriveCmd cmd{};

    // Local state drained from safety event queue (architecture principle #1).
    bool     m_estop_pending = false;
    uint8_t  m_current_mode  = 0;   // 0=Manual, 1=Auto, 2=Estop
    bool     m_seb_takeover  = false;

    while (1) {
        g_alive_control.store(xTaskGetTickCount(), std::memory_order_relaxed);
        // ── Drain bounded safety events and their overflow fallbacks ─
        rt::SafetyEvent evt;
        bool had_estop_this_cycle = g_pending_estop_event.exchange(false);
        if (had_estop_this_cycle) {
            m_estop_pending = true;
        }
        int16_t pending_mode = g_pending_mode_event.exchange(-1);
        if (pending_mode >= 0) {
            m_current_mode = static_cast<uint8_t>(pending_mode);
            if (pending_mode != int16_t(can::Mode::Estop) && !had_estop_this_cycle) {
                m_estop_pending = false;
            }
        }
        while (xQueueReceive(g_safety_evt_q, &evt, 0) == pdTRUE) {
            switch (evt.type) {
            case rt::SafetyEvent::ESTOP:
                m_estop_pending = true;
                had_estop_this_cycle = true;
                break;
            case rt::SafetyEvent::MODE_CHANGE:
                m_current_mode = evt.payload;
                // Only clear ESTOP on mode change if no ESTOP arrived in this
                // drain cycle. Prevents periodic Auto broadcasts from cancelling
                // a valid ESTOP that arrived in the same queue window (bug 4.9).
                if (evt.payload != uint8_t(can::Mode::Estop) && !had_estop_this_cycle) {
                    m_estop_pending = false;
                }
                break;
            }
        }
        // Publish mode after event drain for read-heavy tx tasks (read at 50Hz/10Hz).
        // SEB takeover is published immediately after safety checks below.
        g_mode_current.store(m_current_mode);

        if (xQueueReceive(g_cmd_q, &cmd, 0) != pdTRUE)
            cmd = {0, 0};

        rt::ResolvedSetpoint sp;
        g_physics.resolve({cmd.speed_mmps, cmd.yaw_rate_mrad_s}, sp);
        sp.cmd_gear = cmd.gear;  // propagate CAN gear override

        uint32_t obs = g_obstacle_mm.load();
        sp.motor_speed_mmps = rt::PhysicsModel::obstacle_limit(sp.motor_speed_mmps, obs);

        // ── Dynamic angle clamp (arch §7.6, fix #6) ─────────────────
        {
            float max_deg = rt::compute_dynamic_limit(static_cast<float>(std::abs(sp.motor_speed_mmps)));
            int32_t limit_mdeg = static_cast<int32_t>(max_deg * 1000.0f);
            sp.steer_angle_mdeg = std::clamp(sp.steer_angle_mdeg, -limit_mdeg, limit_mdeg);
        }

        int32_t obs_kpa = rt::PhysicsModel::obstacle_to_kpa(obs);
        int32_t bk = rt::brake_arbitrate(obs_kpa, g_brake_request_kpa.load());

        // ── Safety checks ──────────────────────────────────────────
        int64_t const now = esp_timer_get_time();
        bool startup_grace = (now < int64_t(shared::kStartupGracePeriodMs) * 1000);

        rt::SafetyResult sr = run_safety_checks(now, startup_grace, obs,
                                                  m_estop_pending, m_current_mode, m_seb_takeover);
        g_seb_takeover.store(m_seb_takeover);

        // Propagate ESTOP reason from safety checks to telemetry atomic.
        // Internal checks (heartbeat, following-error, obstacle) set it in
        // SafetyResult. External triggers (0x001, L3 faults, bus-off) were
        // already set by dispatch/can_health directly on g_estop_reason.
        // Reset to None when no ESTOP condition is active (mode != Estop
        // and no safety check triggered zero_setpoints/disable_steering).
        if (sr.estop_reason != 0) {
            g_estop_reason.store(sr.estop_reason);
        } else if (!sr.zero_setpoints && !sr.disable_steering
                   && m_current_mode != uint8_t(can::Mode::Estop)) {
            g_estop_reason.store(can::kEstopReasonNone);
        }

        if (sr.zero_setpoints) {
            cmd = {0, 0};
            xQueueOverwrite(g_cmd_q, &cmd);
            sp = {};

            // Originate 0x001 ESTOP on internal fault detection (fix #5)
            // Send on both buses — SYS, EPS-C, SEB, Host all listen for 0x001
            // Gap #14: rate-limited to prevent bus flooding
            if (can_send_estop()) {
                can::Frame estop_frame;
                estop_frame.id = can::kIdSafetyEstop;
                estop_frame.dlc = 0;
                xQueueSendToFront(g_gw_tx_low_q, &estop_frame, pdMS_TO_TICKS(10));
                xQueueSendToFront(g_gw_tx_high_q, &estop_frame, pdMS_TO_TICKS(10));
            }
        }
        if (sr.brake_kpa) bk = sr.brake_kpa;
        // Steering ESTOP: state machine handles ramp-to-zero (gap C3).
        // Obstacle-triggered → hold-then-silent (arch §7.6, gap #9).
        // Non-obstacle triggers → ramp to 0° at 20°/s.
        if (sr.disable_steering) {
            g_steering.start_estop(sr.obstacle_triggered);
            if (sr.obstacle_triggered) {
                g_steering.set_estop_hold_time(esp_timer_get_time() / 1000);
            }
        }

        g_brake_kpa_to_send.store(bk);  // consumed by can_tx_low → 0x205 at 50 Hz (fix C7)
        xQueueOverwrite(g_setpoint_q, &sp);

        // ── Shadow PID (telemetry only — arch §7.6, gap #5) ───────
        {
            int16_t pid_out = 0;
            g_speed_ctrl.update_shadow_pid(sp.motor_speed_mmps,
                                        g_mtr_actual_speed_mmps.load(),
                                        0.01f, pid_out);
            g_pid_output_mmps.store(pid_out);
            g_last_speed_setpoint_mmps.store(sp.motor_speed_mmps);

#ifdef CONFIG_ENABLE_ACTIVE_PID
            // ── ACTIVE PID CONTROL ────────────────────────────────
            // ENABLED: PID correction injected into motor setpoint.
            // PREREQUISITES BEFORE ENABLING (all must be true):
            //   1. Rear motor encoder physically installed on GPIO 1/2
            //   2. Quadrature phasing verified (swap A/B if reversed)
            //   3. CONFIG_ENABLE_ENCODERS defined (enables PCNT hardware)
            //   4. Speed reading validated on 0x220 RT_PID_RPT telemetry
            //   5. No-load bench test: PID tracks setpoint without oscillation
            //   6. THEN define CONFIG_ENABLE_ACTIVE_PID
            //
            // SAFETY: measured==0 guard in update_shadow_pid() prevents
            // runaway if encoder fails (wire break → zero reading).
            // Additional guards needed before production:
            //   - RPM plausibility: measured cannot jump > X mm/s per tick
            //   - Zero-speed timeout: if measured==0 for >500ms while
            //     setpoint>0 → fault (encoder failure, not stationary)
            //   - Encoder fault output monitoring (if encoder has FLT pin)
            if (g_mtr_actual_speed_mmps.load() != 0) {
                sp.motor_speed_mmps += pid_out;
                sp.motor_speed_mmps = std::clamp(sp.motor_speed_mmps,
                    -shared::kMaxSpeedRevMmps, shared::kMaxSpeedFwdMmps);
            }
#endif // CONFIG_ENABLE_ACTIVE_PID
        }

        // ── Capture state for telemetry (fix #1, #5) ──────────────
        g_last_cmd_angle_0_1deg.store(static_cast<int16_t>(sp.steer_angle_mdeg / 100));
        g_reversing.store(sp.reversing);

        // External watchdog kick — toggled at 100 Hz (TPS3850, 100ms window)
        static bool wdt_toggle = false;
        wdt_toggle = !wdt_toggle;
        gpio_set_level(static_cast<gpio_num_t>(rt::kWdtToggleGpio), wdt_toggle ? 1 : 0);

        monitor_can_bus_off();

        vTaskDelayUntil(&last, per);
    }
}

static void send_seb_req(can::CanDriver& drv, can::Frame& fr,
                         can::VcuSebReq seb, uint8_t& rolling_counter) {
    seb.control_enable = 1;
    seb.roll_cnt_enable = 1;
    seb.checksum_enable = 1;
    seb.rolling_counter = rolling_counter;
    rolling_counter = (rolling_counter + 1) & 0x0F;
    seb.to_frame(fr);
    drv.send(fr);
}

// ── CAN TX low (prio 3) ────────────────────────────────────────────
[[noreturn]] static void t_can_tx_low(void*) {
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t t100 = last_wake, t50 = last_wake;
    rt::ResolvedSetpoint sp{};
    can::Frame fr; can::Frame gw;
    while (1) {
        g_alive_tx_low.store(xTaskGetTickCount(), std::memory_order_relaxed);
        auto* drv = rt::can_low_driver();
        if (!drv) { vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5)); continue; }

        if (xTaskGetTickCount() - t100 >= pdMS_TO_TICKS(10)) {
            t100 = xTaskGetTickCount();
            // Gate: in MANUAL mode, RT does not command actuators
            if (g_mode_current.load() == uint8_t(can::Mode::Manual)) continue;
            if (xQueuePeek(g_setpoint_q, &sp, 0) == pdTRUE) {
                g_steering.set_target(sp.steer_angle_mdeg, g_mtr_actual_speed_mmps.load());
                // Drive motor lockout: only send 0x204 when steering is ready (arch §7.6).
                // Block during boot, listen-sync, and fault — send {0,N} instead of silence
                // so MTR's 200ms staleness check does not false-trigger.
                auto ss = g_steering.state();
                bool drive_allowed = (ss == rt::SteerState::STEER_ACTIVE
                                   || ss == rt::SteerState::ESTOP_RAMP_TO_ZERO
                                   || ss == rt::SteerState::ESTOP_HOLD_THEN_SILENT);
                int32_t speed_out = drive_allowed ? sp.motor_speed_mmps : 0;
                uint8_t gear_out;
                if (!drive_allowed) {
                    gear_out = uint8_t(can::Gear::N);
                } else if (sp.cmd_gear != 0) {
                    gear_out = sp.cmd_gear;  // CAN override
                } else if (sp.motor_speed_mmps > 0) {
                    gear_out = uint8_t(can::Gear::D);
                } else if (sp.motor_speed_mmps < 0) {
                    gear_out = uint8_t(can::Gear::R);
                } else {
                    gear_out = uint8_t(can::Gear::N);
                }
                can::RtDriveCmd{speed_out, gear_out}.to_frame(fr);
                send_can_low(fr);
            }
        }
        if (xTaskGetTickCount() - t50 >= pdMS_TO_TICKS(20)) {
            t50 = xTaskGetTickCount();
            // 0x205 RT_BRAKE_CMD at 50 Hz (arch §7.4, fix C7).
            // Architecture §2.3: RT→SYS, AUTO only. Suppress in MANUAL — SYS handles brake directly.
            uint8_t mode_now = g_mode_current.load();
            if (mode_now != uint8_t(can::Mode::Manual)) {
                can::RtBrakeCmd{g_brake_kpa_to_send.load()}.to_frame(fr);
                send_can_low(fr);
            }
            // 0x169 VCU_SES_REQ at 50 Hz — steering state machine gates transmission.
            // Transmits in ACTIVE, ESTOP_RAMP_TO_ZERO, and ESTOP_HOLD_THEN_SILENT.
            // Silent in BOOT_WAIT, LISTEN_SYNC, FAULT, and MANUAL mode.
            // Allow in AUTO (active steering) and ESTOP (centering ramp per §7.6 gap #3).
            // Only block in MANUAL — EPS-C runs standalone, RT must not command.
            if (g_mode_current.load() != uint8_t(can::Mode::Manual)) {
                can::VcuSesReq ses;
                int64_t now_ms = esp_timer_get_time() / 1000;
                if (g_steering.tick(g_ses_angle_0_1deg.load(), g_ses_angle_status.load(),
                                    now_ms, ses)) {
                    ses.to_frame(fr); send_can_low(fr);
                }
            }

            // Gap #12: SEB brake takeover — RT sends 0x7B9 at 50Hz on SYS heartbeat loss.
            // Must be in same 50Hz block (not a separate timing check — was dead code).
            static uint8_t seb_roll = 0;
            bool seb_takeover = g_seb_takeover.load(std::memory_order_relaxed);
            if (seb_takeover) {
                send_seb_req(*drv, fr, rt::make_seb_takeover_req(), seb_roll);
            }

            // Gap #12 completion: Option D - RT sends 0x7B9 directly in AUTO mode.
            // Architecture §6.2: 1-hop from kinematics, no cross-node sync needed.
            // NOTE: SYS MUST gate its own 0x7B9 on mode (stop sending in AUTO).
            // Uses Pressure Mode for kPa-based braking, Stroke Mode when no brake.
            // Only active when NOT in SEB takeover (takeover has priority).
            // Safety: Only send when steering is ACTIVE. In ESTOP/FAULT states,
            // SYS is the 0x7B9 authority — RT must suppress to avoid dual-sender
            // bus collision and brake=0 override (bugs 4.1, 4.2).
            auto ss = g_steering.state();
            if (!seb_takeover
                && g_mode_current.load() == uint8_t(can::Mode::Auto)
                && ss == rt::SteerState::STEER_ACTIVE) {
                int32_t brake = g_brake_kpa_to_send.load();
                send_seb_req(*drv, fr, rt::make_seb_auto_req(brake), seb_roll);
            }
        }

        if (xQueueReceive(g_gw_tx_low_q, &gw, 0) == pdTRUE) drv->send(gw);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5));
    }
}

// ── CAN TX high (prio 3) ───────────────────────────────────────────
// Gateway frames are drained at 100 Hz (10ms inner loop) while
// periodic telemetry is produced at 10 Hz (100ms outer loop).
[[noreturn]] static void t_can_tx_high(void*) {
    can::Frame gw;
    can::Frame fr;
    while (1) {
        g_alive_tx_high.store(xTaskGetTickCount(), std::memory_order_relaxed);
        // Drain gateway queue every 10ms (100 Hz) — was 100ms
        for (int i = 0; i < 10; i++) {
            while (xQueueReceive(g_gw_tx_high_q, &gw, 0) == pdTRUE) {
                send_can_high(gw);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 0x210 RT_STATE_RPT — 10 Hz (arch §7.4)
        can::RtStateRpt rpt;
        rpt.mode         = g_mode_current.load();
        auto ss = g_steering.state();
        rpt.safety_state = (ss == rt::SteerState::STEER_ACTIVE) ? 0 :
                           (ss == rt::SteerState::STEER_FAULT)   ? 2 : 1;
                           // 0=Normal, 1=InternalEstop(ramp/hold), 2=Fault
        rpt.reversing    = g_reversing.load();
        rpt.rx_overflow  = static_cast<uint8_t>(g_can_high.rx_overflow_count());
        rpt.estop_reason = g_estop_reason.load();
        rpt.steer_state  = static_cast<uint8_t>(ss);
        // Task health bitmask: bit set = task alive within 500ms
        {
            TickType_t now = xTaskGetTickCount();
            rpt.task_health = 0;
            if (now - g_alive_control.load(std::memory_order_relaxed) <= pdMS_TO_TICKS(500)) rpt.task_health |= 0x01;
            if (now - g_alive_dispatch.load(std::memory_order_relaxed) <= pdMS_TO_TICKS(500)) rpt.task_health |= 0x02;
            if (now - g_alive_tx_low.load(std::memory_order_relaxed) <= pdMS_TO_TICKS(500)) rpt.task_health |= 0x04;
            if (now - g_alive_tx_high.load(std::memory_order_relaxed) <= pdMS_TO_TICKS(500)) rpt.task_health |= 0x08;
#ifdef BENCH_BUILD_ACKNOWLEDGED
            rpt.task_health |= 0x80;  // bit 7: bench build indicator
#endif
        }
        rpt.to_frame(fr);
        static uint32_t rpt_fail_count = 0;
        if (!g_can_high.send(fr)) {
            rpt_fail_count++;
            if (rpt_fail_count == 1 || rpt_fail_count % 100 == 0) {
                ESP_LOGW(TAG, "MCP2515 RT_STATE_RPT send failed (count=%lu)", rpt_fail_count);
            }
        } else if (rpt_fail_count > 0) {
            ESP_LOGI(TAG, "MCP2515 RT_STATE_RPT send recovered after %lu failures", rpt_fail_count);
            rpt_fail_count = 0;
        }
        // Also send on low bus so SYS can read RT safety_state for takeover detection
        auto* drv_low = rt::can_low_driver();
        if (drv_low) drv_low->send(fr);

        // 0x310 STEER_DIAG — 10 Hz (v0.0.4: EPS-C telemetry for Host)
        // Rescale: SES_Test source (0.0078125 A/bit, 0.5 degC/bit) → STEER_DIAG dest (0.01 A/bit, 0.1 degC/bit)
        {
            int16_t angle = g_ses_angle_0_1deg.load() + rt::kSbwAngleOffset;
            uint8_t fault = (g_ses_error_status.load() > 0) ? 1 : 0;
            uint16_t mtr_curr = uint16_t((g_ses_motor_current.load() * 25) / 32);  // ×0.78125
            uint16_t ecu_tmp = uint16_t(g_ses_ecu_temp.load() * 5);               // ×5
            can::SteerDiag{angle, fault, mtr_curr, ecu_tmp, 0}.to_frame(fr);
            static uint32_t diag_fail_count = 0;
            if (!g_can_high.send(fr)) {
                diag_fail_count++;
                if (diag_fail_count == 1 || diag_fail_count % 100 == 0) {
                    ESP_LOGW(TAG, "MCP2515 STEER_DIAG send failed (count=%lu)", diag_fail_count);
                }
            } else if (diag_fail_count > 0) {
                ESP_LOGI(TAG, "MCP2515 STEER_DIAG send recovered after %lu failures", diag_fail_count);
                diag_fail_count = 0;
            }
        }

        // 0x311 BRAKE_DIAG — 10 Hz (v0.0.4: SEB telemetry for Host)
        // Rescale: SEB_Test source (0.0078125 A/bit, 0.5 degC/bit) → BRAKE_DIAG dest (0.01 A/bit, 0.1 degC/bit)
        {
            uint16_t seb_pressure = g_seb_pressure_raw.load();
            uint8_t  seb_fault    = (g_seb_error_status.load() > 0) ? 1 : 0;
            uint16_t mtr_curr = uint16_t((g_seb_motor_current.load() * 25) / 32); // ×0.78125
            // SEB: factor 0.5 offset -40 → BRAKE_DIAG: factor 0.1 offset 0.
            // Use signed intermediate to prevent wrap on sub-zero temps (bug B3).
            int32_t ecu_tmp_raw = int32_t(g_seb_ecu_temp_c.load()) * 5 - 400;
            uint16_t ecu_tmp = ecu_tmp_raw < 0 ? 0 : uint16_t(ecu_tmp_raw);
            can::BrakeDiag{seb_pressure, seb_fault, mtr_curr, ecu_tmp, 0}.to_frame(fr);
            send_can_high(fr);
        }

        // 0x220 RT_PID_RPT — 10 Hz (shadow PID telemetry, arch §7.6, gap #5)
        {
            int16_t setpoint = static_cast<int16_t>(std::clamp(
                g_last_speed_setpoint_mmps.load(), int32_t(-32768), int32_t(32767)));
            int16_t measured = g_mtr_actual_speed_mmps.load();
            int16_t pid      = g_pid_output_mmps.load();
            can::RtPidRpt{setpoint, measured, pid}.to_frame(fr);
            send_can_high(fr);
        }

    }
}

// ── Watchdog (prio 1, 10 Hz) ───────────────────────────────────────
[[noreturn]] static void t_watchdog(void*) {
    TickType_t per = pdMS_TO_TICKS(100), last = xTaskGetTickCount();
    while (1) {
        check_task_watchdog();
        if (g_watchdog.is_stale(esp_timer_get_time())) {
            ESP_LOGW(TAG, "Command stale");
            can::HostDriveCmd zero{};
            xQueueOverwrite(g_cmd_q, &zero);
            g_steering.start_estop(false);  // ramp to 0° (gap C3, replaces disable flag)
        }
        vTaskDelayUntil(&last, per);
    }
}

static void check_task_watchdog() {
    TickType_t now = xTaskGetTickCount();
    auto stale = [now](std::atomic<uint32_t>& a, const char* name) {
        if (now - a.load(std::memory_order_relaxed) > pdMS_TO_TICKS(500))
            ESP_LOGE(TAG, "Task %s stalled >500ms — hardware WDT may fire", name);
    };
    stale(g_alive_control,  "control");
    stale(g_alive_dispatch, "dispatch");
    stale(g_alive_tx_low,   "tx_low");
    stale(g_alive_tx_high,  "tx_high");
}

// ── Heartbeat (prio 1, 2 Hz) ───────────────────────────────────────
[[noreturn]] static void t_heartbeat(void*) {
    TickType_t per = pdMS_TO_TICKS(rt::kHeartbeatIntervalMs), last = xTaskGetTickCount();
    can::Frame fr;
    while (1) {
        // Compute health flags for heartbeat byte 1
        uint8_t hf = 0;
        {
            int64_t now_us = esp_timer_get_time();
            bool sys_alive = (g_last_sys_hb_us.load() > 0
                && (now_us - g_last_sys_hb_us.load()) <= int64_t(rt::kHeartbeatTimeoutMsSys) * 1000);
            bool host_alive = (g_last_host_hb_us.load() > 0
                && (now_us - g_last_host_hb_us.load()) <= int64_t(shared::kHeartbeatTimeoutMsHost) * 1000);
            if (sys_alive && host_alive) hf |= can::kHbHealthBitHeartbeatOk;
            if (g_steering.state() == rt::SteerState::ESTOP_RAMP_TO_ZERO
                || g_steering.state() == rt::SteerState::ESTOP_HOLD_THEN_SILENT
                || g_mode_current.load() == uint8_t(can::Mode::Estop))
                hf |= can::kHbHealthBitEstopActive;
            if (g_mode_current.load() == uint8_t(can::Mode::Auto))
                hf |= can::kHbHealthBitModeAuto;
            if (!g_can_high.bus_off())
                hf |= can::kHbHealthBitCanOk;
        }
        g_heartbeat.tick_low(fr, hf);
        auto* drv = rt::can_low_driver();
        if (drv) send_can_low(fr);
        g_heartbeat.tick_high(fr, hf);
        static uint32_t hb_fail_count = 0;
        if (!g_can_high.send(fr)) {
            hb_fail_count++;
            if (hb_fail_count == 1 || hb_fail_count % 100 == 0) {
                ESP_LOGW(TAG, "MCP2515 heartbeat send failed (count=%lu)", hb_fail_count);
            }
        } else if (hb_fail_count > 0) {
            ESP_LOGI(TAG, "MCP2515 heartbeat send recovered after %lu failures", hb_fail_count);
            hb_fail_count = 0;
        }
        vTaskDelayUntil(&last, per);
    }
}

// ───────────────────────────────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "RT ESP32-S3 boot");
    
    // Evaluate System Run Mode
    if (SYSTEM_RUN_MODE == 2) {
        ESP_LOGE(TAG, "***********************************");
        ESP_LOGE(TAG, "* PURE SOFTWARE SIMULATION MODE   *");
        ESP_LOGE(TAG, "* BYPASSING SAFETY SYNC CHECKS!   *");
        ESP_LOGE(TAG, "***********************************");
        g_bench_solo_mode = true;
        g_bypass_eps_sync = true;
        g_bypass_seb_sync = true;
        g_bypass_mtr_absent = true;
    } else if (SYSTEM_RUN_MODE == 1) {
        gpio_set_direction(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN), GPIO_MODE_INPUT);
        gpio_pullup_en(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN));
        
        // Brief delay to let pull-up stabilize
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (gpio_get_level(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN)) == 0) {
            ESP_LOGE(TAG, "***********************************");
            ESP_LOGE(TAG, "* HARDWARE OVERRIDE PIN DETECTED! *");
            ESP_LOGE(TAG, "* BYPASSING SAFETY SYNC CHECKS!   *");
            ESP_LOGE(TAG, "***********************************");
            g_bench_solo_mode = true;
            g_bypass_eps_sync = true;
            g_bypass_seb_sync = true;
            g_bypass_mtr_absent = true;
        } else {
            ESP_LOGI(TAG, "Prototype mode: Override pin not jumped. Enforcing safety.");
        }
    } else {
        ESP_LOGI(TAG, "Production mode: Safety checks enforced.");
    }

    rt::can_low_init();
    bool has_high_can = g_can_high.init();
    g_steering.init();
    g_heartbeat.init();
    g_watchdog.init();

    // External watchdog GPIO — toggled by control_task at 100 Hz (TPS3850 or equiv)
    gpio_set_direction(static_cast<gpio_num_t>(rt::kWdtToggleGpio), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(rt::kWdtToggleGpio), 0);

    g_can_rx_low_q  = xQueueCreate(16, sizeof(can::Frame));
    g_can_rx_high_q = xQueueCreate(16, sizeof(can::Frame));
    g_cmd_q         = xQueueCreate( 1, sizeof(can::HostDriveCmd));       // overwrite queue — only latest value matters
    g_setpoint_q    = xQueueCreate( 1, sizeof(rt::ResolvedSetpoint));    // overwrite queue
    g_gw_tx_low_q   = xQueueCreate( 8, sizeof(can::Frame));
    g_gw_tx_high_q  = xQueueCreate( 8, sizeof(can::Frame));
    g_safety_evt_q  = xQueueCreate(16, sizeof(rt::SafetyEvent));

    int task_count = 0;

    static CanRxParams rx_low_par  = { low_receive,  nullptr };
    rx_low_par.queue  = g_can_rx_low_q;
    xTaskCreate(task_can_rx, "rx_low",  4096, &rx_low_par,  5, nullptr);
    task_count++;

    if (has_high_can) {
        static CanRxParams rx_high_par = { high_receive, nullptr, &g_can_high };
        rx_high_par.queue = g_can_rx_high_q;
        xTaskCreate(task_can_rx, "rx_high", 4096, &rx_high_par, 5, nullptr);
        task_count++;
    } else {
        ESP_LOGW(TAG, "High CAN (MCP2515) not available — rx_high/tx_high tasks skipped");
    }

    xTaskCreate(t_dispatch,    "dispatch",4096, nullptr, 4, nullptr); task_count++;
    xTaskCreate(t_control,     "control", 4096, nullptr, 4, nullptr); task_count++;
    xTaskCreate(t_can_tx_low,  "tx_low",  3072, nullptr, 3, nullptr); task_count++;

    if (has_high_can) {
        xTaskCreate(t_can_tx_high, "tx_high", 3072, nullptr, 3, nullptr);
        task_count++;
    }

    xTaskCreate(t_watchdog,    "watchdog",4096, nullptr, 1, nullptr); task_count++;
    xTaskCreate(t_heartbeat,   "hb",      3072, nullptr, 1, nullptr); task_count++;

    ESP_LOGI(TAG, "Ready — %d tasks", task_count);
    vTaskDelete(nullptr);
}
