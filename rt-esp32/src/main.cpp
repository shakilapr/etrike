// RT ESP32-S3 — Jetson bridge and realtime physics.
// Public CAN: Jetson <-> RT. Direct inter-MCU link: RT <-> SYS.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "control_logic.h"
#include "physics_model.h"
#include "speed_pid.h"
#include "obstacle_sensor.h"
#include "watchdog.h"
#include "can_protocol.h"
#include "can_driver.h"
#include "os/queue.h"
// NOTE: intermcu/ removed — Phase 3 will rewrite this file for unified single-ESP32 architecture
#include <atomic>

namespace {

constexpr const char* kTag = "rt";

// ── application state ──────────────────────────────────────────
can::Mode             g_mode{can::Mode::Manual};
rt::PhysicsModel      g_physics;
rt::SpeedPid          g_pid;
rt::ObstacleSensor    g_obstacle;
rt::Watchdog          g_watchdog;
std::atomic<int32_t>  g_brake_request_kpa{0};
can::CanDriver     g_can;
inter_mcu::InterMcuDriver g_link({
    rt::kInterMcuUartPort,
    rt::kInterMcuTxGpio,
    rt::kInterMcuRxGpio,
    rt::kInterMcuBaud,
});

os::Queue<can::Frame, 16>        g_can_rx_queue;
os::Queue<rt::DriveCmd, 4>       g_cmd_queue;
os::Queue<inter_mcu::RtToSysSetpoint, 4> g_setpoint_queue;

void send_link_estop() {
    inter_mcu::Frame lf;
    inter_mcu::RtToSysSetpoint{0, 0, 0, inter_mcu::kFlagEstop}.to_frame(lf);
    g_link.send(lf);
}

// ── CAN RX task ────────────────────────────────────────────────

struct CanRxContext {
    can::CanDriver*                driver;
    os::Queue<can::Frame, 16>*     queue;
};

void can_rx_task(void* arg) {
    auto* ctx = static_cast<CanRxContext*>(arg);
    can::Frame fr;
    while (true) {
        if (ctx->driver->receive(fr, 100)) {
            if (can::is_estop_id(fr.id)) {
                g_mode = can::Mode::Estop;
                send_link_estop();
                ESP_LOGW(kTag, "ESTOP via CAN RX");
            } else {
                ctx->queue->send(fr, 0);  // non-blocking; drop if full
            }
        }
    }
}

// ── dispatch task ──────────────────────────────────────────────

void dispatch_task(void*) {
    can::Frame fr;
    while (true) {
        if (!g_can_rx_queue.receive_blocking(fr)) continue;

        switch (fr.id) {
        case can::kIdHostDriveCmd: {   // 0x300 — Jetson drive command
            auto cmd = can::HostDriveCmd::from_frame(fr);
            g_cmd_queue.overwrite({cmd.speed_mmps, cmd.yaw_rate_mrad_s});
            g_watchdog.feed();
            break;
        }
        case can::kIdHostBrakeRequest: {  // 0x301 — Jetson brake request
            auto brk = can::HostBrakeRequest::from_frame(fr);
            g_brake_request_kpa.store(brk.brake_pressure_kpa, std::memory_order_relaxed);
            break;
        }
        case can::kIdSysEstop:
        case can::kIdRtEstop:
        case can::kIdHostEstop:
            g_mode = can::Mode::Estop;
            send_link_estop();
            ESP_LOGW(kTag, "ESTOP via CAN");
            break;
        }
    }
}

// ── inter-MCU RX task ──────────────────────────────────────────

void link_rx_task(void*) {
    inter_mcu::Frame fr;
    while (true) {
        if (!g_link.receive(fr, 100)) continue;

        switch (fr.type) {
        case inter_mcu::MessageType::SysToRtStatus: {
            auto status = inter_mcu::SysToRtStatus::from_frame(fr);
            if (status.estop_active || status.mode == static_cast<uint8_t>(can::Mode::Estop)) {
                g_mode = can::Mode::Estop;
            } else if (status.mode == static_cast<uint8_t>(can::Mode::Auto)) {
                g_mode = can::Mode::Auto;
            } else {
                g_mode = can::Mode::Manual;
            }
            break;
        }
        case inter_mcu::MessageType::SysHeartbeat:
            break;
        default:
            break;
        }
    }
}

// ── control task (100 Hz) ──────────────────────────────────────

void control_task(void*) {
    using namespace rt;
    TickType_t period = pdMS_TO_TICKS(1000 / kControlLoopHz);
    TickType_t last   = xTaskGetTickCount();

    while (true) {
        rt::DriveCmd cmd;
        bool has_cmd = g_cmd_queue.receive(cmd, 0);

        if (has_cmd && g_mode == can::Mode::Auto) {
            unsigned obs = g_obstacle.distance_mm();
            float measured = 0.0f;  // TODO: encoder_get_speed_mmps()
            float dt = 1.0f / kControlLoopHz;
            int32_t brake_kpa = g_brake_request_kpa.load(std::memory_order_relaxed);
            auto sp = rt::resolve_drive_setpoint(g_physics, g_pid, cmd, measured, obs, brake_kpa, dt);

            // Push setpoint to SYS over CAN.
            g_setpoint_queue.overwrite(sp);
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── inter-MCU TX task ──────────────────────────────────────────

void link_tx_task(void*) {
    inter_mcu::RtToSysSetpoint sp;
    while (true) {
        if (g_setpoint_queue.receive(sp, pdMS_TO_TICKS(50))) {
            inter_mcu::Frame fr;
            sp.to_frame(fr);
            g_link.send(fr);
        }
    }
}

// ── obstacle task (10 Hz) ──────────────────────────────────────

void obstacle_task(void*) {
    while (true) {
        g_obstacle.poll();
        unsigned d = g_obstacle.distance_mm();
        if (d != UINT32_MAX) {
            can::Frame fr;
            can::RtObstacleDist{d}.to_frame(fr);
            g_can.send(fr);

            inter_mcu::Frame link_fr;
            inter_mcu::RtObstacleDistance{d}.to_frame(link_fr);
            g_link.send(link_fr);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── watchdog task (10 Hz) ──────────────────────────────────────

void watchdog_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1000));  // let system initialize
    while (true) {
        if (g_watchdog.is_stale() && !g_watchdog.is_tripped()) {
            ESP_LOGW(kTag, "STALE — zero setpoint");
            inter_mcu::Frame fr;
            inter_mcu::RtToSysSetpoint{
                0, 0, 0, inter_mcu::kFlagAutoEnable | inter_mcu::kFlagEpsEnable
            }.to_frame(fr);
            g_link.send(fr);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── heartbeat task (2 Hz) ──────────────────────────────────────

void heartbeat_task(void*) {
    using namespace rt;
    can::Frame can_fr;
    can_fr.id = can::kIdHeartbeat;
    can_fr.dlc = 0;
    while (true) {
        g_can.send(can_fr);
        auto link_fr = inter_mcu::heartbeat(inter_mcu::MessageType::RtHeartbeat);
        g_link.send(link_fr);
        vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs));
    }
}

}  // anonymous namespace

// ── app_main ───────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(kTag, "RT ESP32-S3 — delta trike control");

    // Init hardware
    g_can.init();
    g_link.init();
    g_obstacle.init();
    g_watchdog.init();

    ESP_LOGI(kTag, "Kp=%.3f Ki=%.3f Kd=%.3f",
             static_cast<double>(rt::kPidKp),
             static_cast<double>(rt::kPidKi),
             static_cast<double>(rt::kPidKd));

    // Create tasks
    static CanRxContext can_rx_ctx{&g_can, &g_can_rx_queue};
    xTaskCreate(can_rx_task,       "can_rx",   4096, &can_rx_ctx,     5, nullptr);
    xTaskCreate(link_rx_task,      "link_rx",  4096, nullptr,         5, nullptr);
    xTaskCreate(dispatch_task,      "dispatch", 3072, nullptr,         4, nullptr);
    xTaskCreate(control_task,       "control",  4096, nullptr,         4, nullptr);
    xTaskCreate(link_tx_task,       "link_tx",  3072, nullptr,         3, nullptr);
    xTaskCreate(obstacle_task,      "obstacle", 2048, nullptr,         2, nullptr);
    xTaskCreate(watchdog_task,      "watchdog", 2048, nullptr,         1, nullptr);
    xTaskCreate(heartbeat_task,     "hb",       2048, nullptr,         1, nullptr);

    ESP_LOGI(kTag, "Ready. Mode=%s", can::mode_name(g_mode));
}
