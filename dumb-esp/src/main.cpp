// dumb-esp — single-bus bare controller & fake e-trike simulator.
//
// Bridges high-level autonomy commands (Host/Autoware) and low-level actuators
// (SES, SEB, MTR) on a single shared CAN bus (TX=GPIO5, RX=GPIO4 @ 500 kbit/s).
//
// Satisfies and unlocks all 4 vehicle safety interlocks:
//   1. 0x210 RT_STATE_RPT (10 Hz): Confirms AUTO mode & 0 faults -> Host confirmed_auto = true.
//   2. 0x011 SYS_SAFETY_STS (10 Hz): Confirms ESTOP clear, HB alive, AUTOSAR CRC-8 -> Host & MTR unlocked.
//   3. 0x110 SYS_MODE_CMD (10 Hz): Sets MTR Mode to AUTO (1) -> MTR accepts CAN drive commands.
//   4. 0x113 SYS_PWR_CMD (10 Hz): Performs rising-edge 0->1 rearm -> MTR ignition enabled.
//   5. 0x121 RT_MOTION_RPT (20 Hz): High-rate motion report -> Satisfies Host 100ms watchdog.
//   6. 0x600 SYS_DIAG_RPT (1 Hz): Diagnostics report -> Host diagnostic monitor green.
//   7. 0x120 SYS_THROTTLE_STS (10 Hz): Speed feedback -> Host velocity reporting.
//   8. 0x169 VCU_SES_REQ (50 Hz): Slew-rated steer-by-wire commands.
//   9. 0x7B9 VCU_SEB_REQ (50 Hz): Brake-by-wire stroke/pressure commands.
//  10. 0x204 RT_DRIVE_CMD (100 Hz): Motor speed + gear setpoint.
//
// Smart coexistence:
//   Passively monitors 0x206 (MTR), 0x201 (SES), and 0x721 (SEB). If real physical
//   actuators are transmitting on the bus, dumb-esp suppresses its own simulated
//   feedback frames to eliminate CAN collisions. If absent, it simulates them.

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
#include "protocol/compat/e2e.hpp"

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
static std::atomic<uint8_t> g_cmd_lights{0};        // bits: 0=L, 1=R, 2=Brake, 3=Head

// ─── Real actuator presence & status (timestamps in microseconds) ───────────
static std::atomic<int64_t>  g_last_real_mtr_us{0};
static std::atomic<int64_t>  g_last_real_ses_us{0};
static std::atomic<int64_t>  g_last_real_seb_us{0};
static std::atomic<uint16_t> g_ses_actual_angle_raw{dumb::kSbwAngleOffset};
static std::atomic<uint8_t>  g_ses_mode_status{0};
static std::atomic<bool>     g_rearm_ses_req{false};

// ─── Outbound rolling counters (used only by control task) ───────────────────
static uint8_t g_roll_ses{0};
static uint8_t g_roll_seb{0};
static uint8_t g_roll_ses_sts{0};
static uint8_t g_roll_seb_sts{0};
static uint8_t g_roll_rt_hb{0};
static uint8_t g_roll_sys_hb{0};
static uint8_t g_roll_rt_motion{0};
static uint8_t g_roll_sys_safety{0};
static uint8_t g_roll_sys_mode{0};
static uint8_t g_roll_sys_pwr{0};

// Startup rearm counter for MTR (needs 2 frames of power=0 before power=1)
static uint8_t g_rearm_pwr_ticks{4};

// Startup rearm counter for SES (needs 200 ms of CtrlEnable=0 for rising edge)
static uint8_t g_rearm_ses_ticks{20};

// ─── Utilities ────────────────────────────────────────────────────────────────
static inline bool can_send(Frame& fr) {
    return g_can.send(fr, 2 /*timeout_ms*/);
}

static inline uint8_t calc_sum8(const uint8_t* d, int n) {
    uint8_t sum = 0;
    for (int i = 0; i < n; ++i) sum = static_cast<uint8_t>(sum + d[i]);
    return sum;
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
    const int64_t now_us = esp_timer_get_time();

    switch (fr.id) {

    // 0x300 HOST_DRIVE_CMD — speed(i32 BE), yaw_rate(i24 BE), gear(u8)
    case can::kIdHostDriveCmd:
        if (fr.dlc < 8) break;
        g_cmd_speed_mmps.store(be_read_i32(fr.data.data()), std::memory_order_relaxed);
        {
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

    // 0x302 HOST_LIGHT_CMD — left_turn(bit0), right_turn(bit1), brake_light(bit2), head(bit3)
    case can::kIdHostLightCmd:
        if (fr.dlc >= 1) {
            g_cmd_lights.store(fr.data[0] & 0x0Fu, std::memory_order_relaxed);
        }
        break;

    // 0x7FC HOST_HEARTBEAT — keepalive
    case can::kIdHostHeartbeat:
        break;

    // Passive listener for physical actuators (collision avoidance)
    case can::kIdMtrMotorFbk:
        g_last_real_mtr_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdSesStatus:
        if (fr.dlc >= 3) {
            const uint8_t mode = static_cast<uint8_t>((fr.data[0] >> 1u) & 0x03u);
            g_ses_mode_status.store(mode, std::memory_order_relaxed);
            const uint16_t ang = static_cast<uint16_t>(
                (static_cast<uint16_t>(fr.data[1]) << 8) | static_cast<uint16_t>(fr.data[2]));
            g_ses_actual_angle_raw.store(ang, std::memory_order_relaxed);
        }
        {
            const int64_t last = g_last_real_ses_us.load(std::memory_order_relaxed);
            if (last > 0 && (now_us - last > 250'000)) {
                g_rearm_ses_req.store(true, std::memory_order_relaxed);
            }
        }
        g_last_real_ses_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdSebStatus:
        g_last_real_seb_us.store(now_us, std::memory_order_relaxed);
        break;

    default:
        break;
    }
}

// ─── Outbound encoders ────────────────────────────────────────────────────────

// 0x169 VCU_SES_REQ — steer-by-wire command (50 Hz)
static void emit_ses(int16_t angle_raw, int32_t speed_mmps, bool armed) {
    Frame fr = Frame::standard(can::kIdVcuSesReq, 8);
    // Byte 0: CtrlEnable=1(bit1) on rising edge; AlignEn=0
    fr.data[0] = armed ? 0x02u : 0x00u;
    be_write_i16(&fr.data[1], angle_raw);                // target angle (offset 30000)
    be_write_u16(&fr.data[3], 328u);                     // nominal slew rate °/s
    fr.data[5] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_ses & 0x0Fu) << 4));
    g_roll_ses = (g_roll_ses + 1u) & 0x0Fu;
    // Byte 6: Vehicle speed (km/h) — keep >= 10 km/h when armed to prevent zero-speed motor sleep
    if (armed) {
        uint8_t kmh = static_cast<uint8_t>(std::abs(speed_mmps) * 36 / 10000);
        fr.data[6] = std::max<uint8_t>(10u, kmh);
    } else {
        fr.data[6] = 0u;
    }
    fr.data[7] = calc_sum8(fr.data.data(), 7);
    can_send(fr);
}

// 0x7B9 VCU_SEB_REQ — brake-by-wire command (50 Hz)
static void emit_seb(uint16_t stroke_raw) {
    Frame fr = Frame::standard(can::kIdVcuSebReq, 8);
    // Byte 0: AlignEn=0(bit0)|CtrlEn=1(bit1)|Mode=Stroke(0)|AutoBrake=0(bit3)
    fr.data[0] = 0x02u;
    be_write_u16(&fr.data[1], stroke_raw);
    fr.data[3] = 0u; // pressure request = 0 in Stroke mode
    fr.data[4] = 0u;
    fr.data[5] = 0u;
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_seb & 0x0Fu) << 4));
    g_roll_seb = (g_roll_seb + 1u) & 0x0Fu;
    fr.data[7] = calc_sum8(fr.data.data(), 7) ^ 0xFFu; // sum(B0..B6) ^ 0xFF
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

// 0x201 SES_STATUS — simulated steer feedback (50 Hz)
static void emit_ses_status(int16_t angle_raw) {
    Frame fr = Frame::standard(can::kIdSesStatus, 8);
    fr.data[0] = 0x03u; // AngleAligned=1, Mode=Auto(1)
    be_write_u16(&fr.data[1], static_cast<uint16_t>(angle_raw));
    be_write_u16(&fr.data[3], 328u);
    fr.data[5] = 121u;  // 0 Nm torque
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_ses_sts & 0x0Fu) << 4));
    g_roll_ses_sts = (g_roll_ses_sts + 1u) & 0x0Fu;
    fr.data[7] = calc_sum8(fr.data.data(), 7);
    can_send(fr);
}

// 0x721 SEB_STATUS — simulated brake feedback (50 Hz)
static void emit_seb_status(uint16_t stroke_raw) {
    Frame fr = Frame::standard(can::kIdSebStatus, 8);
    fr.data[0] = 0x03u; // Aligned=1(bit0), CtrlEn=1(bit1), Mode=Stroke(0)
    be_write_u16(&fr.data[1], stroke_raw);
    fr.data[3] = 0u;
    be_write_i16(&fr.data[4], 0);
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_seb_sts & 0x0Fu) << 4));
    g_roll_seb_sts = (g_roll_seb_sts + 1u) & 0x0Fu;
    fr.data[7] = calc_sum8(fr.data.data(), 7) ^ 0xFFu; // sum(B0..B6) ^ 0xFF
    can_send(fr);
}

// 0x310 STEER_DIAG — Autoware steering report provider (20 Hz)
static void emit_steer_diag(uint16_t angle_raw) {
    Frame fr = Frame::standard(can::kIdSteerDiag, 8);
    be_write_u16(&fr.data[0], angle_raw);  // raw angle: 30000 = 0.0°
    fr.data[2] = 0u;                       // fault = 0
    be_write_u16(&fr.data[3], 0u);         // motor_current = 0
    be_write_u16(&fr.data[5], 300u);       // ecu_temp = 30.0°C (0.1°C/bit)
    fr.data[7] = 0u;                       // reserved
    can_send(fr);
}

// 0x011 SYS_SAFETY_STS — unlocks Host interlock and MTR ignition (10 Hz)
static void emit_sys_safety_sts(uint8_t lights) {
    Frame fr = Frame::standard(can::kIdSysSafetySts, 5);
    fr.data[0] = 0u;                                     // estop_active = 0 (RELEASED)
    fr.data[1] = 1u;                                     // heartbeat_ok = 1 (ALIVE)
    fr.data[2] = lights;                                 // light feedback
    fr.data[3] = g_roll_sys_safety++;                    // rolling counter
    fr.data[4] = proto::e2e::sys_safety_sts_crc(fr.data.data()); // AUTOSAR CRC-8
    can_send(fr);
}

// 0x110 SYS_MODE_CMD — sets MTR to AUTO mode so it accepts CAN 0x204 (10 Hz)
static void emit_sys_mode_cmd() {
    Frame fr = Frame::standard(can::kIdSysModeCmd, 2);
    fr.data[0] = 1u;                                     // mode = AUTO (1)
    fr.data[1] = g_roll_sys_mode++;
    can_send(fr);
}

// 0x113 SYS_PWR_CMD — performs 0->1 rearm edge for MTR ignition (10 Hz)
static void emit_sys_pwr_cmd() {
    Frame fr = Frame::standard(can::kIdSysPwrCmd, 2);
    if (g_rearm_pwr_ticks > 0) {
        --g_rearm_pwr_ticks;
        fr.data[0] = 0u;                                 // initial power OFF
    } else {
        fr.data[0] = 1u;                                 // power ON
    }
    fr.data[1] = g_roll_sys_pwr++;
    can_send(fr);
}

// 0x210 RT_STATE_RPT — confirms AUTO mode to Host (/vehicle_bridge) (10 Hz)
static void emit_rt_state_rpt(bool reversing) {
    Frame fr = Frame::standard(can::kIdRtStateRpt, 6);
    fr.data[0] = 1u;                                     // mode = AUTO (1)
    fr.data[1] = 0u;                                     // safety_state = 0 (Healthy), estop_reason = 0
    fr.data[2] = reversing ? 1u : 0u;                    // reversing bit
    fr.data[3] = 0u;                                     // rx_overflow = 0
    fr.data[4] = 0xFFu;                                  // task_health = all healthy
    fr.data[5] = 2u;                                     // steer_state = STEER_ACTIVE (2)
    can_send(fr);
}

// 0x600 SYS_DIAG_RPT — Host diagnostics monitor keepalive (1 Hz)
static void emit_sys_diag_rpt(bool braking) {
    Frame fr = Frame::standard(can::kIdSysDiagRpt, 8);
    fr.data[0] = 1u;                                     // mode = AUTO
    fr.data[1] = braking ? 1u : 0u;                      // brake_engaged, brake_fault = 0
    fr.data[2] = 1u;                                     // heartbeat_ok = 1
    fr.data[3] = 0u;                                     // estop_active = 0
    be_write_u16(&fr.data[4], 120u);                     // free_heap_kb
    fr.data[6] = 0u;                                     // tec = 0
    fr.data[7] = 0u;                                     // rec = 0
    can_send(fr);
}

// 0x120 SYS_THROTTLE_STS — velocity report fallback (10 Hz)
static void emit_sys_throttle_sts(int16_t speed_mmps) {
    Frame fr = Frame::standard(can::kIdSysThrottleSts, 2);
    be_write_i16(fr.data.data(), speed_mmps);
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

// 0x121 RT_MOTION_RPT — vehicle motion telemetry (20 Hz, satisfies 100ms timeout)
static void emit_rt_motion(int16_t speed_mmps, int32_t yaw_mrad_s, uint8_t gear) {
    Frame fr = Frame::standard(can::kIdRtMotionRpt, 8);
    be_write_i16(fr.data.data(), speed_mmps);
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
    vTaskDelay(pdMS_TO_TICKS(dumb::kBootHoldMs));
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0u;

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        g_can.service_recovery(now_us);

        // ── Snapshot command state ───────────────────────────────────────
        const int32_t speed_in   = g_cmd_speed_mmps.load(std::memory_order_relaxed);
        const int32_t yaw_in     = g_cmd_yaw_mrad_s.load(std::memory_order_relaxed);
        const uint8_t gear_in    = g_cmd_gear.load(std::memory_order_relaxed);
        const int32_t brake_kpa  = g_cmd_brake_kpa.load(std::memory_order_relaxed);
        const int16_t steer_ang  = g_cmd_steer_0_1deg.load(std::memory_order_relaxed);
        const bool    steer_val  = g_cmd_steer_valid.load(std::memory_order_relaxed);
        uint8_t       lights_in  = g_cmd_lights.load(std::memory_order_relaxed);

        // Auto-set brake light if braking
        if (brake_kpa > 100) lights_in |= 0x04u;

        // Check real actuator presence (>200ms without packet = absent)
        const bool real_mtr_active = (now_us - g_last_real_mtr_us.load(std::memory_order_relaxed)) < 200'000;
        const bool real_ses_active = (now_us - g_last_real_ses_us.load(std::memory_order_relaxed)) < 200'000;
        const bool real_seb_active = (now_us - g_last_real_seb_us.load(std::memory_order_relaxed)) < 200'000;

        // SES Hot-Plug & Mode-Desync Auto-Recovery State Machine
        if (g_rearm_ses_req.exchange(false, std::memory_order_relaxed)) {
            g_rearm_ses_ticks = 20; // 200 ms disarm pulse on physical reconnect
        }
        if (real_ses_active && g_ses_mode_status.load(std::memory_order_relaxed) == 0 && g_rearm_ses_ticks == 0) {
            g_rearm_ses_ticks = 20; // 200 ms disarm pulse on Assist mode desync
        }
        const bool ses_armed = (g_rearm_ses_ticks == 0);
        if (g_rearm_ses_ticks > 0) {
            --g_rearm_ses_ticks;
        }

        // ── Resolve kinematics ───────────────────────────────────────────
        int32_t motor_speed_mmps;
        int16_t ses_angle_raw;

        if (steer_val) {
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
            rt::DriveCmd cmd{speed_in, yaw_in};
            rt::ResolvedSetpoint sp{};
            g_physics.resolve(cmd, sp);
            motor_speed_mmps = sp.motor_speed_mmps;
            int32_t ang_0_1deg = sp.steer_angle_mdeg / 100;
            ses_angle_raw = std::clamp(
                static_cast<int16_t>(ang_0_1deg + dumb::kSbwAngleOffset),
                dumb::kMinSteerRaw,
                dumb::kMaxSteerRaw);
        }

        // ── Brake stroke mapping: raw = (mm + 30.0) / 0.05; released=600, full=1140 ──
        const float stroke_mm = std::clamp(
            (static_cast<float>(brake_kpa) / static_cast<float>(shared::kMaxBrakeKpa))
                * dumb::kMaxBrakeStrokeMm,
            0.0f, dumb::kMaxBrakeStrokeMm);
        const uint16_t seb_stroke_raw = static_cast<uint16_t>(
            std::clamp(std::round((stroke_mm + dumb::kBrakeStrokeBias) / dumb::kBrakeStrokeScaleF),
                       static_cast<float>(dumb::kBrakeStrokeZeroRaw),
                       static_cast<float>(dumb::kMaxBrakeStrokeRaw)));

        // ── 100 Hz (every 10 ms) — motor drive command ───────────────────
        emit_rt_drive(motor_speed_mmps, gear_in);

        // ── 50 Hz (every 20 ms) — actuator commands + feedback ───────────
        if (tick % 2u == 0u) {
            emit_ses(ses_angle_raw, motor_speed_mmps, ses_armed);
            emit_seb(seb_stroke_raw);

            // Emit simulated feedback only if physical actuators are absent
            if (!real_mtr_active) emit_mtr_fbk(static_cast<int16_t>(motor_speed_mmps), gear_in);
            if (!real_ses_active) emit_ses_status(ses_angle_raw);
            if (!real_seb_active) emit_seb_status(seb_stroke_raw);
        }

        // ── 20 Hz (every 50 ms) — motion telemetry (satisfies 100ms Host timeout)
        if (tick % 5u == 0u) {
            const int32_t yaw_out = steer_val ? 0 : yaw_in;
            emit_rt_motion(static_cast<int16_t>(motor_speed_mmps), yaw_out, gear_in);

            // 0x310 STEER_DIAG: publishes steering angle so Autoware /vehicle/status/steering_status is fed
            const uint16_t active_angle_raw = real_ses_active
                ? g_ses_actual_angle_raw.load(std::memory_order_relaxed)
                : static_cast<uint16_t>(ses_angle_raw);
            emit_steer_diag(active_angle_raw);
        }

        // ── 10 Hz (every 100 ms) — vehicle supervisor & authority frames ──
        if (tick % 10u == 0u) {
            emit_sys_safety_sts(lights_in);                           // 0x011: Clears ESTOP, CRC-8
            emit_sys_mode_cmd();                                      // 0x110: MTR AUTO mode
            emit_sys_pwr_cmd();                                       // 0x113: MTR Power rearm
            emit_rt_state_rpt(gear_in == static_cast<uint8_t>(can::Gear::R)); // 0x210: Host confirmed_auto
            if (!real_mtr_active) {
                emit_sys_throttle_sts(static_cast<int16_t>(motor_speed_mmps)); // 0x120: Suppressed when real MTR active
            }
            emit_sys_heartbeat();                                     // 0x7FE: SYS heartbeat
        }

        // ── 2 Hz (every 500 ms) — slow heartbeat + serial log ─────────────
        if (tick % 50u == 0u) {
            emit_rt_heartbeat();
            ESP_LOGI(TAG, "spd=%5ld mm/s  ses=%d(arm=%d)  seb=%u  gear=%u  brk=%ld kPa  mtr_hw=%d",
                     static_cast<long>(motor_speed_mmps),
                     ses_angle_raw, ses_armed ? 1 : 0, seb_stroke_raw,
                     static_cast<unsigned>(gear_in),
                     static_cast<long>(brake_kpa),
                     real_mtr_active ? 1 : 0);
        }

        // ── 1 Hz (every 1000 ms) — system diagnostics report ──────────────
        if (tick % 100u == 0u) {
            emit_sys_diag_rpt(brake_kpa > 100);
        }

        ++tick;
        vTaskDelayUntil(&last_wake, kPeriod);
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "====================================================");
    ESP_LOGI(TAG, "  dumb-esp: Full Vehicle & Actuator Bridge");
    ESP_LOGI(TAG, "  Version: %s", FW_VERSION);
    ESP_LOGI(TAG, "  Bus: TX=GPIO%d  RX=GPIO%d  @ 500 kbit/s",
             dumb::kCanTxGpio, dumb::kCanRxGpio);
    ESP_LOGI(TAG, "  Unlocks: Autoware Gate (0x210, 0x011) + MTR (0x110, 0x113, CRC)");
    ESP_LOGI(TAG, "====================================================");

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
