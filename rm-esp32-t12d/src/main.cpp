// RM-ESP32-T12D — Receiver Module Gateway (C++17 FreeRTOS Application)
// Target: ESP32 / ESP32-S3 via ESP-IDF 5.x
// Controller: RadioLink T12D (FHSS V2.1) + RadioLink R16F V1.0 (SBUS Mode)

#include <atomic>
#include <cmath>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "config.h"
#include "can_driver.h"
#include "can_emitter.h"
#include "rc_receiver.h"

static const char* TAG = "rm_t12d";

// ── CAN Driver & Peripherals ───────────────────────────────────────
static can::CanDriver g_can(can::CanDriver::Config{
    rm::kCanTxGpio,
    rm::kCanRxGpio,
    rm::kCanBitrateHz
});

static rm::RcReceiver g_rc;
static rm::CanEmitter g_emitter;

// Telemetry counters
static std::atomic<uint32_t> g_can_tx_ok{0};
static std::atomic<uint32_t> g_can_tx_fail{0};

static bool send_can_frame(can::Frame& fr) {
    if (!g_can.send(fr, 2)) {
        g_can_tx_fail.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_can_tx_ok.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ── Task: RC SBUS Capture (Event-driven UART receiver) ─────────────
[[noreturn]] static void task_rc_capture(void*) {
    bool boot_warning_logged = false;
    uint32_t boot_start_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    while (1) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // sample() blocks for up to 10ms on UART, waking immediately when bytes arrive
        g_rc.sample(now_ms);

        if (!boot_warning_logged && (now_ms - boot_start_ms) >= 2000) {
            auto snap = g_rc.snapshot();
            if (!snap.signal_valid) {
                ESP_LOGW(TAG, "No SBUS frames received after 2s! Verify R16F has RED + BLUE LEDs ON.");
                boot_warning_logged = true;
            }
        }
    }
}

// ── Task: CAN Transmit & Encode (100 Hz / 10 ms) ──────────────────
[[noreturn]] static void task_can_tx(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kCanTxHz);
    TickType_t last = xTaskGetTickCount();
    static bool was_in_link_loss = false;
    static uint32_t tick_10ms_count = 0;

    while (1) {
        g_can.service_recovery(esp_timer_get_time());

        rm::RcSnapshot snap = g_rc.snapshot();

        // 1. Link State Transition Warnings
        if (!snap.signal_valid) {
            if (!was_in_link_loss) {
                ESP_LOGW(TAG, "RC Link LOST or Failsafe active! Safe stop commanded.");
                was_in_link_loss = true;
            }
        } else {
            if (was_in_link_loss) {
                ESP_LOGI(TAG, "RC Link active.");
                was_in_link_loss = false;
            }
        }

        // Active drive condition: SWA enabled, Park released, signal valid
        bool drive_active = snap.signal_valid && snap.drive_enable_req && !snap.park_hold_req;
        int32_t target_motor_speed = drive_active ? snap.target_speed_mmps : 0;
        can::Gear active_gear = drive_active ? snap.gear : can::Gear::N;

        // 2. Emit canonical CAN cluster for current operating mode (BARE, SYS, RT)
        g_emitter.emit_cluster(snap, tick_10ms_count++, [](can::Frame& fr) {
            return send_can_frame(fr);
        });

        // 3. Serial CAN Command Display (Delta-triggered + 2 Hz Decimated)
        static struct {
            rm::OperatingMode mode{rm::OperatingMode::Bare};
            float             steer_deg{999.0f};
            float             brake_mm{999.0f};
            float             throttle{999.0f};
            int32_t           speed_mmps{999999};
            can::Gear         selected_gear{static_cast<can::Gear>(0xFF)};
            can::Gear         cmd_gear{static_cast<can::Gear>(0xFF)};
            bool              enable{false};
            bool              park{false};
            bool              valid{false};
            uint32_t          count{0};
        } s_last_can_log;

        bool mode_changed     = (snap.op_mode != s_last_can_log.mode);
        bool valid_changed    = (snap.signal_valid != s_last_can_log.valid);
        bool steer_changed    = std::abs(snap.steering_deg - s_last_can_log.steer_deg) >= rm::kLogDeltaSteerDeg;
        bool brake_changed    = std::abs(snap.brake_stroke_mm - s_last_can_log.brake_mm) >= rm::kLogDeltaBrakeMm;
        bool throttle_changed = std::abs(snap.throttle_norm - s_last_can_log.throttle) >= 0.05f;
        bool speed_changed    = std::abs(target_motor_speed - s_last_can_log.speed_mmps) >= rm::kLogDeltaSpeedMmps;
        bool gear_changed     = (snap.gear != s_last_can_log.selected_gear) || (active_gear != s_last_can_log.cmd_gear);
        bool enable_status_chg= (snap.drive_enable_req != s_last_can_log.enable);
        bool park_changed     = (snap.park_hold_req != s_last_can_log.park);
        bool periodic_tick    = (++s_last_can_log.count >= static_cast<uint32_t>(rm::kCanLogDecimation));

        if (mode_changed || valid_changed || steer_changed || brake_changed || throttle_changed || speed_changed ||
            gear_changed || enable_status_chg || park_changed || periodic_tick) {
            s_last_can_log.mode           = snap.op_mode;
            s_last_can_log.valid          = snap.signal_valid;
            s_last_can_log.steer_deg      = snap.steering_deg;
            s_last_can_log.brake_mm       = snap.brake_stroke_mm;
            s_last_can_log.throttle       = snap.throttle_norm;
            s_last_can_log.speed_mmps     = target_motor_speed;
            s_last_can_log.selected_gear  = snap.gear;
            s_last_can_log.cmd_gear       = active_gear;
            s_last_can_log.enable         = snap.drive_enable_req;
            s_last_can_log.park           = snap.park_hold_req;
            s_last_can_log.count          = 0;

            const char* gear_str = (snap.gear == can::Gear::D) ? "D" :
                                   ((snap.gear == can::Gear::R) ? "R" : "N");

            ESP_LOGI("tx", "[%s] STR:%+.1f BRK:%.1f MTR:%+d[%s] EN:%s PRK:%s",
                     rm::mode_name(snap.op_mode),
                     snap.steering_deg,
                     snap.brake_stroke_mm,
                     target_motor_speed,
                     gear_str,
                     snap.drive_enable_req ? "ON" : "OFF",
                     snap.park_hold_req ? "HOLD" : "OFF");
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Task: Heartbeat & Status Diagnostics (10 Hz / 100 ms) ─────────
[[noreturn]] static void task_heartbeat(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kHeartbeatHz);
    TickType_t last = xTaskGetTickCount();
    uint32_t hb_count = 0;

    while (1) {
        // 1 Hz periodic health & telemetry summary (every 10 ticks)
        if (++hb_count % 10 == 0) {
            const auto snap = g_rc.snapshot();
            const char* gear_str = (snap.gear == can::Gear::D) ? "D" :
                                   ((snap.gear == can::Gear::R) ? "R" : "N");
            const char* link_str = (snap.link_state == rm::LinkState::Normal) ? "OK" :
                                   ((snap.link_state == rm::LinkState::Degraded) ? "DEGR" : "LOST");

            ESP_LOGI(TAG, "STATUS | Mode=%s Link=%s Valid=%d Enable=%d Gear=%s Steer=%+.1f deg Brk=%.1f mm Throt=%.0f%% Spd=%d | CAN ok=%lu fail=%lu",
                     rm::mode_name(snap.op_mode),
                     link_str,
                     snap.signal_valid ? 1 : 0,
                     snap.drive_enable_req ? 1 : 0,
                     gear_str,
                     snap.steering_deg,
                     snap.brake_stroke_mm,
                     snap.throttle_norm * 100.0f,
                     snap.target_speed_mmps,
                     static_cast<unsigned long>(g_can_tx_ok.load(std::memory_order_relaxed)),
                     static_cast<unsigned long>(g_can_tx_fail.load(std::memory_order_relaxed)));

            const auto health = g_can.health_snapshot();
            if (health.state == can::CanDriver::HealthState::BusOff) {
                ESP_LOGE(TAG, "CAN BUS-OFF active! Recovery attempts: %lu", static_cast<unsigned long>(health.recovery_attempts));
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  RM-ESP32-T12D Receiver Gateway (RadioLink SBUS)");
    ESP_LOGI(TAG, "  Version: %s", rm::kFirmwareVersion);
    ESP_LOGI(TAG, "=================================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Initialize TWAI CAN Controller
    esp_log_level_set("esp_twai", ESP_LOG_NONE);
    esp_log_level_set("can", ESP_LOG_NONE);
    if (!g_can.init()) {
        ESP_LOGE(TAG, "CAN initialization failed! Rebooting...");
        esp_restart();
    }

    // 2. Initialize SBUS Receiver Peripheral (UART1 RX GPIO 16, 100k, 8E2, inverted)
    if (!g_rc.init()) {
        ESP_LOGE(TAG, "SBUS receiver initialization failed! Rebooting...");
        esp_restart();
    }

    // 3. Spawn FreeRTOS Tasks
    xTaskCreatePinnedToCore(task_rc_capture, "rc_capture", 4096, nullptr, 8, nullptr, 1);
    xTaskCreatePinnedToCore(task_can_tx,     "can_tx",     4096, nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(task_heartbeat,  "heartbeat",  3072, nullptr, 1, nullptr, 1);

    ESP_LOGI(TAG, "All tasks created successfully. RM-ESP32-T12D operational.");
}

