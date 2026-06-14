// RT ESP32-S3 — Realtime physics, steering & CAN gateway.
// Architecture: architecture.md §7.
// Phase R4: migrated from UART inter-MCU to CAN (dual-bus pending R5).

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "control_logic.h"
#include "physics_model.h"
#include "obstacle_sensor.h"
#include "watchdog.h"
#include "can/can_protocol.h"
#include "can/can_driver.h"
#include "os/queue.h"
#include <atomic>

namespace {

constexpr const char* kTag = "rt";

// ── Application state ──────────────────────────────────────────────

can::Mode             g_mode{can::Mode::Manual};
rt::PhysicsModel      g_physics;
rt::SpeedPid          g_pid;
rt::ObstacleSensor    g_obstacle;
rt::Watchdog          g_watchdog;
std::atomic<int32_t>  g_brake_request_kpa{0};
can::CanDriver        g_can_low(can::CanDriver::Config{rt::kCanLowTxGpio,
                                                          rt::kCanLowRxGpio,
                                                          rt::kCanLowBitrateHz});

// Queues
os::Queue<can::Frame, 16>  g_can_rx_queue;
os::Queue<rt::DriveCmd, 4> g_cmd_queue;         // from 0x300
os::Queue<can::Frame, 8>   g_can_tx_low_queue;   // pending R7: CAN TX low
os::Queue<can::Frame, 8>   g_can_tx_high_queue;  // pending R7: CAN TX high

// ── CAN RX low task (prio 5) — low bus TWAI ───────────────────────

void can_rx_low_task(void*) {
    can::Frame fr;
    while (true) {
        if (g_can_low.receive(fr, 100)) {
            if (can::is_estop_id(fr.id)) {
                g_mode = can::Mode::Estop;
                ESP_LOGW(kTag, "ESTOP via CAN RX low");
            } else {
                g_can_rx_queue.send(fr, 0);  // non-blocking; drop if full
            }
        }
    }
}

// ── CAN RX high task (prio 5) — high bus MCP2515 ──────────────────

void can_rx_high_task(void*) {
    can::Frame fr;
    while (true) {
        // Pending R5 integration: g_can_high.receive(fr, 100)
        // For now, MCP2515 RX is polled in this task when driver is active.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Dispatch task ──────────────────────────────────────────────────

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
        // Forwarding: SYS telemetry low→high (pending R6 gateway)
        case can::kIdSysSafetySts:    // 0x011
        case can::kIdSysThrottleSts:  // 0x120
        case can::kIdSysDiagRpt:      // 0x600
            g_can_tx_high_queue.send(fr, 0);
            break;
        // Forwarding: Jetson lights high→low (pending R6 gateway)
        case can::kIdHostLightCmd:    // 0x302
            g_can_tx_low_queue.send(fr, 0);
            break;
        // ESTOP from any source
        case can::kIdSysEstop:
        case can::kIdRtEstop:
        case can::kIdHostEstop:
            g_mode = can::Mode::Estop;
            ESP_LOGW(kTag, "ESTOP via CAN");
            break;
        // SYS mode command
        case can::kIdSysModeCmd: {    // 0x110
            uint8_t m = fr.u8_at(0);
            if (m == uint8_t(can::Mode::Auto))      g_mode = can::Mode::Auto;
            else if (m == uint8_t(can::Mode::Manual)) g_mode = can::Mode::Manual;
            break;
        }
        }
    }
}

// ── Control task (100 Hz) ──────────────────────────────────────────

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
            auto out = rt::resolve_drive_setpoint(g_physics, g_pid, cmd, measured, obs, brake_kpa, dt);

            // Serialize to CAN frames → queue for can_tx_low_task (R7)
            can::Frame drive_fr, brake_fr;
            out.drive.to_frame(drive_fr);
            out.brake.to_frame(brake_fr);
            g_can_tx_low_queue.send(drive_fr, 0);   // 0x202
            g_can_tx_low_queue.send(brake_fr, 0);   // 0x203
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── CAN TX low task (pending R7: full implementation) ──────────────

void can_tx_low_task(void*) {
    can::Frame fr;
    while (true) {
        if (g_can_tx_low_queue.receive(fr, pdMS_TO_TICKS(10))) {
            g_can_low.send(fr);
        }
    }
}

// ── CAN TX high task (pending R5+R7: MCP2515 driver) ───────────────

void can_tx_high_task(void*) {
    can::Frame fr;
    while (true) {
        if (g_can_tx_high_queue.receive(fr, pdMS_TO_TICKS(10))) {
            // Pending R5: send via MCP2515
            // g_can_high.send(fr);
        }
    }
}

// ── Obstacle task (10 Hz) ──────────────────────────────────────────

void obstacle_task(void*) {
    while (true) {
        g_obstacle.poll();
        unsigned d = g_obstacle.distance_mm();
        if (d != UINT32_MAX) {
            can::Frame fr;
            can::RtObstacleRpt{d}.to_frame(fr);
            g_can_tx_high_queue.send(fr, 0);  // → Jetson on high CAN
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── Watchdog task (10 Hz) ──────────────────────────────────────────

void watchdog_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1000));  // let system initialize
    while (true) {
        if (g_watchdog.is_stale() && !g_watchdog.is_tripped()) {
            ESP_LOGW(kTag, "STALE — zero setpoint");
            // Push zero-drive command
            can::Frame fr;
            can::RtDriveCmd{0, uint8_t(can::Gear::N)}.to_frame(fr);
            g_can_tx_low_queue.send(fr, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── Heartbeat task (2 Hz) ─────────────────────────────────────────

void heartbeat_task(void*) {
    using namespace rt;
    uint8_t ctr_low = 0, ctr_high = 0;
    while (true) {
        // Low bus: 0x7FD RT→SYS
        can::Frame fr_low;
        fr_low.id = can::kIdRtHeartbeatLow;
        fr_low.dlc = 1;
        fr_low.put_u8(0, ++ctr_low);
        g_can_low.send(fr_low);

        // High bus: 0x7FD RT→Jetson (pending R5: MCP2515)
        can::Frame fr_high;
        fr_high.id = can::kIdRtHeartbeatHigh;
        fr_high.dlc = 1;
        fr_high.put_u8(0, ++ctr_high);
        g_can_tx_high_queue.send(fr_high, 0);

        vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs));
    }
}

}  // anonymous namespace

// ── app_main ───────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(kTag, "RT ESP32-S3 — delta trike control");

    // Init hardware
    g_can_low.init();
    g_obstacle.init();
    g_watchdog.init();

    ESP_LOGI(kTag, "PID Kp=%.3f Ki=%.3f Kd=%.3f",
             static_cast<double>(rt::kPidKp),
             static_cast<double>(rt::kPidKi),
             static_cast<double>(rt::kPidKd));

    // Create tasks (architecture.md §7.7: 9 tasks total)
    xTaskCreate(can_rx_low_task,   "can_rx_low", 4096, nullptr, 5, nullptr);
    xTaskCreate(can_rx_high_task,  "can_rx_high",4096, nullptr, 5, nullptr);
    xTaskCreate(dispatch_task,      "dispatch",   4096, nullptr, 4, nullptr);
    xTaskCreate(control_task,       "control",    4096, nullptr, 4, nullptr);
    xTaskCreate(can_tx_low_task,    "can_tx_low", 3072, nullptr, 3, nullptr);
    xTaskCreate(can_tx_high_task,   "can_tx_high",3072, nullptr, 3, nullptr);
    xTaskCreate(obstacle_task,      "obstacle",   2048, nullptr, 2, nullptr);
    xTaskCreate(watchdog_task,      "watchdog",   2048, nullptr, 1, nullptr);
    xTaskCreate(heartbeat_task,     "hb",         2048, nullptr, 1, nullptr);

    ESP_LOGI(kTag, "Ready. Mode=%s", can::mode_name(g_mode));
}
