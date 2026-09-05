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
static std::atomic<bool>     g_can_estop_latched{false};
// Tracks 0x001 SAFETY_ESTOP frames originated by this RM node.
// Incremented on successful TX, decremented in the RX loop within a 50ms window.
// Prevents RM from latching ESTOP on its own TWAI loopback while protecting against credit leaks.
static std::atomic<uint32_t> g_rm_self_estop_pending{0};
static std::atomic<int64_t>  g_rm_self_estop_tx_us{0};

// Rolling counters for protocol frames
static uint8_t g_roll_ses = 0;
static uint8_t g_roll_seb = 0;
static uint8_t g_roll_sys_mode = 0;
static uint8_t g_roll_sys_pwr = 0;

static bool send_can_frame(can::Frame& fr, const char* name) {
    if (!g_can.send(fr, 2)) {
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
    static int cmd_heartbeat_counter = 0;

    while (1) {
        g_alive_can_tx.store(xTaskGetTickCount(), std::memory_order_relaxed);
        rm::RcSnapshot snap = g_rc.snapshot();

        // Check latched emergency stop status
        bool estop_latched = g_can_estop_latched.load(std::memory_order_acquire);

        // Allow resetting latched ESTOP when RC signal is valid, ignition is switched OFF, and gear is in Neutral
        if (estop_latched && snap.signal_valid && !snap.ignition && snap.gear == can::Gear::N) {
            g_can_estop_latched.store(false, std::memory_order_release);
            estop_latched = false;
            ESP_LOGI(TAG, "ESTOP condition cleared via RC reset sequence (Ignition OFF + Gear Neutral)");
        }

        bool estop_or_signal_loss = !snap.signal_valid || estop_latched;

        // 1. Fail-Safe / Signal Loss Deadman Guard
        if (!snap.signal_valid) {
            if (!was_in_signal_loss) {
                ESP_LOGE(TAG, "RC Signal LOST! Asserting fail-safe ESTOP");
                was_in_signal_loss = true;

                // Broadcast SAFETY_ESTOP frame (0x001, DLC 0)
                can::Frame estop_fr;
                can::gen::SafetyEstop estop_msg{};
                if (can::gen::encode_safety_estop(estop_msg, estop_fr) == can::gen::CodecStatus::Ok) {
                    if (send_can_frame(estop_fr, "SAFETY_ESTOP")) {
                        g_rm_self_estop_tx_us.store(esp_timer_get_time(), std::memory_order_release);
                        g_rm_self_estop_pending.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        } else {
            if (was_in_signal_loss) {
                ESP_LOGI(TAG, "RC Signal recovered");
                was_in_signal_loss = false;
            }
        }

        // 2. Transmit Steering Setpoint -> 0x169 VCU_SES_REQ
        // Active when Signal is valid, not in ESTOP, Ignition is ON, and in Drive or Reverse
        bool drive_active = !estop_or_signal_loss && snap.ignition &&
                           (snap.gear == can::Gear::D || snap.gear == can::Gear::R);

        can::custom::ses::Command ses_cmd{};
        ses_cmd.alignment_enable = !estop_or_signal_loss && snap.ignition;
        ses_cmd.control_enable = drive_active;
        // Raw angle in 0.1 deg units + vendor offset (29550 to 30450)
        int16_t angle_raw = static_cast<int16_t>(rm::kSbwAngleOffset);
        if (drive_active) {
            angle_raw = static_cast<int16_t>(std::round(snap.steering_deg * 10.0f)) + static_cast<int16_t>(rm::kSbwAngleOffset);
            angle_raw = std::clamp(angle_raw, rm::kMinSteerRaw, rm::kMaxSteerRaw);
        }
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
        seb_cmd.alignment_enable = !estop_or_signal_loss;
        seb_cmd.control_enable = !estop_or_signal_loss;
        seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
        seb_cmd.auto_brake = false;

        // Raw stroke units: (mm - (-30.0)) / 0.05 = (mm + 30.0) * 20
        // When in ESTOP or signal loss, clamp immediately to maximum emergency brake stroke
        float commanded_stroke = (!estop_or_signal_loss) ? snap.brake_stroke_mm : rm::kMaxBrakeStrokeMm;
        uint16_t stroke_raw = static_cast<uint16_t>((commanded_stroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        seb_cmd.stroke_request_raw = stroke_raw;
        seb_cmd.pressure_request_raw = 0;
        seb_cmd.rolling_counter = g_roll_seb;
        g_roll_seb = (g_roll_seb + 1) & 0x0F;

        can::Frame seb_fr;
        if (can::custom::seb::encode_command(seb_cmd, seb_fr) == can::gen::CodecStatus::Ok) {
            send_can_frame(seb_fr, "VCU_SEB_REQ");
        }

        // 4. Transmit Motor Command -> 0x204 RT_DRIVE_CMD (50 Hz)
        // Directly drives MTR on Low CAN bus when operating in standalone RM bypass mode.
        // Gated off when under ESTOP or signal loss so MTR watchdog (500 ms) trips independently.
        if (!estop_or_signal_loss) {
            int32_t target_motor_speed = 0;
            // Brake-Over-Throttle interlock: zero throttle setpoint when mechanical brake > 5.0 mm
            constexpr float kBrakeCutoffStrokeMm = 5.0f;
            bool throttle_inhibited_by_brake = (snap.brake_stroke_mm > kBrakeCutoffStrokeMm);

            if (drive_active && !throttle_inhibited_by_brake) {
                if (snap.gear == can::Gear::D) {
                    target_motor_speed = static_cast<int32_t>(snap.throttle_norm * shared::kMaxSpeedFwdMmps);
                } else if (snap.gear == can::Gear::R) {
                    target_motor_speed = -static_cast<int32_t>(snap.throttle_norm * shared::kMaxSpeedRevMmps);
                }
            }

            // Canonical 0x204 RT_DRIVE_CMD (MTR receives speed setpoint + gear)
            can::gen::RtDriveCmd drive_cmd{};
            drive_cmd.motor_speed_mmps = target_motor_speed;
            drive_cmd.gear = static_cast<uint8_t>(snap.ignition ? snap.gear : can::Gear::N);
            can::Frame drive_fr;
            if (can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(drive_fr, "RT_DRIVE_CMD");
            }
        }

        // 5. Transmit authoritative SYS commands (emulated): 0x110 SYS_MODE_CMD + 0x113 SYS_PWR_CMD
        // RM is a development/test command emulator (not a second operational architecture). In
        // production SYS produces these; emulating them directly lets MTR/RT be exercised in
        // isolation without a real SYS node. RM must NOT emit the HMI request frames 0x111/0x112
        // (those are Host→RT→SYS requests terminated at SYS).
        static bool last_ignition = false;
        bool ignition_changed = (snap.ignition != last_ignition);
        last_ignition = snap.ignition;

        if (++cmd_heartbeat_counter >= 5 || ignition_changed) { // 5 * 20ms = 100ms (10 Hz)
            cmd_heartbeat_counter = 0;

            // 0x110 SYS_MODE_CMD (authoritative mode)
            can::gen::SysModeCmd mode_cmd{};
            mode_cmd.mode = drive_active ? uint8_t(can::Mode::Auto) : uint8_t(can::Mode::Manual);
            g_roll_sys_mode = (g_roll_sys_mode + 1) & 0xFF;
            mode_cmd.rolling_counter = g_roll_sys_mode;
            can::Frame mode_fr;
            if (can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(mode_fr, "SYS_MODE_CMD");
            }

            // 0x113 SYS_PWR_CMD (authoritative power)
            can::gen::SysPwrCmd pwr_cmd{};
            pwr_cmd.power_state = (!estop_or_signal_loss && snap.ignition) ? 1u : 0u;
            g_roll_sys_pwr = (g_roll_sys_pwr + 1) & 0xFF;
            pwr_cmd.rolling_counter = g_roll_sys_pwr;
            can::Frame pwr_fr;
            if (can::gen::encode_sys_pwr_cmd(pwr_cmd, pwr_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(pwr_fr, "SYS_PWR_CMD");
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Task: CAN Control & Bus-Off Recovery (50 Hz / 20 ms) ───────────
[[noreturn]] static void task_can_control(void*) {
    while (1) {
        g_can.service_recovery(esp_timer_get_time());

        // Drain incoming CAN messages to prevent queue overflow and process bus ESTOP.
        // NOTE: The TWAI node has no hardware acceptance filter, so RM receives all
        // frames including its own TX loopback. Self-sent 0x001 frames are discarded via
        // g_rm_self_estop_pending; all other self-echoed frames are silently dropped below.
        can::Frame rx_frame;
        while (g_can.receive(rx_frame, 0)) {
            if (rx_frame.id == 0x001u) { // SAFETY_ESTOP
                // Consume one self-sent credit if available within expected loopback window (~50 ms)
                const uint32_t pending = g_rm_self_estop_pending.load(std::memory_order_relaxed);
                const int64_t tx_us = g_rm_self_estop_tx_us.load(std::memory_order_acquire);
                const int64_t now_us = esp_timer_get_time();
                const int64_t elapsed_us = now_us - tx_us;

                if (pending > 0 && tx_us > 0 && elapsed_us >= 0 && elapsed_us < 50000) {
                    g_rm_self_estop_pending.fetch_sub(1, std::memory_order_relaxed);
                    continue; // Discard self-loopback frame
                }
                if (pending > 0) {
                    // Expire stale loopback credit to prevent swallowing future genuine ESTOPs
                    g_rm_self_estop_pending.store(0, std::memory_order_relaxed);
                }
                if (!g_can_estop_latched.load(std::memory_order_relaxed)) {
                    g_can_estop_latched.store(true, std::memory_order_release);
                    ESP_LOGE(TAG, "CAN SAFETY_ESTOP (0x001) received from external peer! Latching vehicle stop.");
                }
            }
            // All other IDs (incl. our own 0x204 loopback) are discarded here
        }

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
            ESP_LOGI(TAG, "STATUS | Valid=%d Ign=%d Gear=%s Steer=%.1f deg Brk=%.1f mm Throt=%.0f%% | CH[0..5]=[%lu,%lu,%lu,%lu,%lu,%lu]us | CAN ok=%lu fail=%lu",
                     snap.signal_valid ? 1 : 0,
                     snap.ignition ? 1 : 0,
                     (snap.gear == can::Gear::D) ? "D" : ((snap.gear == can::Gear::R) ? "R" : "N"),
                     snap.steering_deg,
                     snap.brake_stroke_mm,
                     snap.throttle_norm * 100.0f,
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
