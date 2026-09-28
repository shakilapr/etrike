// dumb-esp — Dual-Bus Autonomous Bridge & Controller.
//
// Bridges high-level autonomy commands (Jetson Autoware) and low-level actuators
// (SES, SEB, MTR) across two isolated physical CAN buses:
//   - High CAN Bus (MCP2515 SPI @ 500 kbps): Interfaces Jetson Host (autoware_vehicle_bridge).
//   - Low CAN Bus (TWAI @ 500 kbps): Directly controls physical actuators in Bare Mode.
//
// Satisfies and unlocks all 4 Autoware safety interlocks on High CAN:
//   1. 0x210 RT_STATE_RPT (10 Hz): Confirms AUTO mode & 0 faults -> confirmed_auto = true.
//   2. 0x011 SYS_SAFETY_STS (10 Hz): Confirms ESTOP clear, HB alive, AUTOSAR CRC-8 -> unlocks gates.
//   3. 0x121 RT_MOTION_RPT (100 Hz): High-rate motion report -> Satisfies Host 100ms watchdog.
//   4. 0x7FD RT_HEARTBEAT & 0x7FE SYS_HEARTBEAT (2 Hz): Satisfies Host heartbeat watchdogs.
//   5. 0x310 STEER_DIAG (20 Hz): Steering feedback -> Feeds Autoware steering status.
//   6. 0x600 SYS_DIAG_RPT (1 Hz): Diagnostics report -> Host diagnostic monitor green.
//   7. 0x120 SYS_THROTTLE_STS (10 Hz): Velocity reporting.
//
// Controls physical actuators on Low CAN (matching rm-esp32-t12d emit_bare):
//   1. 0x169 VCU_SES_REQ (50 Hz): Slew-rated steer-by-wire commands with 200 ms rearm pulse.
//   2. 0x7B9 VCU_SEB_REQ (50 Hz): Brake-by-wire stroke commands (raw 600..1140).
//   3. 0x204 RT_DRIVE_CMD (100 Hz): Motor speed + gear setpoint.
//   4. 0x110 SYS_MODE_CMD (10 Hz): Sets MTR Mode to AUTO (1).
//   5. 0x113 SYS_PWR_CMD (10 Hz): Performs rising-edge 0->1 rearm.
//   6. 0x011 SYS_SAFETY_STS (10 Hz): Estop clear, heartbeat alive, AUTOSAR CRC-8.
//
// Coexistence:
//   If physical actuators transmit feedback on Low CAN (0x201, 0x206, 0x721), their actual
//   readings are reflected into High CAN telemetry. If absent, an internal physics model
//   provides smooth kinematics so Jetson operates seamlessly even on the bench.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "can_driver.h"
#include "can_driver_mcp2515.h"
#include "physics_model.h"
#include "protocol/compat/can_protocol.hpp"
#include "protocol/compat/e2e.hpp"
#include "shared/status_led.h"

namespace proto = etrike::protocol;
using proto::Frame;

static const char* TAG = "dumb_bridge";

// ─── CAN Drivers ─────────────────────────────────────────────────────────────
// Low CAN (TWAI on GPIO 5 TX, GPIO 4 RX @ 500k -> Actuators)
static can::CanDriver g_can_low{can::CanDriver::Config{
    dumb::kCanLowTxGpio,
    dumb::kCanLowRxGpio,
    dumb::kCanLowBitrateHz,
}};

// High CAN (MCP2515 on SPI -> Jetson Host)
static dumb::Mcp2515Driver g_can_high{dumb::Mcp2515Driver::Config{
    dumb::kSpiSckGpio,
    dumb::kSpiMosiGpio,
    dumb::kSpiMisoGpio,
    dumb::kSpiCsGpio,
    dumb::kMcpIntGpio,
    dumb::kSpiHost,
    8'000'000,
}};

static rt::PhysicsModel g_physics{};
static shared::led::Ws2812Strip g_status_led{};

// ─── Shared Command State (High CAN RX -> Control Task) ──────────────────────
static std::atomic<int32_t> g_cmd_speed_mmps{0};
static std::atomic<int32_t> g_cmd_yaw_mrad_s{0};
static std::atomic<uint8_t> g_cmd_gear{0};
static std::atomic<int32_t> g_cmd_brake_kpa{0};
static std::atomic<int16_t> g_cmd_steer_0_1deg{0};
static std::atomic<bool>    g_cmd_steer_valid{false};
static std::atomic<uint8_t> g_cmd_lights{0};
static std::atomic<int64_t> g_last_host_cmd_us{0};
static std::atomic<bool>    g_mode_auto{true};

// ─── Real Actuator State (Low CAN RX -> Control Task) ────────────────────────
static std::atomic<int64_t>  g_last_real_mtr_us{0};
static std::atomic<int64_t>  g_last_real_ses_us{0};
static std::atomic<int64_t>  g_last_real_seb_us{0};
static std::atomic<uint16_t> g_ses_actual_angle_raw{dumb::kSbwAngleOffset};
static std::atomic<int16_t>  g_mtr_actual_speed_mmps{0};
static std::atomic<uint8_t>  g_ses_mode_status{0};
static std::atomic<bool>     g_rearm_ses_req{false};

// ─── Rolling Counters ────────────────────────────────────────────────────────
static uint8_t g_roll_ses{0};
static uint8_t g_roll_seb{0};
static uint8_t g_roll_rt_hb{0};
static uint8_t g_roll_sys_hb{0};
static uint8_t g_roll_rt_motion{0};
static uint8_t g_roll_sys_safety_low{0};
static uint8_t g_roll_sys_safety_high{0};
static uint8_t g_roll_sys_mode{0};
static uint8_t g_roll_sys_pwr{0};
static uint8_t g_roll_rt_diag{0};

static uint8_t g_rearm_pwr_ticks{4};  // MTR ignition rearm pulse
static uint8_t g_rearm_ses_ticks{20}; // SES rising edge pulse (200 ms)

// ─── Helpers ─────────────────────────────────────────────────────────────────
static inline uint8_t calc_sum8(const uint8_t* d, int n) {
    uint8_t sum = 0;
    for (int i = 0; i < n; ++i) sum = static_cast<uint8_t>(sum + d[i]);
    return sum;
}

static inline uint8_t calc_xor8_ff(const uint8_t* d, int n) {
    uint8_t val = 0;
    for (int i = 0; i < n; ++i) val ^= d[i];
    return static_cast<uint8_t>(val ^ 0xFFu);
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

// ─── Inbound Frame Decoders ──────────────────────────────────────────────────
static void ingest_high(const Frame& fr) {
    const int64_t now_us = esp_timer_get_time();

    switch (fr.id) {
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
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdHostBrakeReq:
        if (fr.dlc < 4) break;
        {
            int32_t kpa = be_read_i32(fr.data.data());
            kpa = std::clamp(kpa, int32_t{0}, int32_t{shared::kMaxBrakeKpa});
            g_cmd_brake_kpa.store(kpa, std::memory_order_relaxed);
        }
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdHostSteerCmd:
        if (fr.dlc < 4) break;
        g_cmd_steer_0_1deg.store(be_read_i16(fr.data.data()), std::memory_order_relaxed);
        g_cmd_steer_valid.store((fr.data[2] & 0x01u) != 0u,   std::memory_order_relaxed);
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdHostLightCmd:
        if (fr.dlc >= 1) {
            g_cmd_lights.store(fr.data[0] & 0x0Fu, std::memory_order_relaxed);
        }
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdHostHeartbeat:
    case can::kIdHmiPwrReq:
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        break;

    case can::kIdHmiModeReq:
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        if (fr.dlc >= 1) {
            g_mode_auto.store(fr.data[0] != 0, std::memory_order_relaxed);
        }
        break;

    case can::kIdHostEstopResetReq:
        g_last_host_cmd_us.store(now_us, std::memory_order_relaxed);
        if (fr.dlc >= 1) {
            Frame rsp = Frame::standard(can::kIdSysEstopResetRsp, 4);
            rsp.data[0] = fr.data[0]; // echo sequence
            rsp.data[1] = 1u;         // success = 1
            rsp.data[2] = 0u;         // blocker_mask low byte
            rsp.data[3] = 0u;         // blocker_mask high byte
            g_can_high.send(rsp, 2);
        }
        break;

    default:
        break;
    }
}

static void ingest_low(const Frame& fr) {
    const int64_t now_us = esp_timer_get_time();

    switch (fr.id) {
    case can::kIdHmiModeReq:
        if (fr.dlc >= 1) {
            g_mode_auto.store(fr.data[0] != 0, std::memory_order_relaxed);
        }
        break;

    case can::kIdMtrMotorFbk:
        if (fr.dlc >= 2) {
            g_mtr_actual_speed_mmps.store(be_read_i16(fr.data.data()), std::memory_order_relaxed);
        }
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

// ─── Low CAN Outbound Actuator Frames (Bare Mode) ────────────────────────────

// 0x169 VCU_SES_REQ — Steer actuator (50 Hz)
static void emit_low_ses(int16_t angle_raw, int32_t speed_mmps, bool armed) {
    Frame fr = Frame::standard(can::kIdVcuSesReq, 8);
    fr.data[0] = armed ? 0x02u : 0x00u; // CtrlEnable=1 on rising edge
    be_write_i16(&fr.data[1], angle_raw);
    be_write_u16(&fr.data[3], 328u);    // nominal slew rate °/s
    fr.data[5] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_ses & 0x0Fu) << 4));
    g_roll_ses = (g_roll_ses + 1u) & 0x0Fu;
    if (armed) {
        uint8_t kmh = static_cast<uint8_t>(std::abs(speed_mmps) * 36 / 10000);
        fr.data[6] = std::max<uint8_t>(10u, kmh); // >= 10 km/h prevents motor shutdown
    } else {
        fr.data[6] = 0u;
    }
    fr.data[7] = calc_xor8_ff(fr.data.data(), 7);
    g_can_low.send(fr, 2);
}

// 0x7B9 VCU_SEB_REQ — Brake actuator (50 Hz)
static void emit_low_seb(uint16_t stroke_raw, bool armed) {
    Frame fr = Frame::standard(can::kIdVcuSebReq, 8);
    fr.data[0] = armed ? 0x02u : 0x00u; // AlignEn=0, CtrlEn=1 only when armed
    be_write_u16(&fr.data[1], stroke_raw);
    fr.data[3] = 0u;
    fr.data[4] = 0u;
    fr.data[5] = 0u;
    fr.data[6] = static_cast<uint8_t>(0x03u | (static_cast<uint8_t>(g_roll_seb & 0x0Fu) << 4));
    g_roll_seb = (g_roll_seb + 1u) & 0x0Fu;
    fr.data[7] = calc_xor8_ff(fr.data.data(), 7);
    g_can_low.send(fr, 2);
}

// 0x204 RT_DRIVE_CMD — Motor speed + gear setpoint (100 Hz)
static void emit_low_rt_drive(int32_t speed_mmps, uint8_t gear) {
    Frame fr = Frame::standard(can::kIdRtDriveCmd, 5);
    be_write_i32(fr.data.data(), speed_mmps);
    fr.data[4] = gear;
    g_can_low.send(fr, 2);
}

// 0x110 SYS_MODE_CMD — Enables MTR AUTO mode for CAN control (10 Hz)
static void emit_low_sys_mode() {
    Frame fr = Frame::standard(can::kIdSysModeCmd, 2);
    fr.data[0] = 1u; // AUTO (1)
    fr.data[1] = g_roll_sys_mode++;
    g_can_low.send(fr, 2);
}

// 0x113 SYS_PWR_CMD — Performs 0->1 rearm edge for MTR ignition (10 Hz)
static void emit_low_sys_pwr() {
    Frame fr = Frame::standard(can::kIdSysPwrCmd, 2);
    if (g_rearm_pwr_ticks > 0) {
        --g_rearm_pwr_ticks;
        fr.data[0] = 0u; // initial power OFF
    } else {
        fr.data[0] = 1u; // power ON
    }
    fr.data[1] = g_roll_sys_pwr++;
    g_can_low.send(fr, 2);
}

// 0x011 SYS_SAFETY_STS — Unlocks MTR ignition via AUTOSAR CRC-8 (10 Hz)
static void emit_low_sys_safety(uint8_t lights) {
    Frame fr = Frame::standard(can::kIdSysSafetySts, 5);
    fr.data[0] = 0u; // estop_active = 0
    fr.data[1] = 1u; // heartbeat_ok = 1
    fr.data[2] = lights;
    fr.data[3] = g_roll_sys_safety_low++;
    fr.data[4] = proto::e2e::sys_safety_sts_crc(fr.data.data());
    g_can_low.send(fr, 2);
}

// ─── High CAN Outbound Telemetry (Satisfying Jetson Autoware) ────────────────

// 0x121 RT_MOTION_RPT — High-rate motion report (100 Hz, satisfies 100ms watchdog)
static void emit_high_rt_motion(int16_t speed_mmps, int32_t yaw_mrad_s, uint8_t gear) {
    Frame fr = Frame::standard(can::kIdRtMotionRpt, 8);
    be_write_i16(fr.data.data(), speed_mmps);
    fr.data[2] = static_cast<uint8_t>(yaw_mrad_s >> 16);
    fr.data[3] = static_cast<uint8_t>(yaw_mrad_s >> 8);
    fr.data[4] = static_cast<uint8_t>(yaw_mrad_s);
    fr.data[5] = (gear <= 3) ? gear : 1u; // 0=N, 1=D, 2=S, 3=R
    fr.data[6] = 0x07u; // speed_valid=1, yaw_valid=1, gear_valid=1
    fr.data[7] = g_roll_rt_motion++;
    g_can_high.send(fr, 2);
}

// 0x501 RT_NODE_STATUS — RT node state (10 Hz, both buses)
static void emit_rt_node_status() {
    Frame fr = Frame::standard(0x501u, 8);
    fr.data[0] = 3u;    // node_state = ACTIVE (3)
    fr.data[1] = 0u;    // block_mask low
    fr.data[2] = 0u;    // block_mask high
    fr.data[3] = 0x12u; // ready=1, output_enabled=1
    fr.data[4] = 0u;
    fr.data[5] = 0u;
    fr.data[6] = 0u;
    fr.data[7] = 0u;
    g_can_high.send(fr, 2);
    g_can_low.send(fr, 2);
}

// 0x500 SYS_NODE_STATUS — SYS node state (10 Hz, both buses)
static void emit_sys_node_status() {
    Frame fr = Frame::standard(0x500u, 8);
    fr.data[0] = 3u;    // node_state = ACTIVE (3)
    fr.data[1] = 0u;    // block_mask low
    fr.data[2] = 0u;    // block_mask high
    fr.data[3] = 0x12u; // ready=1 (bit 1), output_enabled=1 (bit 4)
    fr.data[4] = 0u;
    fr.data[5] = 0u;
    fr.data[6] = 0u;
    fr.data[7] = 0u;
    g_can_high.send(fr, 2);
    g_can_low.send(fr, 2);
}

// 0x310 STEER_DIAG — Steer feedback report to Autoware (20 Hz)
static void emit_high_steer_diag(uint16_t angle_raw) {
    Frame fr = Frame::standard(can::kIdSteerDiag, 8);
    be_write_u16(&fr.data[0], angle_raw); // 30000 = 0.0°
    fr.data[2] = 0u;                      // fault = 0
    be_write_u16(&fr.data[3], 0u);        // motor_current = 0
    be_write_u16(&fr.data[5], 300u);      // ecu_temp = 30.0°C
    fr.data[7] = 0u;
    g_can_high.send(fr, 2);
}

// 0x210 RT_STATE_RPT — Confirms mode to Autoware & Control Toolkit (10 Hz)
static void emit_rt_state(bool reversing, bool host_active) {
    Frame fr = Frame::standard(can::kIdRtStateRpt, 6);
    fr.data[0] = (g_mode_auto.load(std::memory_order_relaxed) && host_active) ? 1u : 0u; // mode: 1=AUTO, 0=MANUAL
    fr.data[1] = 0u;                      // safety_state = 0 (Healthy), estop_reason = 0
    fr.data[2] = reversing ? 1u : 0u;
    fr.data[3] = 0u;                      // rx_overflow = 0
    fr.data[4] = 0xFFu;                   // task_health = all healthy
    fr.data[5] = host_active ? 2u /*STEER_ACTIVE*/ : 1u /*STEER_LISTEN_SYNC*/;
    g_can_high.send(fr, 2);
    g_can_low.send(fr, 2);
}

// 0x011 SYS_SAFETY_STS — Confirms ESTOP clear & HB OK to Autoware (10 Hz)
static void emit_high_sys_safety(uint8_t lights) {
    Frame fr = Frame::standard(can::kIdSysSafetySts, 5);
    fr.data[0] = 0u; // estop_active = 0
    fr.data[1] = 1u; // heartbeat_ok = 1
    fr.data[2] = lights;
    fr.data[3] = g_roll_sys_safety_high++;
    fr.data[4] = proto::e2e::sys_safety_sts_crc(fr.data.data());
    g_can_high.send(fr, 2);
}

// 0x120 SYS_THROTTLE_STS — Velocity report (10 Hz, both buses)
static void emit_sys_throttle(int16_t speed_mmps) {
    Frame fr = Frame::standard(can::kIdSysThrottleSts, 2);
    be_write_i16(fr.data.data(), speed_mmps);
    g_can_high.send(fr, 2);
    g_can_low.send(fr, 2);
}

// 0x7FD RT_HEARTBEAT — RT keepalive (2 Hz)
static void emit_rt_heartbeat() {
    Frame fr = Frame::standard(can::kIdRtHeartbeat, 2);
    fr.data[0] = g_roll_rt_hb++;
    fr.data[1] = 0xFDu; // bit0=heartbeat_ok, bit1=estop(0), bit2=mode_auto, bit3=can_ok, bit4-7=tasks_ok
    g_can_high.send(fr, 2);
    g_can_low.send(fr, 2);
}

// 0x7FE SYS_HEARTBEAT — SYS keepalive (2 Hz, Low bus ONLY per protocol contract)
static void emit_sys_heartbeat() {
    Frame fr = Frame::standard(can::kIdSysHeartbeat, 2);
    fr.data[0] = g_roll_sys_hb++;
    fr.data[1] = 0xFDu; // bit0=heartbeat_ok, bit1=estop(0), bit2=mode_auto, bit3=can_ok, bit4-7=tasks_ok
    g_can_low.send(fr, 2);
}

// 0x600 SYS_DIAG_RPT — Host diagnostic keepalive (1 Hz)
static void emit_sys_diag(bool braking) {
    Frame fr = Frame::standard(can::kIdSysDiagRpt, 8);
    fr.data[0] = g_mode_auto.load(std::memory_order_relaxed) ? 1u : 0u; // mode
    fr.data[1] = braking ? 1u : 0u;
    fr.data[2] = 1u; // heartbeat_ok = 1
    fr.data[3] = 0u; // estop_active = 0
    be_write_u16(&fr.data[4], 120u); // free_heap_kb
    fr.data[6] = 0u; // tec = 0
    fr.data[7] = 0u; // rec = 0
    g_can_high.send(fr, 2);
    g_can_low.send(fr, 2);
}

// 0x620 RT_DIAG_RPT — RT diagnostic keepalive (1 Hz)
static void emit_rt_diag() {
    Frame fr = Frame::standard(0x620u, 8);
    fr.data[0] = 0u; // mcp_eflg
    fr.data[1] = 0u; // mcp_tec
    fr.data[2] = 0u; // mcp_rec
    fr.data[3] = 0u; // flags
    fr.data[4] = 0u; // brake_fallback_state
    fr.data[5] = 0u; // spi_fault_delta
    fr.data[6] = 0u; // mcp_recovery_attempts
    fr.data[7] = g_roll_rt_diag++;
    g_can_high.send(fr, 2);
}

// ─── FreeRTOS Tasks ──────────────────────────────────────────────────────────

// High CAN RX Task: Listens for Jetson Host commands via MCP2515
[[noreturn]] static void task_can_rx_high(void*) {
    g_can_high.set_rx_task_handle(xTaskGetCurrentTaskHandle());
    Frame fr{};
    while (true) {
        if (g_can_high.receive(fr, 100)) {
            ingest_high(fr);
        }
    }
}

// Low CAN RX Task: Listens for physical actuator feedback via TWAI
[[noreturn]] static void task_can_rx_low(void*) {
    Frame fr{};
    while (true) {
        if (g_can_low.receive(fr, 100)) {
            ingest_low(fr);
        }
    }
}

// 100 Hz Control Task: Orchestrates Dual-Bus Control & Telemetry
[[noreturn]] static void task_control(void*) {
    constexpr TickType_t kPeriod = pdMS_TO_TICKS(1000 / dumb::kControlHz);
    vTaskDelay(pdMS_TO_TICKS(dumb::kBootHoldMs));
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0u;

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        g_can_low.service_recovery(now_us);
        if (g_can_high.bus_off()) {
            g_can_high.recover();
        }

        // ── Real Actuator & Host Liveness ────────────────────────────
        const bool real_mtr_active = (now_us - g_last_real_mtr_us.load(std::memory_order_relaxed)) < 250'000;
        const bool real_ses_active = (now_us - g_last_real_ses_us.load(std::memory_order_relaxed)) < 250'000;
        const bool real_seb_active = (now_us - g_last_real_seb_us.load(std::memory_order_relaxed)) < 250'000;
        const bool host_active     = (now_us - g_last_host_cmd_us.load(std::memory_order_relaxed)) < 500'000;

        // ── Command snapshot (auto-zero on host watchdog timeout) ────
        int32_t speed_in   = 0;
        int32_t yaw_in     = 0;
        uint8_t gear_in    = 0;
        int32_t brake_kpa  = 0;
        int16_t steer_ang  = 0;
        bool    steer_val  = false;
        uint8_t lights_in  = 0;

        if (host_active) {
            speed_in   = g_cmd_speed_mmps.load(std::memory_order_relaxed);
            yaw_in     = g_cmd_yaw_mrad_s.load(std::memory_order_relaxed);
            gear_in    = g_cmd_gear.load(std::memory_order_relaxed);
            brake_kpa  = g_cmd_brake_kpa.load(std::memory_order_relaxed);
            steer_ang  = g_cmd_steer_0_1deg.load(std::memory_order_relaxed);
            steer_val  = g_cmd_steer_valid.load(std::memory_order_relaxed);
            lights_in  = g_cmd_lights.load(std::memory_order_relaxed);
        } else {
            // Host stopped sending commands: purge latched values
            g_cmd_speed_mmps.store(0, std::memory_order_relaxed);
            g_cmd_yaw_mrad_s.store(0, std::memory_order_relaxed);
            g_cmd_brake_kpa.store(0, std::memory_order_relaxed);
            g_cmd_steer_0_1deg.store(0, std::memory_order_relaxed);
            g_cmd_steer_valid.store(false, std::memory_order_relaxed);
        }

        if (brake_kpa > 100) lights_in |= 0x04u; // Auto brake light

        // SES Auto-Recovery State Machine
        if (g_rearm_ses_req.exchange(false, std::memory_order_relaxed)) {
            g_rearm_ses_ticks = 20; // 200 ms disarm pulse on physical reconnect
        }
        if (real_ses_active && g_ses_mode_status.load(std::memory_order_relaxed) == 0 && g_rearm_ses_ticks == 0) {
            g_rearm_ses_ticks = 20; // 200 ms disarm pulse on mode desync
        }
        const bool ses_armed = host_active && (g_rearm_ses_ticks == 0);
        if (g_rearm_ses_ticks > 0) {
            --g_rearm_ses_ticks;
        }

        // ── Resolve Kinematics & Setpoints ───────────────────────────
        int32_t motor_speed_mmps;
        int16_t ses_angle_raw;
        int32_t yaw_out;

        if (!host_active) {
            motor_speed_mmps = 0;
            yaw_out = 0;
            ses_angle_raw = real_ses_active
                ? g_ses_actual_angle_raw.load(std::memory_order_relaxed)
                : dumb::kSbwAngleOffset;
        } else if (steer_val) {
            motor_speed_mmps = std::clamp(
                speed_in,
                -static_cast<int32_t>(shared::kMaxSpeedRevMmps),
                static_cast<int32_t>(shared::kMaxSpeedFwdMmps));
            int16_t ang = std::clamp(steer_ang, int16_t{-450}, int16_t{450});
            ses_angle_raw = std::clamp(
                static_cast<int16_t>(ang + dumb::kSbwAngleOffset),
                dumb::kMinSteerRaw,
                dumb::kMaxSteerRaw);
            yaw_out = yaw_in;
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
            yaw_out = yaw_in;
        }

        // Brake stroke mapping: raw = (mm + 30.0) / 0.05
        const float stroke_mm = std::clamp(
            (static_cast<float>(brake_kpa) / static_cast<float>(shared::kMaxBrakeKpa))
                * dumb::kMaxBrakeStrokeMm,
            0.0f, dumb::kMaxBrakeStrokeMm);
        const uint16_t seb_stroke_raw = static_cast<uint16_t>(
            std::clamp(std::round((stroke_mm + dumb::kBrakeStrokeBias) / dumb::kBrakeStrokeScaleF),
                       static_cast<float>(dumb::kBrakeStrokeZeroRaw),
                       static_cast<float>(dumb::kMaxBrakeStrokeRaw)));

        // Telemetry feedback values: use real sensors if present, else setpoints
        const int16_t telemetry_speed = real_mtr_active
            ? g_mtr_actual_speed_mmps.load(std::memory_order_relaxed)
            : static_cast<int16_t>(motor_speed_mmps);
        const uint16_t telemetry_steer_raw = real_ses_active
            ? g_ses_actual_angle_raw.load(std::memory_order_relaxed)
            : static_cast<uint16_t>(ses_angle_raw);

        // ═════════════════════════════════════════════════════════════
        //  A. LOW CAN EMISSIONS (Direct Actuator Control)
        // ═════════════════════════════════════════════════════════════
        // 100 Hz: RT_DRIVE_CMD
        emit_low_rt_drive(motor_speed_mmps, gear_in);

        // 50 Hz: Actuator commands (SES + SEB)
        // Silent stop: only emit active actuator commands when host is driving
        if (tick % 2u == 0u) {
            if (host_active || g_rearm_ses_ticks > 0) {
                emit_low_ses(ses_angle_raw, motor_speed_mmps, ses_armed);
            }
            if (host_active) {
                emit_low_seb(seb_stroke_raw, host_active);
            }
        }

        // 10 Hz: Actuator supervisor commands (SYS_MODE, SYS_PWR, SYS_SAFETY)
        if (tick % 10u == 0u) {
            emit_low_sys_mode();
            emit_low_sys_pwr();
            emit_low_sys_safety(lights_in);
        }

        // ═════════════════════════════════════════════════════════════
        //  B. HIGH CAN EMISSIONS (Jetson Autoware Gateway & Telemetry)
        // ═════════════════════════════════════════════════════════════
        // 100 Hz: RT_MOTION_RPT (satisfies Autoware 100ms watchdog)
        emit_high_rt_motion(telemetry_speed, yaw_out, gear_in);

        // 20 Hz: STEER_DIAG (Autoware steering status)
        if (tick % 5u == 0u) {
            emit_high_steer_diag(telemetry_steer_raw);
        }

        // 10 Hz: RT_STATE_RPT, SYS_SAFETY_STS, SYS_THROTTLE_STS, NODE_STATUS, SYS_HEARTBEAT
        if (tick % 10u == 0u) {
            emit_rt_state(gear_in == 3 /*R*/, host_active);
            emit_high_sys_safety(lights_in);
            emit_sys_throttle(telemetry_speed);
            emit_rt_node_status();
            emit_sys_node_status();
            emit_sys_heartbeat(); // 10 Hz (100 ms) per protocol contract!
        }

        // 2 Hz: RT Heartbeat (500 ms)
        if (tick % 50u == 0u) {
            emit_rt_heartbeat();
        }

        // 1 Hz: Diagnostics report
        if (tick % 100u == 0u) {
            emit_sys_diag(brake_kpa > 100);
            emit_rt_diag();
        }

        // ═════════════════════════════════════════════════════════════
        //  C. STATUS LED
        // ═════════════════════════════════════════════════════════════
        if (tick % 2u == 0u) {
            if (dumb::kHasWs2812Led) {
                shared::led::VisualPattern pat;
                if (g_can_high.bus_off() || g_can_low.bus_off()) {
                    pat.base = shared::led::DomainColor::Red;
                    pat.cadence = shared::led::BaseCadence::FastBlink;
                } else if (host_active) {
                    pat.base = shared::led::DomainColor::Green;
                    pat.cadence = shared::led::BaseCadence::Breathe;
                } else {
                    pat.base = shared::led::DomainColor::Blue;
                    pat.cadence = shared::led::BaseCadence::Breathe;
                }
                const uint32_t now_ms = static_cast<uint32_t>(now_us / 1000);
                g_status_led.set(shared::led::render(pat, now_ms, 64));
            } else {
                if (g_can_high.bus_off() || g_can_low.bus_off()) {
                    gpio_set_level(static_cast<gpio_num_t>(dumb::kStatusLedGpio), (tick / 10) % 2);
                } else if (host_active) {
                    gpio_set_level(static_cast<gpio_num_t>(dumb::kStatusLedGpio), 1);
                } else {
                    gpio_set_level(static_cast<gpio_num_t>(dumb::kStatusLedGpio), (tick / 50) % 2);
                }
            }
        }

        ++tick;
        vTaskDelayUntil(&last_wake, kPeriod);
    }
}

// ─── Entry Point ─────────────────────────────────────────────────────────────
extern "C" void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  dumb-esp Dual-Bus Autonomous Bridge starting");
    ESP_LOGI(TAG, "  High Bus: MCP2515 SPI (SCK=%d, MOSI=%d, MISO=%d, CS=%d, INT=%d @ %d bps)",
             dumb::kSpiSckGpio, dumb::kSpiMosiGpio, dumb::kSpiMisoGpio,
             dumb::kSpiCsGpio, dumb::kMcpIntGpio, dumb::kCanHighBitrateHz);
    ESP_LOGI(TAG, "  Low Bus:  TWAI (TX=%d, RX=%d @ %d bps)",
             dumb::kCanLowTxGpio, dumb::kCanLowRxGpio, dumb::kCanLowBitrateHz);
    ESP_LOGI(TAG, "===============================================");

    // Initialize Status LED
    if (dumb::kHasWs2812Led) {
        g_status_led.init(dumb::kStatusLedGpio);
        g_status_led.set(shared::led::scale_rgb(
            shared::led::palette_lookup(shared::led::DomainColor::White), 64));
    } else {
        gpio_set_direction(static_cast<gpio_num_t>(dumb::kStatusLedGpio), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(dumb::kStatusLedGpio), 1);
    }

    // Initialize Low CAN (TWAI)
    if (!g_can_low.init()) {
        ESP_LOGE(TAG, "TWAI Low CAN driver init failed!");
    } else {
        ESP_LOGI(TAG, "TWAI Low CAN driver ready (TX=%d, RX=%d @ %d bps, bench_loopback=1)",
                 dumb::kCanLowTxGpio, dumb::kCanLowRxGpio, dumb::kCanLowBitrateHz);
    }

    // Initialize High CAN (MCP2515 SPI)
    if (!g_can_high.init()) {
        ESP_LOGE(TAG, "MCP2515 High CAN driver init failed!");
    } else {
        ESP_LOGI(TAG, "MCP2515 High CAN driver ready");
    }

    // Launch FreeRTOS Tasks
    xTaskCreatePinnedToCore(task_can_rx_high, "can_rx_high", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(task_can_rx_low,  "can_rx_low",  4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(task_control,      "control",      6144, nullptr, 6, nullptr, 1);
}
