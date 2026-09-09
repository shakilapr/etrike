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
#include "rc_receiver.h"

static const char* TAG = "rm_t12d";

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
// Prevents RM from latching ESTOP on its own TWAI loopback while protecting against credit leaks.
static std::atomic<uint32_t> g_rm_self_estop_pending{0};
static std::atomic<int64_t>  g_rm_self_estop_tx_us{0};

// Rolling counters for protocol frames
static uint8_t g_roll_ses = 0;
static uint8_t g_roll_seb = 0;
static uint8_t g_roll_sys_mode = 0;
static uint8_t g_roll_sys_pwr = 0;
#if defined(TESTING)
static uint8_t g_roll_sys_safety = 0;
#endif

static bool send_can_frame(can::Frame& fr, const char* name) {
    if (!g_can.send(fr, 2)) {
        g_can_tx_fail.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "CAN TX dropped: %s (ID %03lX)", name, static_cast<unsigned long>(fr.id));
        return false;
    }
    g_can_tx_ok.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ── Task: RC SBUS Capture (50 Hz / 20 ms) ─────────────────────────
[[noreturn]] static void task_rc_capture(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kRcCaptureHz);
    TickType_t last = xTaskGetTickCount();
    bool boot_warning_logged = false;
    uint32_t boot_start_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    while (1) {
        g_alive_capture.store(xTaskGetTickCount(), std::memory_order_relaxed);
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        g_rc.sample(now_ms);

        if (!boot_warning_logged && (now_ms - boot_start_ms) >= 2000) {
            auto snap = g_rc.snapshot();
            if (!snap.signal_valid) {
                ESP_LOGW(TAG, "No SBUS frames received after 2s! Verify R16F has RED + BLUE LEDs ON and transmitter separation is >= 1.0 m.");
                boot_warning_logged = true;
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Task: CAN Transmit & Encode (50 Hz / 20 ms) ───────────────────
[[noreturn]] static void task_can_tx(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / rm::kCanTxHz);
    TickType_t last = xTaskGetTickCount();
    static bool was_in_link_loss = false;
    static int cmd_heartbeat_counter = 0;
    static rm::ArmingTracker s_arming_tracker;
    static rm::ReversalTracker s_reversal_tracker;

    while (1) {
        g_alive_can_tx.store(xTaskGetTickCount(), std::memory_order_relaxed);
        rm::RcSnapshot snap = g_rc.snapshot();
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        // Check latched emergency stop status (ISO 13850: physical ESTOP unlatched by safety hardware only)
        bool estop_latched = g_can_estop_latched.load(std::memory_order_acquire);

        // Update Drive Arming State (SWA edge-qualified arming sequence)
        bool stick_neutral = (std::abs(snap.velocity_norm) <= 0.04f);
        bool link_healthy = (snap.link_state == rm::LinkState::Normal || snap.link_state == rm::LinkState::Degraded) && !snap.failsafe;
        s_arming_tracker.update(snap.drive_enable_req, stick_neutral, link_healthy && !estop_latched, now_ms);
        bool is_armed = s_arming_tracker.is_armed();

        // 1. Link State & Graduated Loss Handling
        if (snap.link_state == rm::LinkState::Lost) {
            if (!was_in_link_loss) {
                ESP_LOGW(TAG, "RC Link LOST or Failsafe active! Controlled stop requested, drive disarmed.");
                was_in_link_loss = true;
            }
        } else {
            if (was_in_link_loss) {
                ESP_LOGI(TAG, "RC Link recovered. Operator must toggle SWA UP -> DOWN to re-arm drive.");
                was_in_link_loss = false;
            }
        }

        // Active drive condition
        bool drive_active = is_armed && !estop_latched && !snap.park_hold_req && snap.signal_valid;

        // 2. Transmit Steering Setpoint -> 0x169 VCU_SES_REQ
        can::custom::ses::Command ses_cmd{};
        ses_cmd.alignment_enable = !estop_latched && snap.signal_valid;
        ses_cmd.control_enable = drive_active;
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
        seb_cmd.alignment_enable = !estop_latched;
        seb_cmd.control_enable = !estop_latched;
        seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
        seb_cmd.auto_brake = false;

        float commanded_stroke = 0.0f;
        if (estop_latched) {
            commanded_stroke = rm::kMaxBrakeStrokeMm;  // Emergency maximum brake stroke (27mm)
        } else if (snap.link_state == rm::LinkState::Lost) {
            commanded_stroke = rm::kParkBrakeStrokeMm; // Controlled safe stop (15mm)
        } else if (snap.park_hold_req) {
            commanded_stroke = rm::kParkBrakeStrokeMm; // Park / Brake Hold (15mm)
        } else {
            commanded_stroke = snap.brake_stroke_mm;  // Dynamic / Aux manual brake
        }

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
        int32_t target_motor_speed = 0;
        if (drive_active && !snap.reversal_locked) {
            target_motor_speed = snap.target_speed_mmps;
        }

        can::gen::RtDriveCmd drive_cmd{};
        drive_cmd.motor_speed_mmps = target_motor_speed;
        if (!drive_active || target_motor_speed == 0) {
            drive_cmd.gear = static_cast<uint8_t>(can::Gear::N);
        } else if (target_motor_speed > 0) {
            drive_cmd.gear = static_cast<uint8_t>(can::Gear::D);
        } else {
            drive_cmd.gear = static_cast<uint8_t>(can::Gear::R);
        }

        can::Frame drive_fr;
        if (can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr) == can::gen::CodecStatus::Ok) {
            send_can_frame(drive_fr, "RT_DRIVE_CMD");
        }

        // 5. Transmit authoritative SYS commands (emulated): 0x110 SYS_MODE_CMD + 0x113 SYS_PWR_CMD
        static bool last_armed = false;
        bool arm_changed = (is_armed != last_armed);
        last_armed = is_armed;

        uint8_t pwr_state = (!estop_latched && is_armed) ? 1u : 0u;
        uint8_t mode_state = (drive_active && snap.auto_mode_req) ? uint8_t(can::Mode::Auto) : uint8_t(can::Mode::Manual);

        if (++cmd_heartbeat_counter >= 5 || arm_changed) { // 5 * 20ms = 100ms (10 Hz)
            cmd_heartbeat_counter = 0;

            // 0x110 SYS_MODE_CMD
            can::gen::SysModeCmd mode_cmd{};
            mode_cmd.mode = mode_state;
            g_roll_sys_mode = (g_roll_sys_mode + 1) & 0xFF;
            mode_cmd.rolling_counter = g_roll_sys_mode;
            can::Frame mode_fr;
            if (can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(mode_fr, "SYS_MODE_CMD");
            }

            // 0x113 SYS_PWR_CMD
            can::gen::SysPwrCmd pwr_cmd{};
            pwr_cmd.power_state = pwr_state;
            g_roll_sys_pwr = (g_roll_sys_pwr + 1) & 0xFF;
            pwr_cmd.rolling_counter = g_roll_sys_pwr;
            can::Frame pwr_fr;
            if (can::gen::encode_sys_pwr_cmd(pwr_cmd, pwr_fr) == can::gen::CodecStatus::Ok) {
                send_can_frame(pwr_fr, "SYS_PWR_CMD");
            }
        }

        // 6. Optimal Serial CAN Command Display (Delta-triggered + Decimated)
        static struct {
            float   steer_deg{999.0f};
            float   brake_mm{999.0f};
            int32_t speed_mmps{999999};
            uint8_t gear{0xFF};
            bool    armed{false};
            bool    park{false};
            uint8_t env{0xFF};
            uint8_t link{0xFF};
            uint32_t count{0};
        } s_last_can_log;

        bool steer_changed = std::abs(snap.steering_deg - s_last_can_log.steer_deg) >= rm::kLogDeltaSteerDeg;
        bool brake_changed = std::abs(commanded_stroke - s_last_can_log.brake_mm) >= rm::kLogDeltaBrakeMm;
        bool speed_changed = std::abs(target_motor_speed - s_last_can_log.speed_mmps) >= rm::kLogDeltaSpeedMmps;
        bool gear_changed  = (drive_cmd.gear != s_last_can_log.gear);
        bool armed_changed = (is_armed != s_last_can_log.armed);
        bool park_changed  = (snap.park_hold_req != s_last_can_log.park);
        bool env_changed   = (static_cast<uint8_t>(snap.drive_envelope) != s_last_can_log.env);
        bool link_changed  = (static_cast<uint8_t>(snap.link_state) != s_last_can_log.link);
        bool periodic_tick = (++s_last_can_log.count >= rm::kCanLogDecimation);

        if (steer_changed || brake_changed || speed_changed || gear_changed ||
            armed_changed || park_changed || env_changed || link_changed || periodic_tick) {
            s_last_can_log.steer_deg   = snap.steering_deg;
            s_last_can_log.brake_mm    = commanded_stroke;
            s_last_can_log.speed_mmps  = target_motor_speed;
            s_last_can_log.gear        = drive_cmd.gear;
            s_last_can_log.armed       = is_armed;
            s_last_can_log.park        = snap.park_hold_req;
            s_last_can_log.env         = static_cast<uint8_t>(snap.drive_envelope);
            s_last_can_log.link        = static_cast<uint8_t>(snap.link_state);
            s_last_can_log.count       = 0;

            const char* env_str = (snap.drive_envelope == rm::DriveEnvelope::Precision) ? "PREC" :
                                  ((snap.drive_envelope == rm::DriveEnvelope::Normal) ? "NORM" : "FAST");
            const char* link_str = (snap.link_state == rm::LinkState::Normal) ? "OK" :
                                   ((snap.link_state == rm::LinkState::Degraded) ? "DEGR" : "LOST");
            const char* gear_str = (drive_cmd.gear == static_cast<uint8_t>(can::Gear::D)) ? "D" :
                                   ((drive_cmd.gear == static_cast<uint8_t>(can::Gear::R)) ? "R" : "N");

            if (estop_latched) {
                ESP_LOGW("can_tx", "[CAN TX | ESTOP] SES: raw=%d | SEB: raw=%u (27.0mm) | MTR: 0mm/s [N] | LINK: %s",
                         rm::kSbwAngleOffset, stroke_raw, link_str);
            } else {
                ESP_LOGI("can_tx", "[CAN TX] SES: %+.1f° | SEB: %.1fmm | MTR: %+dmm/s [%s] | ARM: %s | PARK: %s | ENV: %s | LINK: %s",
                         snap.steering_deg,
                         commanded_stroke,
                         target_motor_speed,
                         gear_str,
                         is_armed ? "YES" : "NO",
                         snap.park_hold_req ? "HOLD" : "OFF",
                         env_str,
                         link_str);
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Task: CAN Control & Bus-Off Recovery (50 Hz / 20 ms) ───────────
[[noreturn]] static void task_can_control(void*) {
    while (1) {
        g_can.service_recovery(esp_timer_get_time());

        can::Frame rx_frame;
        while (g_can.receive(rx_frame, 0)) {
            if (rx_frame.id == 0x001u) { // SAFETY_ESTOP
                const uint32_t pending = g_rm_self_estop_pending.load(std::memory_order_relaxed);
                const int64_t tx_us = g_rm_self_estop_tx_us.load(std::memory_order_acquire);
                const int64_t now_us = esp_timer_get_time();
                const int64_t elapsed_us = now_us - tx_us;

                if (pending > 0 && tx_us > 0 && elapsed_us >= 0 && elapsed_us < 50000) {
                    g_rm_self_estop_pending.fetch_sub(1, std::memory_order_relaxed);
                    continue; // Discard self-loopback frame
                }
                if (pending > 0) {
                    g_rm_self_estop_pending.store(0, std::memory_order_relaxed);
                }
                if (!g_can_estop_latched.load(std::memory_order_relaxed)) {
                    g_can_estop_latched.store(true, std::memory_order_release);
                    ESP_LOGE(TAG, "CAN SAFETY_ESTOP (0x001) received from external peer! Latching vehicle stop.");
                }
            }
#if !defined(TESTING)
            else if (rx_frame.id == can::kIdSysSafetySts) { // 0x011 SYS_SAFETY_STS
                if (rx_frame.dlc >= 5u &&
                    rx_frame.data[4] == ::etrike::protocol::e2e::sys_safety_sts_crc(rx_frame.data.data())) {
                    if (rx_frame.data[0] != 0u &&
                        !g_can_estop_latched.load(std::memory_order_relaxed)) {
                        g_can_estop_latched.store(true, std::memory_order_release);
                        ESP_LOGE(TAG, "SYS_SAFETY_STS (0x011) estop_active=1 received! Latching vehicle stop.");
                    }
                }
            }
#endif
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
            ESP_LOGI(TAG, "STATUS | Valid=%d FS=%d Lost=%d Ign=%d Gear=%s Steer=%.1f deg Brk=%.1f mm Throt=%.0f%% VRA=%.2f | CH[1..8]=[%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu]us | CAN ok=%lu fail=%lu",
                     snap.signal_valid ? 1 : 0,
                     snap.failsafe ? 1 : 0,
                     snap.frame_lost ? 1 : 0,
                     snap.ignition ? 1 : 0,
                     (snap.gear == can::Gear::D) ? "D" : ((snap.gear == can::Gear::R) ? "R" : "N"),
                     snap.steering_deg,
                     snap.brake_stroke_mm,
                     snap.throttle_norm * 100.0f,
                     snap.dial_vra,
                     static_cast<unsigned long>(snap.pulse_us[0]),
                     static_cast<unsigned long>(snap.pulse_us[1]),
                     static_cast<unsigned long>(snap.pulse_us[2]),
                     static_cast<unsigned long>(snap.pulse_us[3]),
                     static_cast<unsigned long>(snap.pulse_us[4]),
                     static_cast<unsigned long>(snap.pulse_us[5]),
                     static_cast<unsigned long>(snap.pulse_us[6]),
                     static_cast<unsigned long>(snap.pulse_us[7]),
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
    xTaskCreatePinnedToCore(task_can_control,"can_ctrl",   4096, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(task_heartbeat,  "heartbeat",  3072, nullptr, 1, nullptr, 1);

    ESP_LOGI(TAG, "All tasks created successfully. RM-ESP32-T12D operational.");
}
