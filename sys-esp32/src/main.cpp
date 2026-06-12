// SYS ESP32-S3 — safety, local actuation, and private Syntree CAN master.
// Public Jetson traffic reaches SYS only through the direct RT/SYS inter-MCU link.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "config.h"
#include "can_rx_router.h"
#include "mode_manager.h"
#include "safety_monitor.h"
#include "motor_driver.h"
#include "brake_actuator.h"
#include "throttle_input.h"
#include "diagnostics.h"
#include "speed_limiter.h"
#include "can/can_protocol.h"
#include "can/can_driver.h"
#include "intermcu/intermcu_protocol.h"
#include "intermcu/intermcu_driver.h"
#include "os/queue.h"
#include <atomic>
#include <limits>

namespace {

constexpr const char* kTag = "sys";

// ── application state ──────────────────────────────────────────
sys::ModeManager   g_mode;
sys::SafetyMonitor g_safety;
sys::MotorDriver   g_motor;
sys::BrakeActuator g_brake;
sys::ThrottleInput g_throttle;
sys::Diagnostics   g_diag;
can::CanDriver     g_can;
inter_mcu::InterMcuDriver g_link({
    sys::kInterMcuUartPort,
    sys::kInterMcuTxGpio,
    sys::kInterMcuRxGpio,
    sys::kInterMcuBaud,
});
std::atomic<unsigned> g_obstacle_mm{std::numeric_limits<unsigned>::max()};
std::atomic<int32_t> g_target_steer_angle_mdeg{0};
std::atomic<int32_t> g_target_brake_pressure_kpa{0};
std::atomic<uint16_t> g_syntree_fault_bits{0};

os::Queue<can::Frame, 16>                g_can_rx_queue;
os::Queue<inter_mcu::RtToSysSetpoint, 4> g_setpoint_queue;

// I2C mutex — serialises access to the shared I2C bus (MCP4725 DAC + optional IMU).
// ESP-IDF I2C driver thread safety varies by version; application-level mutex
// guarantees transaction atomicity regardless of driver version.
SemaphoreHandle_t g_i2c_mutex = nullptr;

// ── context structs for tasks needing multiple pointers ────────

struct CanRxContext {
    can::CanDriver*             driver;
    os::Queue<can::Frame, 16>*  queue;
};

struct DiagContext {
    sys::Diagnostics*  diag;
    can::CanDriver*    can;
};

// ── CAN RX task ────────────────────────────────────────────────

void can_rx_task(void* arg) {
    auto* ctx = static_cast<CanRxContext*>(arg);
    can::Frame fr;
    while (true) {
        if (ctx->driver->receive(fr, 100)) {
            auto route = sys::classify_can_rx_frame(fr);
            if (route.enqueue) {
                ctx->queue->send(fr, 0);
            }
        }
    }
}

// ── inter-MCU RX task ──────────────────────────────────────────

void link_rx_task(void*) {
    inter_mcu::Frame fr;
    while (true) {
        if (!g_link.receive(fr, 100)) continue;

        switch (fr.type) {
        case inter_mcu::MessageType::RtToSysSetpoint: {
            auto sp = inter_mcu::RtToSysSetpoint::from_frame(fr);
            g_safety.feed_heartbeat_rt();
            if ((sp.flags & inter_mcu::kFlagEstop) != 0) {
                g_mode.set(can::Mode::Estop);
                ESP_LOGW(kTag, "ESTOP via RT link");
            }
            g_target_steer_angle_mdeg.store(sp.steer_angle_mdeg, std::memory_order_relaxed);
            g_target_brake_pressure_kpa.store(sp.brake_pressure_kpa, std::memory_order_relaxed);
            g_setpoint_queue.overwrite(sp);
            break;
        }
        case inter_mcu::MessageType::RtHeartbeat:
            g_safety.feed_heartbeat_rt();
            break;
        case inter_mcu::MessageType::RtObstacleDist: {
            auto obs = inter_mcu::RtObstacleDistance::from_frame(fr);
            g_obstacle_mm.store(obs.distance_mm, std::memory_order_relaxed);
            break;
        }
        default:
            break;
        }
    }
}

// ── safety task (20 Hz, priority 5) ────────────────────────────

void safety_task(void*) {
    using namespace sys;
    while (true) {
        // E-stop button → ESTOP
        if (g_safety.estop_active() && g_mode.current() != can::Mode::Estop) {
            ESP_LOGW(kTag, "ESTOP — button pressed");
            g_mode.set(can::Mode::Estop);
        }

        // Heartbeat timeout in AUTO → ESTOP
        if (g_mode.current() == can::Mode::Auto && !g_safety.heartbeat_ok()) {
            ESP_LOGW(kTag, "ESTOP — heartbeat timeout");
            g_mode.set(can::Mode::Estop);
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / kSafetyCheckHz));
    }
}

// ── dispatch task ──────────────────────────────────────────────

void dispatch_task(void*) {
    can::Frame fr;
    while (true) {
        if (!g_can_rx_queue.receive_blocking(fr)) continue;

        switch (fr.id) {
        case can::kIdSyntreeEpsStatus:
            // TODO: decode actual EPS angle and fault bits after DBC/protocol confirmation.
            g_syntree_fault_bits.store(0, std::memory_order_relaxed);
            break;
        case can::kIdSyntreeSebStatus:
            // TODO: decode SEB pressure/stroke feedback and fault bits after protocol confirmation.
            g_syntree_fault_bits.store(0, std::memory_order_relaxed);
            break;
        }
    }
}

void send_sys_status() {
    inter_mcu::Frame fr;
    inter_mcu::SysToRtStatus{
        static_cast<uint8_t>(g_mode.current()),
        g_safety.estop_active(),
        g_safety.heartbeat_ok(),
        g_brake.is_engaged(),
        0,
        g_target_brake_pressure_kpa.load(std::memory_order_relaxed),
        g_syntree_fault_bits.load(std::memory_order_relaxed),
    }.to_frame(fr);
    g_link.send(fr);
}

// ── mode task (10 Hz) ──────────────────────────────────────────

void mode_task(void*) {
    while (true) {
        auto prev = g_mode.current();
        g_mode.poll();
        auto cur = g_mode.current();

        if (cur != prev) {
            send_sys_status();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── motor task (100 Hz) ────────────────────────────────────────

void motor_task(void*) {
    using namespace sys;
    TickType_t period = pdMS_TO_TICKS(1000 / kControlLoopHz);
    TickType_t last   = xTaskGetTickCount();

    while (true) {
        auto mode = g_mode.current();

        if (mode == can::Mode::Estop) {
            g_motor.stop();
        } else if (mode == can::Mode::Auto) {
            inter_mcu::RtToSysSetpoint sp;
            if (g_setpoint_queue.receive(sp, 0)) {
                g_motor.set_effort(sp.motor_effort_pwm);
            }
        } else {  // Manual
            int32_t throttle = g_throttle.read_mmps();
            unsigned obstacle = g_obstacle_mm.load(std::memory_order_relaxed);
            g_motor.set_speed(sys::limit_forward_speed_for_obstacle(throttle, obstacle));
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── throttle task (100 Hz) ─────────────────────────────────────

void throttle_task(void*) {
    using namespace sys;
    while (true) {
        g_throttle.poll();
        vTaskDelay(pdMS_TO_TICKS(1000 / kControlLoopHz));
    }
}

// ── brake task (20 Hz) ────────────────────────────────────────

void brake_task(void*) {
    using namespace sys;
    while (true) {
        auto mode = g_mode.current();

        if (mode == can::Mode::Estop) {
            if (!g_brake.is_engaged()) {
                g_brake.engage();
                send_sys_status();
            }
        } else if (g_safety.brake_lever_pressed()) {
            if (!g_brake.is_engaged()) {
                g_brake.engage();
                send_sys_status();
            }
        } else {
            if (g_brake.is_engaged()) {
                g_brake.release();
                send_sys_status();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / kSafetyCheckHz));
    }
}

// ── inter-MCU status task (20 Hz) ──────────────────────────────

void link_status_task(void*) {
    while (true) {
        send_sys_status();
        auto hb = inter_mcu::heartbeat(inter_mcu::MessageType::SysHeartbeat);
        g_link.send(hb);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── private Syntree CAN command task (50 Hz) ───────────────────

void syntree_tx_task(void*) {
    while (true) {
        if (sys::kSyntreeCanOutputEnabled) {
            can::Frame fr;

            can::SyntreeEpsCommand eps;
            // TODO: encode steering angle, rolling counter, and checksum after
            // confirming the project-specific EPS-C byte layout.
            eps.to_frame(fr);
            g_can.send(fr);

            can::SyntreeSebCommand seb;
            // TODO: encode brake pressure/stroke, rolling counter, and checksum
            // after confirming the project-specific SEB byte layout.
            seb.to_frame(fr);
            g_can.send(fr);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── diagnostics task (1 Hz) ────────────────────────────────────

void diagnostics_task(void*) {
    using namespace sys;
    while (true) {
        auto mode = g_mode.current();

        g_diag.report(
            static_cast<uint8_t>(mode),
            g_brake.is_engaged(),
            g_safety.heartbeat_ok(),
            g_safety.estop_active()
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // anonymous namespace

// ── app_main ───────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(kTag, "SYS ESP32-S3 — safety & actuator");

    // Init synchronisation primitives
    g_i2c_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(g_i2c_mutex != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

    // Init hardware
    g_can.init();
    g_link.init();
    g_mode.init();
    g_safety.init();
    g_throttle.init();
    g_motor.init();
    g_brake.init();

    // Create tasks
    static CanRxContext can_rx_ctx{&g_can, &g_can_rx_queue};

    // prio 5: private CAN RX, RT link RX, safety
    // prio 4: dispatch, mode, motor  |  prio 3: throttle, brake
    // prio 2: link status, Syntree TX  |  prio 1: diag
    xTaskCreate(can_rx_task,      "can_rx",   4096, &can_rx_ctx,   5, nullptr);
    xTaskCreate(link_rx_task,     "link_rx",  4096, nullptr,       5, nullptr);
    xTaskCreate(safety_task,      "safety",   2048, nullptr,       5, nullptr);
    xTaskCreate(dispatch_task,    "dispatch", 3072, nullptr,       4, nullptr);
    xTaskCreate(mode_task,        "mode",     2048, nullptr,       4, nullptr);
    xTaskCreate(motor_task,       "motor",    2048, nullptr,       4, nullptr);
    xTaskCreate(throttle_task,    "throttle", 1536, nullptr,       3, nullptr);
    xTaskCreate(brake_task,       "brake",    1536, nullptr,       3, nullptr);
    xTaskCreate(link_status_task, "link_tx",  3072, nullptr,       2, nullptr);
    xTaskCreate(syntree_tx_task,  "syn_tx",   3072, nullptr,       2, nullptr);
    xTaskCreate(diagnostics_task, "diag",     2048, nullptr,       1, nullptr);

    ESP_LOGI(kTag, "Ready. Mode=%s", can::mode_name(g_mode.current()));
}
