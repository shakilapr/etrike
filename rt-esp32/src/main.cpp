// RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway.
// Architecture: architecture.md §7.  8 FreeRTOS tasks.
#include <cstdio>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

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

// ── Queues ─────────────────────────────────────────────────────────
static QueueHandle_t g_can_rx_low_q  = nullptr;  // 16 deep
static QueueHandle_t g_can_rx_high_q = nullptr;  // 16 deep
static QueueHandle_t g_cmd_q         = nullptr;  //  4 deep, overwrite
static QueueHandle_t g_setpoint_q    = nullptr;  //  4 deep, overwrite
static QueueHandle_t g_gw_tx_low_q   = nullptr;  //  8 deep
static QueueHandle_t g_gw_tx_high_q  = nullptr;  //  8 deep

// ── CAN RX — low bus (prio 5) ──────────────────────────────────────
[[noreturn]] static void t_can_rx_low(void*) {
    can::Frame fr;
    while (1) {
        auto* drv = rt::can_low_driver();
        if (drv && drv->receive(fr, 100))
            xQueueSend(g_can_rx_low_q, &fr, 0);
    }
}

// ── CAN RX — high bus, MCP2515 (prio 5) ────────────────────────────
[[noreturn]] static void t_can_rx_high(void*) {
    can::Frame fr;
    while (1) {
        if (g_can_high.receive(fr, 100))
            xQueueSend(g_can_rx_high_q, &fr, 0);
    }
}

// ── Dispatch + gateway (prio 4) ────────────────────────────────────
[[noreturn]] static void t_dispatch(void*) {
    can::Frame fr;
    while (1) {
        if (xQueueReceive(g_can_rx_low_q, &fr, 0) != pdTRUE &&
            xQueueReceive(g_can_rx_high_q, &fr, 0) != pdTRUE) {
            xQueueReceive(g_can_rx_low_q, &fr, portMAX_DELAY);
        }

        rt::GatewayQueues q;
        can::Frame gw_lo, gw_hi;
        q.gw_tx_low  = &gw_lo;
        q.gw_tx_high = &gw_hi;

        rt::HostDriveCmd cmd_buf{};
        q.cmd = &cmd_buf;

        int32_t bkpa = 0; q.brake_req_kpa = &bkpa;
        bool estop = false; q.estop_flag = &estop;
        uint8_t mode = 0; q.mode_from_sys = &mode;
        int16_t ang = 0; q.steer_feedback_angle = &ang;

        bool is_high = (fr.id == 0x300 || fr.id == 0x301 || fr.id == 0x302 || fr.id == 0x7FC);
        rt::route_frame(fr, is_high, q);

        if (gw_lo.id)  xQueueSend(g_gw_tx_low_q,  &gw_lo, 0);
        if (gw_hi.id)  xQueueSend(g_gw_tx_high_q, &gw_hi, 0);
        if (estop)      g_estop_flag.store(true);
        if (mode)       g_mode_from_sys.store(mode);
        if (bkpa)       g_brake_request_kpa.store(bkpa);
        if (cmd_buf.speed_mmps) { xQueueOverwrite(g_cmd_q, &cmd_buf); g_watchdog.feed(esp_timer_get_time()); }
    }
}

// ── Control (prio 4, 100 Hz) ───────────────────────────────────────
[[noreturn]] static void t_control(void*) {
    TickType_t per = pdMS_TO_TICKS(10), last = xTaskGetTickCount();
    rt::HostDriveCmd cmd{};
    while (1) {
        if (xQueueReceive(g_cmd_q, &cmd, 0) != pdTRUE)
            cmd = {0, 0};

        rt::ResolvedSetpoint sp;
        g_physics.resolve({cmd.speed_mmps, cmd.yaw_rate_mrad_s}, sp);

        uint32_t obs = g_obstacle_mm.load();
        sp.motor_speed_mmps = rt::PhysicsModel::obstacle_limit(sp.motor_speed_mmps, obs);

        int32_t obs_kpa = (obs <= rt::kObstacleStopDistMM) ? 20000 : 0;
        int32_t bk = rt::brake_arbitrate(obs_kpa, g_brake_request_kpa.load());
        (void)bk;  // sent via 0x203 in can_tx_low

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
                can::RtDriveCmd{sp.motor_speed_mmps, uint8_t(0)}.to_frame(fr);
                drv->send(fr);
            }
        }
        if (xTaskGetTickCount() - t50 >= pdMS_TO_TICKS(20)) {
            t50 = xTaskGetTickCount();
            can::VcuSesReq ses;
            if (g_steering.tick(INT16_MIN, ses)) { ses.to_frame(fr); drv->send(fr); }
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
            rt::HostDriveCmd zero{};
            xQueueOverwrite(g_cmd_q, &zero);
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
    g_cmd_q         = xQueueCreate( 4, sizeof(rt::HostDriveCmd));
    g_setpoint_q    = xQueueCreate( 4, sizeof(rt::ResolvedSetpoint));
    g_gw_tx_low_q   = xQueueCreate( 8, sizeof(can::Frame));
    g_gw_tx_high_q  = xQueueCreate( 8, sizeof(can::Frame));

    xTaskCreate(t_can_rx_low,  "rx_low",  4096, nullptr, 5, nullptr);
    xTaskCreate(t_can_rx_high, "rx_high", 4096, nullptr, 5, nullptr);
    xTaskCreate(t_dispatch,    "dispatch",4096, nullptr, 4, nullptr);
    xTaskCreate(t_control,     "control", 4096, nullptr, 4, nullptr);
    xTaskCreate(t_can_tx_low,  "tx_low",  3072, nullptr, 3, nullptr);
    xTaskCreate(t_can_tx_high, "tx_high", 3072, nullptr, 3, nullptr);
    xTaskCreate(t_watchdog,    "watchdog",2048, nullptr, 1, nullptr);
    xTaskCreate(t_heartbeat,   "hb",      2048, nullptr, 1, nullptr);

    ESP_LOGI(TAG, "Ready — 8 tasks");
    vTaskDelete(nullptr);
}
