// dumb-esp — single-bus bare controller & fake e-trike simulator.
//
// Receives high-level CAN commands from Host / RM-r12d in BARE mode on the
// shared CAN bus and:
//   • Resolves speed + yaw-rate through the RT bicycle kinematics model.
//   • Emits low-level actuator commands: SES (0x169), SEB (0x7B9), MTR (0x204).
//   • Echoes simulated actuator feedback so the Host sees a live vehicle:
//       SES_STATUS (0x201), SEB_STATUS (0x721), MTR_MOTOR_FBK (0x206),
//       RT_HEARTBEAT (0x7FD), SYS_HEARTBEAT (0x7FE), RT_MOTION_RPT (0x121).
//
// No safety interlocks, no ESTOP, no fault latches.
// Single TWAI bus: TX=GPIO5, RX=GPIO4 @ 500 kbit/s.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "can_driver.h"
#include "physics_model.h"
#include "protocol/compat/can_protocol.hpp"

namespace proto = etrike::protocol;
using proto::Frame;

static const char* TAG = "dumb";

// ─── CAN driver & physics model ──────────────────────────────────────────────
static can::CanDriver g_can{can::CanDriver::Config{
    dumb::kCanTxGpio,
    dumb::kCanRxGpio,
    dumb::kCanBitrateHz,
}};

static rt::PhysicsModel g_physics{};

// ─── Shared command state (written by RX task, read by control task) ─────────
static std::atomic<int32_t> g_cmd_speed_mmps{0};
static std::atomic<int32_t> g_cmd_yaw_mrad_s{0};
static std::atomic<uint8_t> g_cmd_gear{0};          // Gear::N = 0
static std::atomic<int32_t> g_cmd_brake_kpa{0};
static std::atomic<int16_t> g_cmd_steer_0_1deg{0};  // from HOST_STEER_CMD
static std::atomic<bool>    g_cmd_steer_valid{false};

// ─── Outbound rolling counters (used only by control task, no lock needed) ───
static uint8_t g_roll_ses{0};
static uint8_t g_roll_seb{0};
static uint8_t g_roll_ses_sts{0};
static uint8_t g_roll_seb_sts{0};
static uint8_t g_roll_rt_hb{0};
static uint8_t g_roll_sys_hb{0};
static uint8_t g_roll_rt_motion{0};

// ─── Utilities ────────────────────────────────────────────────────────────────
static inline bool can_send(Frame& fr) {
    return g_can.send(fr, 2 /*timeout_ms*/);
}

static inline uint8_t xor8_ff(const uint8_t* d, int n) {
    uint8_t x = 0;
    for (int i = 0; i < n; ++i) x ^= d[i];
    return x ^ 0xFFu;
}

static inline void be_write_i16(uint8_t* dst, int16_t v) {
    dst[0] = static_cast<uint8_t>(v >> 8);
    dst[1] = static_cast<uint8_t>(v);
}
static inline void be_write_u16(uint8_t* dst, uint16_t v) {
    dst[0] = static_cast<uint8_t>(v >> 8);
    dst[1] = static_cast<uint8_t>(v);
}
static inline void be_write_i32(uint8_t* dst, int32_t v) {
    dst[0] = static_cast<uint8_t>(v >> 24);
    dst[1] = static_cast<uint8_t>(v >> 16);
    dst[2] = static_cast<uint8_t>(v >> 8);
    dst[3] = static_cast<uint8_t>(v);
}

static inline int32_t be_read_i32(const uint8_t* s) {
    return static_cast<int32_t>(
        (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 |
        (uint32_t)s[2] << 8  | (uint32_t)s[3]);
}
static inline int16_t be_read_i16(const uint8_t* s) {
    return static_cast<int16_t>((uint16_t)s[0] << 8 | s[1]);
}

// ─── Inbound frame decoder ────────────────────────────────────────────────────
static void ingest(const Frame& fr) {
    switch (fr.id) {

    // 0x300 HOST_DRIVE_CMD — speed(i32 BE), yaw_rate(i24 BE), gear(u8)
    case can::kIdHostDriveCmd:
        if (fr.dlc < 8) break;
        g_cmd_speed_mmps.store(be_read_i32(fr.data.data()),  std::memory_order_relaxed);
        {
            // 24-bit signed big-endian yaw rate
            int32_t yaw = static_cast<int32_t>(
                (uint32_t)fr.data[4] << 16 |
                (uint32_t)fr.data[5] << 8  |
                (uint32_t)fr.data[6]);
            if (yaw & 0x800000) yaw |= static_cast<int32_t>(0xFF000000u);
            g_cmd_yaw_mrad_s.store(yaw, std::memory_order_relaxed);
        }
        g_cmd_gear.store(fr.data[7], std::memory_order_relaxed);
        break;

    // 0x301 HOST_BRAKE_REQ — brake_pressure_kpa(i32 BE)
    case can::kIdHostBrakeReq:
        if (fr.dlc < 4) break;
        {
            int32_t kpa = be_read_i32(fr.data.data());
            kpa = std::clamp(kpa, int32_t{0}, int32_t{shared::kMaxBrakeKpa});
            g_cmd_brake_kpa.store(kpa, std::memory_order_relaxed);
        }
        break;

    // 0x303 HOST_STEER_CMD — steer_angle_0_1deg(i16 BE), angle_valid(bit0 of byte2), roll
    case can::kIdHostSteerCmd:
        if (fr.dlc < 4) break;
        g_cmd_steer_0_1deg.store(be_read_i16(fr.data.data()), std::memory_order_relaxed);
        g_cmd_steer_valid.store((fr.data[2] & 0x01u) != 0u,   std::memory_order_relaxed);
        break;

    // 0x7FC HOST_HEARTBEAT — received, no watchdog action
    case can::kIdHostHeartbeat:
        break;

    default:
        break;
    }
}

// ─── Outbound encoders ────────────────────────────────────────────────────────

// 0x169 VCU_SES_REQ — steer-by-wire command (50 Hz)
static void emit_ses(int16_t angle_raw) {
    Frame fr = Frame::standard(can::kIdVcuSesReq, 8);
    fr.data[0] = 0x02u;                                  // CtrlEnable=1(bit1); AlignEn=0
    be_write_i16(&fr.data[1], angle_raw);                // target angle
    be_write_u16(&fr.data[3], 328u);                     // nominal slew rate °/s
    fr.data[5] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_ses & 0x0Fu) << 4));
    g_roll_ses = (g_roll_ses + 1u) & 0x0Fu;
    fr.data[6] = 0u;                                     // vehicle speed field
    fr.data[7] = xor8_ff(fr.data.data(), 7);
    can_send(fr);
}

// 0x7B9 VCU_SEB_REQ — brake-by-wire command in stroke mode (50 Hz)
static void emit_seb(uint16_t stroke_raw) {
    Frame fr = Frame::standard(can::kIdVcuSebReq, 8);
    // AlignEn=1(bit0), CtrlEn=1(bit1), Mode=Stroke(bit2=0), AutoBrk=0
    fr.data[0] = 0x03u;
    be_write_u16(&fr.data[1], stroke_raw);
    fr.data[3] = 0u;  // pressure request (not used in stroke mode)
    fr.data[4] = 0u;  // reserved
    fr.data[5] = 0u;  // reserved
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_seb & 0x0Fu) << 4));
    g_roll_seb = (g_roll_seb + 1u) & 0x0Fu;
    fr.data[7] = xor8_ff(fr.data.data(), 7);
    can_send(fr);
}

// 0x204 RT_DRIVE_CMD — motor speed + gear (100 Hz)
static void emit_rt_drive(int32_t speed_mmps, uint8_t gear) {
    Frame fr = Frame::standard(can::kIdRtDriveCmd, 5);
    be_write_i32(fr.data.data(), speed_mmps);
    fr.data[4] = gear;
    can_send(fr);
}

// 0x206 MTR_MOTOR_FBK — simulated motor feedback (50 Hz)
static void emit_mtr_fbk(int16_t speed_mmps, uint8_t gear) {
    Frame fr = Frame::standard(can::kIdMtrMotorFbk, 4);
    be_write_i16(fr.data.data(), speed_mmps);
    fr.data[2] = gear;
    fr.data[3] = shared::kMtrFaultStartupReady;  // bit4=ready, no faults
    can_send(fr);
}

// 0x201 SES_STATUS — simulated steer-by-wire feedback (50 Hz)
static void emit_ses_status(int16_t angle_raw) {
    Frame fr = Frame::standard(can::kIdSesStatus, 8);
    // AngleAligned=1(bit0), ControlMode=Auto=1(bits1-2), Error=0(bits6-7)
    fr.data[0] = 0x03u;
    be_write_u16(&fr.data[1], static_cast<uint16_t>(angle_raw));
    be_write_u16(&fr.data[3], 328u);                    // target speed echo
    fr.data[5] = 121u;                                  // torque = 0 Nm (raw: (0+12.1)/0.1)
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_ses_sts & 0x0Fu) << 4));
    g_roll_ses_sts = (g_roll_ses_sts + 1u) & 0x0Fu;
    fr.data[7] = xor8_ff(fr.data.data(), 7);
    can_send(fr);
}

// 0x721 SEB_STATUS — simulated brake-by-wire feedback (50 Hz)
static void emit_seb_status(uint16_t stroke_raw) {
    Frame fr = Frame::standard(can::kIdSebStatus, 8);
    // Align=1(bit0), CtrlEn=1(bit1), Mode=Stroke→1(bits2-3=0b01), AutoBrk=0, Error=0
    fr.data[0] = 0x07u;  // 0b0000_0111
    be_write_u16(&fr.data[1], stroke_raw);
    fr.data[3] = 0u;                                    // pressure echo = 0
    be_write_i16(&fr.data[4], 0);                       // angle echo = 0
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_seb_sts & 0x0Fu) << 4));
    g_roll_seb_sts = (g_roll_seb_sts + 1u) & 0x0Fu;
    fr.data[7] = xor8_ff(fr.data.data(), 7);
    can_send(fr);
}

// 0x7FD RT_HEARTBEAT — simulated RT heartbeat (2 Hz)
static void emit_rt_heartbeat() {
    Frame fr = Frame::standard(can::kIdRtHeartbeat, 2);
    fr.data[0] = g_roll_rt_hb++;
    fr.data[1] = 0x05u;  // bit0=heartbeat_ok, bit2=mode_auto
    can_send(fr);
}

// 0x7FE SYS_HEARTBEAT — simulated SYS heartbeat (10 Hz)
static void emit_sys_heartbeat() {
    Frame fr = Frame::standard(can::kIdSysHeartbeat, 2);
    fr.data[0] = g_roll_sys_hb++;
    fr.data[1] = 0x05u;  // bit0=heartbeat_ok, bit2=mode_auto
    can_send(fr);
}

// 0x121 RT_MOTION_RPT — vehicle motion telemetry (10 Hz)
static void emit_rt_motion(int16_t speed_mmps, int32_t yaw_mrad_s, uint8_t gear) {
    Frame fr = Frame::standard(can::kIdRtMotionRpt, 8);
    be_write_i16(fr.data.data(), speed_mmps);
    // 24-bit signed big-endian yaw rate
    fr.data[2] = static_cast<uint8_t>(yaw_mrad_s >> 16);
    fr.data[3] = static_cast<uint8_t>(yaw_mrad_s >> 8);
    fr.data[4] = static_cast<uint8_t>(yaw_mrad_s);
    fr.data[5] = gear;
    fr.data[6] = 0x07u;   // speed_valid=1, yaw_valid=1, gear_valid=1
    fr.data[7] = g_roll_rt_motion++;
    can_send(fr);
}

// ─── CAN RX task ─────────────────────────────────────────────────────────────
[[noreturn]] static void task_can_rx(void*) {
    Frame fr{};
    while (true) {
        if (g_can.receive(fr, 100 /*timeout_ms*/)) {
            ingest(fr);
        }
    }
}

// ─── 100 Hz control + TX task ────────────────────────────────────────────────
[[noreturn]] static void task_control(void*) {
    constexpr TickType_t kPeriod = pdMS_TO_TICKS(1000 / dumb::kControlHz);
    vTaskDelay(pdMS_TO_TICKS(dumb::kBootHoldMs));   // let bus settle
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0u;

    while (true) {
        g_can.service_recovery(esp_timer_get_time());

        // ── Snapshot command state ───────────────────────────────────────
        const int32_t speed_in   = g_cmd_speed_mmps.load(std::memory_order_relaxed);
        const int32_t yaw_in     = g_cmd_yaw_mrad_s.load(std::memory_order_relaxed);
        const uint8_t gear_in    = g_cmd_gear.load(std::memory_order_relaxed);
        const int32_t brake_kpa  = g_cmd_brake_kpa.load(std::memory_order_relaxed);
        const int16_t steer_ang  = g_cmd_steer_0_1deg.load(std::memory_order_relaxed);
        const bool    steer_val  = g_cmd_steer_valid.load(std::memory_order_relaxed);

        // ── Resolve kinematics ───────────────────────────────────────────
        int32_t motor_speed_mmps;
        int16_t ses_angle_raw;

        if (steer_val) {
            // Direct angle override from HOST_STEER_CMD (0x303)
            motor_speed_mmps = std::clamp(
                speed_in,
                -static_cast<int32_t>(shared::kMaxSpeedRevMmps),
                static_cast<int32_t>(shared::kMaxSpeedFwdMmps));
            int16_t ang = std::clamp(steer_ang, int16_t{-450}, int16_t{450});
            ses_angle_raw = std::clamp(
                static_cast<int16_t>(ang + dumb::kSbwAngleOffset),
                dumb::kMinSteerRaw,
                dumb::kMaxSteerRaw);
        } else {
            // Inverse bicycle model from HOST_DRIVE_CMD (0x300)
            rt::DriveCmd cmd{speed_in, yaw_in};
            rt::ResolvedSetpoint sp{};
            g_physics.resolve(cmd, sp);
            motor_speed_mmps = sp.motor_speed_mmps;
            // steer_angle_mdeg → 0.1° units → raw
            int32_t ang_0_1deg = sp.steer_angle_mdeg / 100;
            ses_angle_raw = std::clamp(
                static_cast<int16_t>(ang_0_1deg + dumb::kSbwAngleOffset),
                dumb::kMinSteerRaw,
                dumb::kMaxSteerRaw);
        }

        // ── Brake kPa → SEB stroke raw ───────────────────────────────────
        // Physical range: 0 mm (raw 600) → 27 mm (raw 1140)
        // raw = (mm + 30) / 0.05
        const float stroke_mm = std::clamp(
            (static_cast<float>(brake_kpa) / static_cast<float>(shared::kMaxBrakeKpa))
                * dumb::kMaxBrakeStrokeMm,
            0.0f, dumb::kMaxBrakeStrokeMm);
        const uint16_t seb_stroke_raw = static_cast<uint16_t>(
            (stroke_mm + dumb::kBrakeStrokeBias) / dumb::kBrakeStrokeScaleF);

        // ── 100 Hz — motor drive command ─────────────────────────────────
        emit_rt_drive(motor_speed_mmps, gear_in);

        // ── 50 Hz — actuator commands + simulated feedback ───────────────
        if (tick % 2u == 0u) {
            emit_ses(ses_angle_raw);
            emit_seb(seb_stroke_raw);
            emit_mtr_fbk(static_cast<int16_t>(motor_speed_mmps), gear_in);
            emit_ses_status(ses_angle_raw);
            emit_seb_status(seb_stroke_raw);
        }

        // ── 10 Hz — telemetry ────────────────────────────────────────────
        if (tick % 10u == 0u) {
            const int32_t yaw_out = steer_val ? 0 : yaw_in;
            emit_sys_heartbeat();
            emit_rt_motion(static_cast<int16_t>(motor_speed_mmps), yaw_out, gear_in);
        }

        // ── 2 Hz — slow heartbeat + log ──────────────────────────────────
        if (tick % 50u == 0u) {
            emit_rt_heartbeat();
            ESP_LOGI(TAG, "spd=%5ld mm/s  ses_raw=%d  seb_raw=%u  gear=%u  brake=%ld kPa",
                     static_cast<long>(motor_speed_mmps),
                     ses_angle_raw, seb_stroke_raw,
                     static_cast<unsigned>(gear_in),
                     static_cast<long>(brake_kpa));
        }

        ++tick;
        vTaskDelayUntil(&last_wake, kPeriod);
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "====================================================");
    ESP_LOGI(TAG, "  dumb-esp: Bare CAN Controller & Fake E-Trike");
    ESP_LOGI(TAG, "  Version: %s", FW_VERSION);
    ESP_LOGI(TAG, "  Bus: TX=GPIO%d  RX=GPIO%d  @ 500 kbit/s",
             dumb::kCanTxGpio, dumb::kCanRxGpio);
    ESP_LOGI(TAG, "====================================================");

    // NVS required by some ESP-IDF subsystems
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!g_can.init()) {
        ESP_LOGE(TAG, "CAN init failed — restarting");
        esp_restart();
    }

    xTaskCreatePinnedToCore(task_can_rx,  "can_rx",  4096, nullptr, 8 /*pri*/, nullptr, 1 /*core*/);
    xTaskCreatePinnedToCore(task_control, "ctrl",    4096, nullptr, 5 /*pri*/, nullptr, 0 /*core*/);
}
