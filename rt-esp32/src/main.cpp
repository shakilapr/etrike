// RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway.
// Architecture: architecture.md §7.  8 FreeRTOS tasks.
#include <atomic>
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "config.h"
#include "can/can_protocol.h"
#include "can_driver_twai.h"
#include "can_driver_mcp2515.h"
#include "can_rx_router.h"
#include "physics_model.h"
#include "steering_control.h"
#include "brake_arbitration.h"
#include "heartbeat.h"
#include "watchdog.h"

static const char* TAG = "rt";

// ── CAN drivers ────────────────────────────────────────────────────
static rt::Mcp2515Driver g_can_high;

// ── Application objects ────────────────────────────────────────────
static rt::PhysicsModel    g_physics;
static rt::SteeringControl g_steering;
static rt::DualHeartbeat   g_heartbeat;
static rt::CmdWatchdog     g_watchdog;

// ── Shared state (atomics for cross-task access) ───────────────────
static std::atomic<int32_t>  g_brake_request_kpa{0};
static std::atomic<bool>     g_estop_flag{false};
static std::atomic<uint8_t>  g_mode_from_sys{0};
static std::atomic<uint32_t> g_obstacle_mm{UINT32_MAX};
static std::atomic<int32_t>  g_ses_angle_raw{INT16_MIN};    // steering angle from 0x201 (fix C6)
static std::atomic<uint8_t>  g_ses_angle_status{0};         // 0x201 byte0 bit0: alignment (gap C2)
static std::atomic<int32_t>  g_brake_kpa_to_send{0};        // resolved brake kPa for 0x205 (fix C7)
static std::atomic<int32_t>  g_mtr_actual_speed_mmps{0};    // measured speed from 0x206 MTR (fix M4)

// ── Heartbeat tracking (written by dispatch, checked by control) ─
static std::atomic<int64_t>  g_last_sys_hb_us{0};      // 0x7FE SYS heartbeat timestamp
static std::atomic<int64_t>  g_last_host_hb_us{0};    // 0x7FC Host heartbeat timestamp

// ── Telemetry atomics (written by control/dispatch, read by tx tasks) ─
static std::atomic<int16_t>  g_last_cmd_angle_raw{0};   // commanded steering angle in 0.1° (fix #5)
static std::atomic<bool>     g_reversing{false};         // reversing flag from resolved setpoint (fix #1)
static std::atomic<uint16_t> g_ses_motor_current{0};     // 0x6FA motor current raw (bytes 1-2 LE, scale 0.0078125 A/bit)
static std::atomic<uint16_t> g_ses_ecu_temp{0};          // 0x6FA ECU temperature raw (bytes 3-4 LE, scale 0.5 °C/bit)
static std::atomic<uint16_t> g_ses_pow_volt{0};          // 0x6FA supply voltage raw (bytes 5-6 LE, scale 0.00390625 V/bit)
static std::atomic<uint8_t>  g_ses_error_status{0};     // 0x201 byte0 bits 6-7: EPS-C error level
static std::atomic<uint16_t> g_seb_pressure_raw{0};     // 0x721 byte3: SEB pressure (0.05 MPa/bit)
static std::atomic<uint8_t>  g_seb_error_status{0};     // 0x721 byte0 bits 6-7: SEB error level
static std::atomic<uint16_t> g_seb_motor_current{0};    // 0x6FB motor current raw
static std::atomic<uint16_t> g_seb_ecu_temp_c{0};       // 0x6FB ECU temp raw

// ── Queues ─────────────────────────────────────────────────────────
static QueueHandle_t g_can_rx_low_q  = nullptr;  // 16 deep
static QueueHandle_t g_can_rx_high_q = nullptr;  // 16 deep
static QueueHandle_t g_cmd_q         = nullptr;  //  4 deep, overwrite
static QueueHandle_t g_setpoint_q    = nullptr;  //  4 deep, overwrite
static QueueHandle_t g_gw_tx_low_q   = nullptr;  //  8 deep
static QueueHandle_t g_gw_tx_high_q  = nullptr;  //  8 deep

// ── CAN RX — unified (prio 5) ─────────────────────────────────────
using CanReceiveFn = bool (*)(can::Frame&, uint32_t);
struct CanRxParams { CanReceiveFn receive; QueueHandle_t queue; };

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
        if (p.receive(fr, 100))
            xQueueSend(p.queue, &fr, 0);
    }
}

// ── Dispatch + gateway (prio 4) ────────────────────────────────────
struct DispatchContext {
    can::Frame gw_lo;
    can::Frame gw_hi;
    can::HostDriveCmd cmd;
    int32_t brake_req_kpa;
    bool estop_flag;
    uint8_t mode_from_sys;
    int16_t steer_feedback_angle;
    uint8_t steer_angle_status;  // 0x201 byte0 bit0: angle alignment (gap C2)
    bool has_mode = false;    // set true when 0x110 mode received (fix CRITICAL falsy check)
    bool has_brake = false;   // set true when 0x301 brake received (fix CRITICAL falsy check)
    bool has_cmd = false;     // set true when 0x300 drive cmd received (fix falsy check)
};

static void process_frame(const can::Frame& fr, bool is_high, DispatchContext& ctx) {
    if (fr.id == can::kIdSysHeartbeat) {
        g_last_sys_hb_us.store(esp_timer_get_time());
    } else if (fr.id == can::kIdHostHeartbeat) {
        g_last_host_hb_us.store(esp_timer_get_time());
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
    rt::route_frame(fr, is_high, q);

    // ── Post-routing handlers ───────────────────────────────────────
    if (fr.id == can::kIdSafetyEstop) { ctx.gw_lo = fr; ctx.gw_hi = fr; }
    if (fr.id == can::kIdSyntreeEpsStatus) {
        g_ses_angle_raw.store(ctx.steer_feedback_angle);
        g_ses_angle_status.store(ctx.steer_angle_status);  // alignment bit (gap C2)
        g_ses_error_status.store((fr.data[0] >> 6) & 0x03);  // bits 6-7: error level
    }
    if (fr.id == can::kIdHostObstacleDist && is_high) { g_obstacle_mm.store(fr.u32_at(0)); }
    if (fr.id == can::kIdMtrMotorFbk && !is_high) { g_mtr_actual_speed_mmps.store(fr.i16_at(0)); }

    // 0x202 SES_ErrInfo — L3 fault bits (arch §7.3, fix #3)
    if (fr.id == can::kIdSyntreeEpsErrInfo && !is_high) {
        uint8_t angle_faults  = fr.data[1] & 0x0F;           // bits 8-11: angle sensor P/S O/C+AF
        uint8_t torque_faults = (fr.data[2] >> 2) & 0x0F;    // bits 18-21: torque sensor T1/T2
        if (angle_faults || torque_faults) {
            ESP_LOGW(TAG, "SES_ErrInfo L3 fault: angle=0x%X torque=0x%X", angle_faults, torque_faults);
            ctx.estop_flag = true;
        }
    }
    // 0x203 SES_Version — log SW/HW once (arch §7.3, fix #3)
    if (fr.id == can::kIdSyntreeEpsVersion && !is_high) {
        static bool ses_version_logged = false;
        if (!ses_version_logged) {
            ESP_LOGI(TAG, "SES_Version: SW=%02X.%02X HW=%02X.%02X",
                     fr.data[0], fr.data[1], fr.data[2], fr.data[3]);
            ses_version_logged = true;
        }
    }
    // 0x6FA SES_Test — motor current + ECU temp + supply voltage (arch §7.3, fix #3)
    if (fr.id == can::kIdSyntreeEpsTest && !is_high) {
        // Byte layout (Motorola LSB / little-endian):
        //   byte0=reserved, byte1-2=SES_MtrCurt (i16 LE, 0.0078125 A/bit),
        //   byte3-4=SES_ECUTemp (u16 LE, 0.5 °C/bit),
        //   byte5-6=SES_PowVolt (u16 LE, 0.00390625 V/bit), byte7=reserved
        int16_t  mc_raw = int16_t((uint16_t(fr.data[2]) << 8) | fr.data[1]);
        uint16_t et_raw = (uint16_t(fr.data[4]) << 8) | fr.data[3];
        uint16_t pv_raw = (uint16_t(fr.data[6]) << 8) | fr.data[5];
        g_ses_motor_current.store(mc_raw);
        g_ses_ecu_temp.store(et_raw);
        g_ses_pow_volt.store(pv_raw);
        float mc_a = mc_raw * 0.0078125f;
        float et_c = et_raw * 0.5f;
        float pv_v = pv_raw * 0.00390625f;
        if (et_c > 85.0f)  ESP_LOGW(TAG, "SES ECU temp high: %.1f°C", et_c);
        if (mc_a > 30.0f)  ESP_LOGW(TAG, "SES motor current high: %.1f A", mc_a);
        if (pv_v < 10.0f)  ESP_LOGW(TAG, "SES supply voltage low: %.2f V", pv_v);
    }
    // 0x6FB SEB_Test — motor current + ECU temp (arch §7.3, for 0x311 BRAKE_DIAG)
    if (fr.id == can::kIdSyntreeSebTest && !is_high) {
        int16_t  mc_raw = int16_t((uint16_t(fr.data[2]) << 8) | fr.data[1]);
        uint16_t et_raw = (uint16_t(fr.data[4]) << 8) | fr.data[3];
        g_seb_motor_current.store(mc_raw);
        g_seb_ecu_temp_c.store(et_raw);
    }
    // 0x721 SEB_STATUS — capture pressure + error for 0x311 BRAKE_DIAG
    if (fr.id == can::kIdSyntreeSebStatus && !is_high) {
        g_seb_pressure_raw.store(fr.data[3]);  // byte 3 = SEB_Pressure_Value (0.05 MPa/bit)
        g_seb_error_status.store((fr.data[0] >> 6) & 0x03);
    }
    // Track reception of 0x110 mode and 0x301 brake (fix #3: 0=Manual/0=release are valid)
    if (fr.id == can::kIdSysModeCmd) {
        ctx.has_mode = true;
    }
    if (fr.id == can::kIdHostBrakeReq) {
        ctx.has_brake = true;
    }
    if (fr.id == can::kIdHostDriveCmd) {
        ctx.has_cmd = true;
    }
}

[[noreturn]] static void t_dispatch(void*) {
    can::Frame fr;
    while (1) {
        if (xQueueReceive(g_can_rx_low_q, &fr, 0) != pdTRUE &&
            xQueueReceive(g_can_rx_high_q, &fr, 0) != pdTRUE) {
            xQueueReceive(g_can_rx_low_q, &fr, portMAX_DELAY);
        }

        bool is_high = (fr.id == can::kIdHostDriveCmd || fr.id == can::kIdHostBrakeReq
                     || fr.id == can::kIdHostLightCmd || fr.id == can::kIdHostHeartbeat
                     || fr.id == can::kIdHostObstacleDist);

        DispatchContext ctx{};
        process_frame(fr, is_high, ctx);

        if (ctx.gw_lo.id)  xQueueSend(g_gw_tx_low_q,  &ctx.gw_lo, 0);
        if (ctx.gw_hi.id)  xQueueSend(g_gw_tx_high_q, &ctx.gw_hi, 0);
        if (ctx.estop_flag)     g_estop_flag.store(true);
        if (ctx.has_mode) {  // has_mode flag: 0=Manual is valid (fix CRITICAL falsy check)
            g_mode_from_sys.store(ctx.mode_from_sys);
            if (ctx.mode_from_sys != uint8_t(can::Mode::Estop)) {
                g_estop_flag.store(false);
                g_steering.exit_estop();  // ESTOP → AUTO/MANUAL transition (gap C3)
            }
        }
        if (ctx.has_brake)   g_brake_request_kpa.store(ctx.brake_req_kpa);  // has_brake flag: 0=release is valid (fix CRITICAL falsy check)
        if (ctx.has_cmd) {
            xQueueOverwrite(g_cmd_q, &ctx.cmd);
            g_watchdog.feed(esp_timer_get_time());
            g_steering.exit_estop();  // new drive cmd → clear any ESTOP hold (gap C3)
        }
    }
}

// ── Safety checks (used by t_control) ─────────────────────────────
struct SafetyResult { bool zero_setpoints; int32_t brake_kpa; bool disable_steering; };

static SafetyResult run_safety_checks(int64_t now, bool startup_grace) {
    SafetyResult r{};

    // 1. ESTOP flag from CAN 0x001 — zero everything, max brake, disable steering (fix H8)
    if (g_estop_flag.exchange(false)) {  // atomic read+clear, no TOCTOU race
        ESP_LOGW(TAG, "ESTOP flag set — zeroing setpoints, max brake");
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
    }

    // 2. Mode from SYS 0x110 — zero setpoints in ESTOP mode
    if (g_mode_from_sys.load() == uint8_t(can::Mode::Estop)) {
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
    }

    if (startup_grace) return r;

    // 3. SYS heartbeat timeout (architecture §8.6: 200ms)
    int64_t sys_hb = g_last_sys_hb_us.load();
    if (sys_hb > 0 && (now - sys_hb) > int64_t(rt::kHeartbeatTimeoutMsSys) * 1000) {
        ESP_LOGW(TAG, "SYS heartbeat timeout — zeroing setpoints");
        r.zero_setpoints = true;
    }

    // 4. Host heartbeat timeout (arch §7.6: 1500ms → assisted stop)
    int64_t jetson_hb = g_last_host_hb_us.load();
    if (jetson_hb > 0 && (now - jetson_hb) > int64_t(shared::kHeartbeatTimeoutMsHost) * 1000) {
        ESP_LOGW(TAG, "Host heartbeat timeout — assisted stop brake=2000kPa");
        r.zero_setpoints = true;
        g_brake_request_kpa.store(shared::kAssistStopKpa);
    }

    // 5. Steering following-error check (arch §7.6, fix #5)
    // abs(cmd_angle - actual_angle) > threshold (speed-scaled, max(2°, 0.25×dynamic_limit)) for >300ms → ESTOP
    // Only check when not already zeroing or disabled
    // Only check following error when steering is actively commanding (gap C3)
    if (!r.zero_setpoints && g_steering.state() == rt::SteerState::ACTIVE) {
        static int steer_follow_err_ticks = 0;
        int16_t cmd_raw    = g_last_cmd_angle_raw.load();  // 0.1° units
        int16_t actual_raw = g_ses_angle_raw.load();        // 0.1° units from 0x201
        if (actual_raw != INT16_MIN) {                      // valid reading
            int32_t diff = int32_t(cmd_raw) - int32_t(actual_raw);
            int32_t err_mdeg = (diff >= 0 ? diff : -diff) * 100;  // convert 0.1° → mdeg
            float threshold_deg = rt::compute_following_error_threshold(g_mtr_actual_speed_mmps.load());
            int32_t kThresholdMdeg = static_cast<int32_t>(threshold_deg * 1000.0f);
            constexpr int kTickLimit = rt::kSteerFollowingErrMs / (1000 / rt::kControlLoopHz);
            if (err_mdeg > kThresholdMdeg) {
                if (++steer_follow_err_ticks >= kTickLimit) {
                    ESP_LOGW(TAG, "Steer follow err >%.1f° for >%dms — ESTOP",
                             static_cast<double>(threshold_deg), rt::kSteerFollowingErrMs);
                    r.zero_setpoints = true;
                    r.brake_kpa = shared::kMaxBrakeKpa;
                    r.disable_steering = true;
                }
            } else {
                steer_follow_err_ticks = 0;
            }
        }
    }

    return r;
}

// ── Control (prio 4, 100 Hz) ───────────────────────────────────────
[[noreturn]] static void t_control(void*) {
    TickType_t per = pdMS_TO_TICKS(10), last = xTaskGetTickCount();
    can::HostDriveCmd cmd{};
    while (1) {
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

        // Obstacle→kPa: 300mm→5000, 3000mm→0, linear between
        int32_t obs_kpa;
        if (obs <= shared::kObstacleStopMM) {
            obs_kpa = shared::kObstacleMaxKpa;
        } else if (obs >= shared::kObstacleClearMM) {
            obs_kpa = 0;
        } else {
            float t = static_cast<float>(obs - shared::kObstacleStopMM)
                    / static_cast<float>(shared::kObstacleClearMM - shared::kObstacleStopMM);
            obs_kpa = static_cast<int32_t>(shared::kObstacleMaxKpa * (1.0f - t));
        }
        int32_t bk = rt::brake_arbitrate(obs_kpa, g_brake_request_kpa.load());

        // ── Safety checks ──────────────────────────────────────────
        int64_t const now = esp_timer_get_time();
        bool startup_grace = (now < int64_t(shared::kStartupGracePeriodMs) * 1000);

        SafetyResult sr = run_safety_checks(now, startup_grace);
        if (sr.zero_setpoints) {
            cmd = {0, 0};
            xQueueOverwrite(g_cmd_q, &cmd);
            sp = {};

            // Originate 0x001 ESTOP on internal fault detection (fix #5)
            // Send on both buses — SYS, EPS-C, SEB, Host all listen for 0x001
            can::Frame estop_frame;
            estop_frame.id = can::kIdSafetyEstop;
            estop_frame.dlc = 0;
            xQueueSend(g_gw_tx_low_q, &estop_frame, 0);
            xQueueSend(g_gw_tx_high_q, &estop_frame, 0);
        }
        if (sr.brake_kpa) bk = sr.brake_kpa;
        // Steering ESTOP: state machine handles ramp-to-zero (gap C3).
        // Non-obstacle triggers use ramp; obstacle hold-then-silent reserved for future.
        if (sr.disable_steering) {
            g_steering.start_estop(false);  // non-obstacle → ramp to 0° at 20°/s
        }

        g_brake_kpa_to_send.store(bk);  // consumed by can_tx_low → 0x205 at 50 Hz (fix C7)
        xQueueOverwrite(g_setpoint_q, &sp);

        // ── Capture state for telemetry (fix #1, #5) ──────────────
        g_last_cmd_angle_raw.store(static_cast<int16_t>(sp.steer_angle_mdeg / 100));
        g_reversing.store(sp.reversing);

        // External watchdog kick — toggled at 100 Hz (TPS3850, 100ms window)
        static bool wdt_toggle = false;
        wdt_toggle = !wdt_toggle;
        gpio_set_level(static_cast<gpio_num_t>(rt::kWdtToggleGpio), wdt_toggle ? 1 : 0);

        // ── CAN bus-off monitoring — check both buses at 1 Hz ──────
        static int bus_check_ctr = 0;
        static int bus_off_count_low = 0, bus_off_count_high = 0;
        if (++bus_check_ctr >= 100) {
            bus_check_ctr = 0;
            // Low bus (TWAI)
            {
                uint8_t tec = 0, rec = 0;
                auto* drv = rt::can_low_driver();
                if (drv) drv->get_error_counters(tec, rec);
                if (tec > 128 && tec <= 255)
                    ESP_LOGW(TAG, "Low CAN error-warning: TEC=%u REC=%u", tec, rec);
                if (tec > 255) {
                    ESP_LOGE(TAG, "Low CAN bus-off: TEC=%u REC=%u", tec, rec);
                    bus_off_count_low++;
                    if (bus_off_count_low >= 5) {
                        ESP_LOGE(TAG, "Low CAN bus-off persistent — triggering ESTOP");
                        can::Frame ef; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                        xQueueSend(g_gw_tx_low_q, &ef, 0);
                        xQueueSend(g_gw_tx_high_q, &ef, 0);
                    }
                    if (drv) drv->init();  // attempt recovery
                } else { bus_off_count_low = 0; }
            }
            // High bus (MCP2515)
            {
                uint8_t tec = 0, rec = 0;
                g_can_high.get_error_counters(tec, rec);
                if (tec > 128 && tec <= 255)
                    ESP_LOGW(TAG, "High CAN error-warning: TEC=%u REC=%u", tec, rec);
                if (tec > 255) {
                    ESP_LOGE(TAG, "High CAN bus-off: TEC=%u REC=%u", tec, rec);
                    bus_off_count_high++;
                    if (bus_off_count_high >= 5) {
                        ESP_LOGE(TAG, "High CAN bus-off persistent — zeroing setpoints");
                        can::HostDriveCmd zero{};
                        xQueueOverwrite(g_cmd_q, &zero);
                        g_steering.start_estop(false);
                    }
                    g_can_high.init();  // attempt recovery
                } else { bus_off_count_high = 0; }
            }
        }

        vTaskDelayUntil(&last, per);
    }
}

// ── CAN TX low (prio 3) ────────────────────────────────────────────
[[noreturn]] static void t_can_tx_low(void*) {
    TickType_t t100 = xTaskGetTickCount(), t50 = t100;
    rt::ResolvedSetpoint sp{};
    can::Frame fr; can::Frame gw;
    while (1) {
        auto* drv = rt::can_low_driver();
        if (!drv) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

        if (xTaskGetTickCount() - t100 >= pdMS_TO_TICKS(10)) {
            t100 = xTaskGetTickCount();
            if (xQueuePeek(g_setpoint_q, &sp, 0) == pdTRUE) {
                g_steering.set_target(sp.steer_angle_mdeg, g_mtr_actual_speed_mmps.load());
                uint8_t gear = (sp.cmd_gear != 0) ? sp.cmd_gear  // CAN override
                             : (sp.motor_speed_mmps > 0) ? uint8_t(can::Gear::D)
                             : (sp.motor_speed_mmps < 0) ? uint8_t(can::Gear::R)
                             : uint8_t(can::Gear::N);
                can::RtDriveCmd{sp.motor_speed_mmps, gear}.to_frame(fr);
                drv->send(fr);
            }
        }
        if (xTaskGetTickCount() - t50 >= pdMS_TO_TICKS(20)) {
            t50 = xTaskGetTickCount();
            // 0x205 RT_BRAKE_CMD at 50 Hz (arch §7.4, fix C7)
            can::RtBrakeCmd{g_brake_kpa_to_send.load()}.to_frame(fr);
            drv->send(fr);
            // 0x169 VCU_SES_REQ at 50 Hz — steering state machine gates transmission.
            // Transmits in ACTIVE, ESTOP_RAMP_TO_ZERO, and ESTOP_HOLD_THEN_SILENT.
            // Silent in BOOT_WAIT, LISTEN_SYNC, FAULT, and MANUAL mode.
            // Allow in AUTO (active steering) and ESTOP (centering ramp per §7.6 gap #3).
            // Only block in MANUAL — EPS-C runs standalone, RT must not command.
            if (g_mode_from_sys.load() != uint8_t(can::Mode::Manual)) {
                can::VcuSesReq ses;
                uint32_t now_ms = esp_timer_get_time() / 1000;
                if (g_steering.tick(g_ses_angle_raw.load(), g_ses_angle_status.load(),
                                    now_ms, ses)) {
                    ses.to_frame(fr); drv->send(fr);
                }
            }
        }
        if (xQueueReceive(g_gw_tx_low_q, &gw, 0) == pdTRUE) drv->send(gw);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ── CAN TX high (prio 3) ───────────────────────────────────────────
[[noreturn]] static void t_can_tx_high(void*) {
    can::Frame gw;
    can::Frame fr;   // temp frame for telemetry
    TickType_t last = xTaskGetTickCount();
    while (1) {
        while (xQueueReceive(g_gw_tx_high_q, &gw, 0) == pdTRUE)
            g_can_high.send(gw);

        // 0x210 RT_STATE_RPT — 10 Hz (arch §7.4, fix #1)
        can::RtStateRpt rpt;
        rpt.mode        = g_mode_from_sys.load();
        rpt.steer_valid = (g_steering.state() == rt::SteerState::ACTIVE);
        rpt.reversing   = g_reversing.load();
        rpt.to_frame(fr);
        g_can_high.send(fr);

        // 0x400 HOST_OBSTACLE_DIST — 10 Hz (Host→RT perception data)
        can::HostObstacleDist{g_obstacle_mm.load()}.to_frame(fr);
        g_can_high.send(fr);

        // 0x310 STEER_DIAG — 10 Hz (v0.0.4: EPS-C telemetry for Host)
        {
            int16_t angle = g_ses_angle_raw.load();
            uint8_t fault = (g_ses_error_status.load() > 0) ? 1 : 0;
            can::SteerDiag{angle, fault, g_ses_motor_current.load(), g_ses_ecu_temp.load(), 0}.to_frame(fr);
            g_can_high.send(fr);
        }

        // 0x311 BRAKE_DIAG — 10 Hz (v0.0.4: SEB telemetry for Host)
        {
            uint16_t seb_pressure = g_seb_pressure_raw.load();
            uint8_t  seb_fault    = (g_seb_error_status.load() > 0) ? 1 : 0;
            can::BrakeDiag{seb_pressure, seb_fault, g_seb_motor_current.load(), g_seb_ecu_temp_c.load(), 0}.to_frame(fr);
            g_can_high.send(fr);
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}

// ── Watchdog (prio 1, 10 Hz) ───────────────────────────────────────
[[noreturn]] static void t_watchdog(void*) {
    TickType_t per = pdMS_TO_TICKS(100), last = xTaskGetTickCount();
    while (1) {
        if (g_watchdog.is_stale(esp_timer_get_time())) {
            ESP_LOGW(TAG, "Command stale");
            can::HostDriveCmd zero{};
            xQueueOverwrite(g_cmd_q, &zero);
            g_steering.start_estop(false);  // ramp to 0° (gap C3, replaces disable flag)
        }
        vTaskDelayUntil(&last, per);
    }
}

// ── Heartbeat (prio 1, 2 Hz) ───────────────────────────────────────
[[noreturn]] static void t_heartbeat(void*) {
    TickType_t per = pdMS_TO_TICKS(rt::kHeartbeatIntervalMs), last = xTaskGetTickCount();
    can::Frame fr;
    while (1) {
        g_heartbeat.tick_low(fr);
        auto* drv = rt::can_low_driver();
        if (drv) drv->send(fr);
        g_heartbeat.tick_high(fr);
        g_can_high.send(fr);
        vTaskDelayUntil(&last, per);
    }
}

// ───────────────────────────────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "RT ESP32-S3 boot");

    rt::can_low_init();
    g_can_high.init();
    g_steering.init();
    g_heartbeat.init();
    g_watchdog.init();

    // External watchdog GPIO — toggled by control_task at 100 Hz (TPS3850 or equiv)
    gpio_set_direction(static_cast<gpio_num_t>(rt::kWdtToggleGpio), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(rt::kWdtToggleGpio), 0);

    g_can_rx_low_q  = xQueueCreate(16, sizeof(can::Frame));
    g_can_rx_high_q = xQueueCreate(16, sizeof(can::Frame));
    g_cmd_q         = xQueueCreate( 4, sizeof(can::HostDriveCmd));
    g_setpoint_q    = xQueueCreate( 4, sizeof(rt::ResolvedSetpoint));
    g_gw_tx_low_q   = xQueueCreate( 8, sizeof(can::Frame));
    g_gw_tx_high_q  = xQueueCreate( 8, sizeof(can::Frame));

    static CanRxParams rx_low_par  = { low_receive,  nullptr };
    static CanRxParams rx_high_par = { high_receive, nullptr };
    rx_low_par.queue  = g_can_rx_low_q;
    rx_high_par.queue = g_can_rx_high_q;
    xTaskCreate(task_can_rx, "rx_low",  4096, &rx_low_par,  5, nullptr);
    xTaskCreate(task_can_rx, "rx_high", 4096, &rx_high_par, 5, nullptr);
    xTaskCreate(t_dispatch,    "dispatch",4096, nullptr, 4, nullptr);
    xTaskCreate(t_control,     "control", 4096, nullptr, 4, nullptr);
    xTaskCreate(t_can_tx_low,  "tx_low",  3072, nullptr, 3, nullptr);
    xTaskCreate(t_can_tx_high, "tx_high", 3072, nullptr, 3, nullptr);
    xTaskCreate(t_watchdog,    "watchdog",2048, nullptr, 1, nullptr);
    xTaskCreate(t_heartbeat,   "hb",      2048, nullptr, 1, nullptr);

    ESP_LOGI(TAG, "Ready — 8 tasks");
    vTaskDelete(nullptr);
}
