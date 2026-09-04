// RM-ESP32 — Receiver Module Gateway (C++17 FreeRTOS Application)
// Target: ESP32 classic / ESP-IDF 5.x
// Architecture: new-architecture.md

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
#include "rc_receiver.h"

static const char* TAG = "rm";

// ── CAN Driver & Peripherals ───────────────────────────────────────
static can::CanDriver g_can(can::CanDriver::Config{
    rm::kCanTxGpio,
    rm::kCanRxGpio,
    rm::kCanBitrateHz
});

static rm::RcReceiver g_rc;

// Telemetry counters
static std::atomic<uint32_t> g_can_tx_ok{0};
static std::atomic<uint32_t> g_can_tx_fail{0};
static std::atomic<uint32_t> g_alive_capture{0};
static std::atomic<uint32_t> g_alive_can_tx{0};
static std::atomic<uint32_t> g_alive_hb{0};

// Rolling counters for protocol frames
static uint8_t g_roll_ses = 0;
static uint8_t g_roll_seb = 0;
static uint8_t g_roll_hmi_mode = 0;
static uint8_t g_roll_hmi_pwr = 0;

static bool send_can_frame(can::Frame& fr, const char* name) {
    if (!g_can.send(fr)) {
        g_can_tx_fail.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "CAN TX dropped: %s (ID %03lX)", name, static_cast<unsigned long>(fr.id));
        return false;
    }
    g_can_tx_ok.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ── Task: RC Capture (50 Hz / 20 ms) ──────────────────────────────
[[noreturn]] static void task_rc_capture(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kRcCaptureHz);
    TickType_t last = xTaskGetTickCount();

    while (1) {
        g_alive_capture.store(xTaskGetTickCount(), std::memory_order_relaxed);
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        g_rc.sample(now_ms);

        vTaskDelayUntil(&last, period);
    }
}

// ── Task: CAN Transmit & Encode (50 Hz / 20 ms) ───────────────────
[[noreturn]] static void task_can_tx(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kCanTxHz);
    TickType_t last = xTaskGetTickCount();
    static bool was_in_signal_loss = false;
    static int hmi_heartbeat_counter = 0;

    while (1) {
        g_alive_can_tx.store(xTaskGetTickCount(), std::memory_order_relaxed);
        rm::RcSnapshot snap = g_rc.snapshot();

        // 1. Fail-Safe / Signal Loss Deadman Guard
        if (!snap.signal_valid) {
            if (!was_in_signal_loss) {
                ESP_LOGE(TAG, "RC Signal LOST! Asserting fail-safe ESTOP");
                was_in_signal_loss = true;

                // Broadcast SAFETY_ESTOP frame (0x001, DLC 0)
                can::Frame estop_fr;
                can::gen::SafetyEstop estop_msg{};
                if (can::gen::encode_safety_estop(estop_msg, estop_fr) == can::gen::CodecStatus::Ok) {
                    send_can_frame(estop_fr, "SAFETY_ESTOP");
                }
            }
        } else {
            if (was_in_signal_loss) {
                ESP_LOGI(TAG, "RC Signal recovered");
                was_in_signal_loss = false;
            }
        }

        // 2. Transmit Steering Setpoint -> 0x169 VCU_SES_REQ
        // Active when Ignition is ON and in Drive or Reverse
        bool drive_active = snap.signal_valid && snap.ignition &&
                           (snap.gear == can::Gear::D || snap.gear == can::Gear::R);

        can::custom::ses::Command ses_cmd{};
        ses_cmd.alignment_enable = snap.signal_valid && snap.ignition;
        ses_cmd.control_enable = drive_active;
        // Raw angle in 0.1 deg units (-3000 to +3000)
        int16_t angle_raw = static_cast<int16_t>(snap.steering_deg * 10.0f);
        ses_cmd.target_angle_raw = angle_raw;
        ses_cmd.target_speed_raw = 328; // Standard nominal speed
        ses_cmd.rolling_counter = g_roll_ses;
        g_roll_ses = (g_roll_ses + 1) & 0x0F;
        ses_cmd.vehicle_speed_raw = 0;

        can::Frame ses_fr;
        if (can::custom::ses::encode_command(ses_cmd, ses_fr) == can::gen::CodecStatus::Ok) {
            send_can_frame(ses_fr, "VCU_SES_REQ");
        }

        // 3. Transmit Brake Setpoint -> 0x7B9 VCU_SEB_REQ
        can::custom::seb::Command seb_cmd{};
        seb_cmd.alignment_enable = snap.signal_valid;
        seb_cmd.control_enable = snap.signal_valid;
        seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
        seb_cmd.auto_brake = false;

        // Raw stroke units: (mm - (-30.0)) / 0.05 = (mm + 30.0) * 20
        float commanded_stroke = snap.signal_valid ? snap.brake_stroke_mm : rm::kMaxBrakeStrokeMm;
        uint16_t stroke_raw = static_cast<uint16_t>((commanded_stroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        seb_cmd.stroke_request_raw = stroke_raw;
        seb_cmd.pressure_request_raw = 0;
        seb_cmd.rolling_counter = g_roll_seb;
        g_roll_seb = (g_roll_seb + 1) & 0x0F;

        can::Frame seb_fr;
        if (can::custom::seb::encode_command(seb_cmd, seb_fr) == can::gen::CodecStatus::Ok) {
            send_can_frame(seb_fr, "VCU_SEB_REQ");
        }

        // 4. Transmit Motor Command -> 0x204 RT_DRIVE_CMD & Legacy Fallback Frames (50 Hz)
        // Directly drives MTR on Low CAN bus when operating in standalone RM bypass mode
        int32_t target_motor_speed = 0;
        if (drive_active) {
            if (snap.gear == can::Gear::D) {
                target_motor_speed = static_cast<int32_t>(snap.speed_trim * shared::kMaxSpeedFwdMmps);
            } else if (snap.gear == can::Gear::R) {
                target_motor_speed = static_cast<int32_t>(snap.speed_trim * shared::kMaxSpeedRevMmps);
            }
        }

        // Canonical 0x204 RT_DRIVE_CMD (MTR receives speed setpoint + gear)
        can::gen::RtDriveCmd drive_cmd{};
        drive_cmd.motor_speed_mmps = target_motor_speed;
        drive_cmd.gear = static_cast<uint8_t>(snap.signal_valid && snap.ignition ? snap.gear : can::Gear::N);
        can::Frame drive_fr;
        if (can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr) == can::gen::CodecStatus::Ok) {
            send_can_frame(drive_fr, "RT_DRIVE_CMD");
        }

        // Fallback 0x0BB (Relay state) & 0x0AA (Throttle DAC code) for legacy hardware compatibility
        can::Frame relay_fr{};
        relay_fr.id = 0x0BB;
        relay_fr.dlc = 8;
        if (snap.signal_valid && snap.ignition) {
            if (snap.gear == can::Gear::D) {
                relay_fr.data[0] = 0x05; // Drive
            } else if (snap.gear == can::Gear::R) {
                relay_fr.data[0] = 0x09; // Reverse
            } else {
                relay_fr.data[0] = 0x03; // Park / Neutral
            }
        } else {
            relay_fr.data[0] = 0x00; // Ignition OFF
        }
        send_can_frame(relay_fr, "LEGACY_RELAY_STATE");

        can::Frame throttle_fr{};
        throttle_fr.id = 0x0AA;
        throttle_fr.dlc = 8;
        uint16_t raw_throttle = 0;
        if (drive_active) {
            raw_throttle = static_cast<uint16_t>(snap.speed_trim * 65535.0f);
        }
        throttle_fr.data[0] = static_cast<uint8_t>((raw_throttle >> 8) & 0xFF);
        throttle_fr.data[1] = static_cast<uint8_t>(raw_throttle & 0xFF);
        send_can_frame(throttle_fr, "LEGACY_THROTTLE");

        // 5. Transmit HMI Mode & Power Requests (1 Hz or on-change)
        if (++hmi_heartbeat_counter >= 50) { // 50 * 20ms = 1000ms (1 Hz)
            hmi_heartbeat_counter = 0;

            // 0x111 HMI_MODE_REQ
            can::gen::HmiModeReq mode_req{};
            mode_req.req_mode = (drive_active) ? 1 : 0; // 1 = AUTO / REMOTE, 0 = MANUAL
            mode_req.rolling_counter = ++g_roll_hmi_mode;
            can::Frame mode_fr;
            if (can::gen::encode_hmi_mode_req(mode_req, mode_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(mode_fr, "HMI_MODE_REQ");
            }

            // 0x112 HMI_PWR_REQ
            can::gen::HmiPwrReq pwr_req{};
            pwr_req.req_start = snap.ignition ? 1 : 0;
            pwr_req.rolling_counter = ++g_roll_hmi_pwr;
            can::Frame pwr_fr;
            if (can::gen::encode_hmi_pwr_req(pwr_req, pwr_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(pwr_fr, "HMI_PWR_REQ");
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Task: CAN Control & Bus-Off Recovery (50 Hz / 20 ms) ───────────
[[noreturn]] static void task_can_control(void*) {
    while (1) {
        g_can.service_recovery(esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── Task: Heartbeat & Status Diagnostics (10 Hz / 100 ms) ─────────
[[noreturn]] static void task_heartbeat(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kHeartbeatHz);
    TickType_t last = xTaskGetTickCount();

    while (1) {
        static uint32_t hb_count = 0;
        if (++hb_count % 10 == 0) {
            const auto snap = g_rc.snapshot();
            ESP_LOGI(TAG, "STATUS | Valid=%d Ign=%d Gear=%s Steer=%.1f Spd=%.2f | CH[0..5]=[%lu,%lu,%lu,%lu,%lu,%lu]us | CAN ok=%lu fail=%lu",
                     snap.signal_valid ? 1 : 0,
                     snap.ignition ? 1 : 0,
                     (snap.gear == can::Gear::D) ? "D" : ((snap.gear == can::Gear::R) ? "R" : "N"),
                     snap.steering_deg,
                     snap.speed_trim,
                     static_cast<unsigned long>(g_rc.raw_pulse_us(0)),
                     static_cast<unsigned long>(g_rc.raw_pulse_us(1)),
                     static_cast<unsigned long>(g_rc.raw_pulse_us(2)),
                     static_cast<unsigned long>(g_rc.raw_pulse_us(3)),
                     static_cast<unsigned long>(g_rc.raw_pulse_us(4)),
                     static_cast<unsigned long>(g_rc.raw_pulse_us(5)),
                     static_cast<unsigned long>(g_can_tx_ok.load(std::memory_order_relaxed)),
                     static_cast<unsigned long>(g_can_tx_fail.load(std::memory_order_relaxed)));
        }

        const auto health = g_can.health_snapshot();
        if (health.state == can::CanDriver::HealthState::BusOff) {
            ESP_LOGE(TAG, "CAN BUS-OFF detected! Recovery attempts: %lu", static_cast<unsigned long>(health.recovery_attempts));
        }

        vTaskDelayUntil(&last, period);
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  RM-ESP32 Receiver Gateway (C++17)");
    ESP_LOGI(TAG, "  Version: %s", rm::kFirmwareVersion);
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Initialize TWAI CAN Controller
    if (!g_can.init()) {
        ESP_LOGE(TAG, "CAN initialization failed! Rebooting...");
        esp_restart();
    }

    // 2. Initialize RMT Receiver Peripheral (6 Channels)
    if (!g_rc.init()) {
        ESP_LOGE(TAG, "RMT initialization failed! Rebooting...");
        esp_restart();
    }

    // 3. Spawn FreeRTOS Tasks
    xTaskCreatePinnedToCore(task_rc_capture, "rc_capture", 4096, nullptr, 8, nullptr, 1);
    xTaskCreatePinnedToCore(task_can_tx,     "can_tx",     4096, nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(task_can_control,"can_ctrl",   4096, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(task_heartbeat,  "heartbeat",  3072, nullptr, 1, nullptr, 1);

    ESP_LOGI(TAG, "All tasks created successfully. RM-ESP32 operational.");
}
