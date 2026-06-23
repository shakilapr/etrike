// RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway.
// Architecture: architecture.md §7.  8 FreeRTOS tasks.
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

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
static std::atomic<int32_t>  g_brake_kpa_to_send{0};        // resolved brake kPa for 0x205 (fix C7)
static std::atomic<bool>     g_steering_disabled{false};     // gate 0x169 (fix H7 + H8)

// ── Heartbeat tracking (written by dispatch, checked by control) ─
static std::atomic<int64_t>  g_last_sys_hb_us{0};      // 0x7FE SYS heartbeat timestamp
static std::atomic<int64_t>  g_last_jetson_hb_us{0};   // 0x7FC Jetson heartbeat timestamp

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
};

static void process_frame(const can::Frame& fr, bool is_high, DispatchContext& ctx) {
    if (fr.id == can::kIdSysHeartbeat) {
        g_last_sys_hb_us.store(esp_timer_get_time());
    } else if (fr.id == can::kIdJetsonHeartbeat) {
        g_last_jetson_hb_us.store(esp_timer_get_time());
    }

    rt::GatewayQueues q;
    q.gw_tx_low  = &ctx.gw_lo;
    q.gw_tx_high = &ctx.gw_hi;
    q.cmd = &ctx.cmd;
    q.brake_req_kpa = &ctx.brake_req_kpa;
    q.estop_flag = &ctx.estop_flag;
    q.mode_from_sys = &ctx.mode_from_sys;
    q.steer_feedback_angle = &ctx.steer_feedback_angle;
    rt::route_frame(fr, is_high, q);

    if (fr.id == can::kIdSafetyEstop) { ctx.gw_lo = fr; ctx.gw_hi = fr; }
    if (fr.id == can::kIdSyntreeEpsStatus) { g_ses_angle_raw.store(ctx.steer_feedback_angle); }
    if (fr.id == can::kIdHostObstacleDist && is_high) { g_obstacle_mm.store(fr.u32_at(0)); }
}

[[noreturn]] static void t_dispatch(void*) {
    can::Frame fr;
    while (1) {
        if (xQueueReceive(g_can_rx_low_q, &fr, 0) != pdTRUE &&
            xQueueReceive(g_can_rx_high_q, &fr, 0) != pdTRUE) {
            xQueueReceive(g_can_rx_low_q, &fr, portMAX_DELAY);
        }

        bool is_high = (fr.id == can::kIdHostDriveCmd || fr.id == can::kIdHostBrakeReq
                     || fr.id == can::kIdHostLightCmd || fr.id == can::kIdJetsonHeartbeat);

        DispatchContext ctx{};
        process_frame(fr, is_high, ctx);

        if (ctx.gw_lo.id)  xQueueSend(g_gw_tx_low_q,  &ctx.gw_lo, 0);
        if (ctx.gw_hi.id)  xQueueSend(g_gw_tx_high_q, &ctx.gw_hi, 0);
        if (ctx.estop_flag)     g_estop_flag.store(true);
        if (ctx.mode_from_sys) {
            g_mode_from_sys.store(ctx.mode_from_sys);
            if (ctx.mode_from_sys != uint8_t(can::Mode::Estop)) {
                g_estop_flag.store(false);
                g_steering_disabled.store(false);
            }
        }
        if (ctx.brake_req_kpa)  g_brake_request_kpa.store(ctx.brake_req_kpa);
        if (ctx.cmd.speed_mmps) {
            xQueueOverwrite(g_cmd_q, &ctx.cmd);
            g_watchdog.feed(esp_timer_get_time());
            g_steering_disabled.store(false);
        }
    }
}

// ── Safety checks (used by t_control) ─────────────────────────────
struct SafetyResult { bool zero_setpoints; int32_t brake_kpa; bool disable_steering; };

static SafetyResult run_safety_checks(int64_t now, bool startup_grace) {
    SafetyResult r{};

    // 1. ESTOP flag from CAN 0x001 — zero everything, max brake, disable steering (fix H8)
    if (g_estop_flag.load()) {
        ESP_LOGW(TAG, "ESTOP flag set — zeroing setpoints, max brake");
        r.zero_setpoints = true;
        r.brake_kpa = shared::kMaxBrakeKpa;
        r.disable_steering = true;
        g_estop_flag.store(false);
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

    // 4. Jetson heartbeat timeout (arch §7.6: 1500ms → assisted stop)
    int64_t jetson_hb = g_last_jetson_hb_us.load();
    if (jetson_hb > 0 && (now - jetson_hb) > int64_t(shared::kHeartbeatTimeoutMsJetson) * 1000) {
        ESP_LOGW(TAG, "Jetson heartbeat timeout — assisted stop brake=2000kPa");
        r.zero_setpoints = true;
        g_brake_request_kpa.store(shared::kAssistStopKpa);
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

        uint32_t obs = g_obstacle_mm.load();
        sp.motor_speed_mmps = rt::PhysicsModel::obstacle_limit(sp.motor_speed_mmps, obs);

        int32_t obs_kpa = (obs <= shared::kObstacleStopMM) ? shared::kMaxBrakeKpa : 0;
        int32_t bk = rt::brake_arbitrate(obs_kpa, g_brake_request_kpa.load());

        // ── Safety checks ──────────────────────────────────────────
        int64_t const now = esp_timer_get_time();
        bool startup_grace = (now < int64_t(shared::kStartupGracePeriodMs) * 1000);

        SafetyResult sr = run_safety_checks(now, startup_grace);
        if (sr.zero_setpoints) {
            cmd = {0, 0};
            xQueueOverwrite(g_cmd_q, &cmd);
            sp = {};
        }
        if (sr.brake_kpa) bk = sr.brake_kpa;
        if (sr.disable_steering) g_steering_disabled.store(true);

        g_brake_kpa_to_send.store(bk);  // consumed by can_tx_low → 0x205 at 50 Hz (fix C7)
        xQueueOverwrite(g_setpoint_q, &sp);
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
                g_steering.set_target(sp.steer_angle_mdeg);  // feed resolved angle → 0x169
                uint8_t gear = (sp.motor_speed_mmps > 0) ? uint8_t(can::Gear::D)
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
            // 0x169 VCU_SES_REQ at 50 Hz, gated by disabled flag (fix H7 + H8)
            if (!g_steering_disabled.load()) {
                can::VcuSesReq ses;
                if (g_steering.tick(g_ses_angle_raw.load(), ses)) { ses.to_frame(fr); drv->send(fr); }
            }
        }
        if (xQueueReceive(g_gw_tx_low_q, &gw, 0) == pdTRUE) drv->send(gw);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ── CAN TX high (prio 3) ───────────────────────────────────────────
[[noreturn]] static void t_can_tx_high(void*) {
    can::Frame gw;
    TickType_t last = xTaskGetTickCount();
    while (1) {
        while (xQueueReceive(g_gw_tx_high_q, &gw, 0) == pdTRUE)
            g_can_high.send(gw);
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
            g_steering_disabled.store(true);  // stop 0x169 per arch §7.10 (fix H7)
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
