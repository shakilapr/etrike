// SYS ESP32-S3 — Safety, Motor Actuation & Body Control.
// Architecture: architecture.md §8.
// 15 FreeRTOS tasks, all wired to real implementation modules.
// Phases S1-S4: CAN RX, dispatch, motor, safety, mode, throttle, brake,
//               lights, indicator, power, can_tx, diag, heartbeat.

// Runtime System Mode Configuration
#include "system_mode.h"
#include "bypass_modes.h"

// Define runtime bypass flags
bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = false;
bool g_bypass_seb_sync = false;
bool g_bypass_mtr_absent = false;

#include <atomic>
#include <initializer_list>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "ESP-IDF 5.0 or later required"
#endif

#include "config.h"
#include "can_driver.h"
#include "safety_monitor.h"
#include "mode_manager.h"
#include "stream_validity.h"
#include "inhibit_state.h"
#include "physical_egas.h"

#include "brake_control.h"
#include "light_control.h"
#include "indicator_control.h"
// #include "wdt_toggle.h"


static const char* TAG = "sys";

// ── Per-task alive counters for multi-task watchdog ─────────────────
static std::atomic<uint32_t> g_alive_safety{0};
static std::atomic<uint32_t> g_alive_brake{0};
static std::atomic<uint32_t> g_alive_dispatch{0};
static std::atomic<uint32_t> g_alive_can_tx{0};
static std::atomic<uint32_t> g_alive_can_ctrl{0};
static std::atomic<uint32_t> g_alive_hb{0};
static std::atomic<uint32_t> g_alive_mode{0};
static std::atomic<uint32_t> g_alive_gear{0};
static std::atomic<uint8_t>  g_task_health_bits{0};
static std::atomic<uint32_t> g_can_rx_overflow{0};

static can::CanDriver g_can(can::CanDriver::Config{sys::kCanTxGpio,
                                                    sys::kCanRxGpio,
                                                    sys::kCanBitrateHz});

// ── CAN TX helper — checks return, retries critical frames ──────────
static uint32_t g_can_tx_fail_count = 0;
static uint32_t g_can_tx_ok_count = 0;
static bool g_can_tx_had_failure = false;  // tracks if we've seen a TX failure
static bool send_can(can::Frame& fr, const char* caller = "?") {
    if (!g_can.send(fr)) {
        // Retry once for safety-critical frames
        if (fr.id == can::kIdVcuSebReq || fr.id == can::kIdSafetyEstop ||
            fr.id == can::kIdSysSafetySts) {
            vTaskDelay(pdMS_TO_TICKS(1));
            if (!g_can.send(fr, 20)) {  // longer timeout on retry
                g_can_tx_fail_count++;
                ESP_LOGE(TAG, "CAN TX critical frame %03X failed after retry (%s)", fr.id, caller);
                return false;
            }
            g_can_tx_ok_count++;
            return true;
        }
        g_can_tx_fail_count++;
        if (!g_can_tx_had_failure) {
            const auto health = g_can.health_snapshot();
            ESP_LOGW(TAG,
                     "CAN TX unavailable (%s) — frame %03X dropped "
                     "state=%u recovering=%u",
                     caller, fr.id, static_cast<unsigned>(health.state),
                     health.recovery_in_progress ? 1U : 0U);
            g_can_tx_had_failure = true;
        }
        return false;
    }
    // Recovery: log when TX succeeds after prior failures
    if (g_can_tx_had_failure) {
        ESP_LOGI(TAG, "CAN TX recovered (%s) — fail=%lu ok=%lu", caller, g_can_tx_fail_count, g_can_tx_ok_count);
        g_can_tx_had_failure = false;
    }
    g_can_tx_ok_count++;
    return true;
}

static bool send_estop_frame(const char* caller) {
    can::Frame frame;
    can::gen::SafetyEstop message{};
    if (can::gen::encode_safety_estop(message, frame) != can::gen::CodecStatus::Ok)
        return false;
    sys::mark_estop_broadcast(static_cast<uint32_t>(xTaskGetTickCount()));
    return send_can(frame, caller);
}

// ── Application state ──────────────────────────────────────────────

static sys::SafetyMonitor  g_safety;
static sys::ModeManager    g_mode_mgr;

static sys::BrakeControl   g_brake;
static sys::LightControl   g_lights;
static sys::IndicatorControl g_indicator;
// static sys::WdtToggle      g_wdt;


// Shared state (written by dispatch, read by actuators).
// Uses memory_order_relaxed throughout: each variable has exactly one
// writer (dispatch task) and one reader (motor/safety task). No ordering
// needed between variables — each is independently self-consistent.
// seq_cst would add ~20ns per access with no safety benefit here.
static std::atomic<int32_t>  g_setpoint_speed_mmps{0};
static std::atomic<uint8_t>  g_setpoint_gear{0};
static std::atomic<int32_t>  g_brake_pressure_kpa{0};
static std::atomic<uint8_t>  g_light_bits{0};       // CAN 0x302 input from Host
static std::atomic<uint8_t>  g_light_state{0};     // Actual SYS light output (packed for 0x011 byte 2)
static std::atomic<uint8_t>  g_rt_safety_state{0}; // RT safety_state from 0x210 (0=Normal, 1=InternalEstop, 2=Fault)
// Physical wheel speed from 0x122 RT_WHEEL_SPEED_STS (issue #3). Only populated
// when RT is configured with a wheel encoder; otherwise untouched (no physical
// EGAS on the current vehicle).
static std::atomic<int16_t>  g_wheel_measured_mmps{0};
static std::atomic<uint8_t>  g_wheel_sensor_state{0};  // 0 NI,1 ACQ,2 VALID,3 FAULT
static std::atomic<uint32_t> g_last_wheel_speed_tick{0};
static std::atomic<uint8_t>  g_mtr_gear_state{0};     // gear state from 0x206 MTR_MOTOR_FBK (C6b)

// 0x204 staleness tracking (arch §8.6: 200ms timeout → zero speed + neutral)
static std::atomic<uint32_t> g_last_setpoint_tick{0};
static std::atomic<uint32_t> g_last_brake_setpoint_tick{0};

// ── HMI request (0x111/0x112) stream validity + resolved power ──────
// SYS is the sole authority: it validates the Host-produced request streams
// (rolling counter + freshness) before letting them change resolved state.
// Invalid/stale requests do NOT change resolved mode/power; SYS falls back to
// its remaining valid inputs (physical buttons, safety state).
static etrike::protocol::StreamValidity g_mode_req_val;
static etrike::protocol::StreamValidity g_pwr_req_val;
static etrike::protocol::StreamValidity g_reset_req_val;
static constexpr uint32_t kReqFreshTicks =
    can::gen::HmiModeReq::kCycleMs * 5;  // request cycle 1000ms -> 5s timeout
static constexpr uint32_t kResetReqFreshTicks = 5000; // 5s timeout for sporadic reset requests
static std::atomic<bool> g_hmi_pwr_on{false};        // last VALID power request
static std::atomic<bool> g_mode_request_valid{false};
static std::atomic<bool> g_power_request_valid{false};


// Gap #14: Rate-limit 0x001 ESTOP broadcasts. Prevents flooding.
static std::atomic<int64_t>  g_last_estop_sent_us{0};

static bool can_send_estop() {
    return shared::should_send_estop_now(g_last_estop_sent_us, esp_timer_get_time());
}

// ── Motor feedback from 0x206 MTR_MOTOR_FBK ─────────────────────────
// MTR 0x206 carries the APPLIED speed COMMAND (the setpoint echoed back) —
// NOT a physical measurement (no wheel/motor encoder fitted). Renamed to
// motor_command_speed_mmps to prevent downstream code from treating it as
// closed-loop speed feedback (issue #1).
static std::atomic<int16_t>  g_motor_command_speed_mmps{0};
static std::atomic<uint8_t>  g_motor_fault_flags{0};

// ── SEB status from 0x721 SEB_STATUS ────────────────────────────────
// Actual stroke in raw units (600 = 0mm, scale 0.05, offset -30)
static std::atomic<uint16_t> g_seb_actual_stroke_raw{600};
// Timestamp of last 0x721 arrival (for staleness check §8.10)
static std::atomic<uint32_t> g_last_seb_status_tick{0};
// SEB rolling counter incrementing (H1: true = SEB is acknowledging commands)
static std::atomic<bool>     g_seb_rolling{true};
static std::atomic<uint32_t> g_last_seb_roll_change_tick{0};

// ── Brake commanded stroke (set by brake task after build_command) ──
static std::atomic<uint16_t> g_cmd_stroke_raw{600};             // 600 = 0mm

// ── SEB version first-receipt guard (0x741) ─────────────────────────
static std::atomic<bool>     g_seb_version_logged{false};

// ── ESTOP trigger timestamp & MTR ACK state machine (Gap #15 / BUG-03) 
#include "mtr_estop_ack.h"
static std::atomic<uint32_t> g_last_estop_trigger_tick{0};
static sys::MtrEstopAckWatchdog g_mtr_ack_watchdog;

static inline void trigger_estop_ack_watchdog(uint32_t now) {
    g_last_estop_trigger_tick.store(now, std::memory_order_relaxed);
    g_mtr_ack_watchdog.trigger(now, g_motor_fault_flags.load(std::memory_order_relaxed));
}

// ── 0x206 staleness tracking (Gap #15) ───────────────────────────────
static std::atomic<uint32_t> g_last_mtr_fbk_tick{0};

// ── SEB fault state for 0x600 diag (Gap #13) ─────────────────────────
// g_seb_error_status / g_seb_status_byte0 are defined in inhibit_state.cpp.
static std::atomic<bool>     g_brake_fault_active{false};

// ── Independent per-owner traction-inhibit masks (issues #5/#7) ─────
// See inhibit_state.h. g_inhibit_reasons / g_latched_fault_reasons are defined
// in inhibit_state.cpp; latched faults are cleared only by the validated reset
// transaction (ModeManager::try_exit_estop).

// ── System ESTOP latch predicate (safety invariant, issue #4) ────────
// The persistent ESTOP state SYS publishes (0x011.estop_active and
// 0x7FE.estop_active) must reflect the *system* latch, not merely the
// hardware ESTOP button. A software ESTOP (CAN 0x001, SEB L3, EGAS,
// bus-off, MTR-reported-ESTOP) latches ModeManager into ESTOP; while that
// latch is held, estop_active MUST be 1 so downstream (RT/MTR) cannot
// two-frame-clear into a false all-clear. It only drops to 0 once SYS has
// been explicitly reset out of ESTOP via the physical reset path.
static bool sys_estop_latched() {
    return sys::ModeManager::estop_latched(g_mode_mgr.mode(), g_safety.estop_active());
}

// Queues
static QueueHandle_t g_can_rx_queue   = nullptr;  // 16 deep, can::Frame

// ── CAN RX task (prio 5) ───────────────────────────────────────────

[[noreturn]] static void task_can_rx(void*) {
    can::Frame fr;
    while (1) {
        if (g_can.receive(fr, 100)) {
            // Use pdMS_TO_TICKS(5) timeout instead of 0 to prevent priority
            // inversion (bug 6.4). When queue is full (16 frames), RX task at
            // prio 5 must yield briefly so dispatch at prio 4 can drain it.
            // A 0 timeout causes continuous frame drops under CAN bursts.
            if (xQueueSend(g_can_rx_queue, &fr, pdMS_TO_TICKS(5)) != pdTRUE) {
                g_can_rx_overflow.fetch_add(1, std::memory_order_relaxed);
                static bool warned = false;
                if (!warned) { ESP_LOGW(TAG, "CAN RX queue overflow — frames dropped"); warned = true; }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));  // yield when silent or uninitialized to prevent CPU starvation
        }
    }
}

// ── CAN recovery supervisor (prio 2) ───────────────────────────────
// State transitions are latched by the TWAI ISR callback. All control API
// calls remain here in task context.
[[noreturn]] static void task_can_control(void*) {
    while (1) {
        g_alive_can_ctrl.store(xTaskGetTickCount(), std::memory_order_relaxed);
        g_can.service_recovery(esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── Dispatch task (prio 4) ────────────────────────────────────────

[[noreturn]] static void task_dispatch(void*) {
    can::Frame fr;
    while (1) {
        g_alive_dispatch.store(xTaskGetTickCount(), std::memory_order_relaxed);
        if (xQueueReceive(g_can_rx_queue, &fr, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        // Manual dispatch into atomic state (struct-based dispatch_frame not used
        // because some targets are std::atomic<T> rather than plain T*)
        switch (fr.id) {
        case can::kIdRtDriveCmd: {   // 0x204
            can::gen::RtDriveCmd sp{};
            if (can::gen::decode_rt_drive_cmd(fr.view(), sp) != can::gen::CodecStatus::Ok) break;
            g_setpoint_speed_mmps.store(sp.motor_speed_mmps, std::memory_order_relaxed);
            g_setpoint_gear.store(sp.gear, std::memory_order_relaxed);
            g_last_setpoint_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
            break;
        }
        case can::kIdRtBrakeCmd: {   // 0x205
            can::gen::RtBrakeCmd brk{};
            if (can::gen::decode_rt_brake_cmd(fr.view(), brk) != can::gen::CodecStatus::Ok) break;
            g_brake_pressure_kpa.store(brk.brake_pressure_kpa, std::memory_order_relaxed);
            g_last_brake_setpoint_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
            break;
        }
        case can::kIdHmiModeReq: {   // 0x111 — Host mode request (1Hz, High→Low via RT)
            static bool inited = false;
            if (!inited) {
                g_mode_req_val.set_key(1 /*low*/, can::kIdHmiModeReq, kReqFreshTicks);
                g_pwr_req_val.set_key(1 /*low*/, can::kIdHmiPwrReq, kReqFreshTicks);
                inited = true;
            }
            can::gen::HmiModeReq request{};
            if (can::gen::decode_hmi_mode_req(fr.view(), request) != can::gen::CodecStatus::Ok) break;
            // Validate the request stream (rolling counter + freshness) before use.
            const bool ok = g_mode_req_val.observe(
                static_cast<std::uint8_t>(request.rolling_counter), xTaskGetTickCount());
            g_mode_request_valid.store(ok, std::memory_order_relaxed);
            if (!ok) break;  // stale/replayed request: do not change resolved mode
            if (g_mode_mgr.parse_hmi_mode(request.req_mode)) {
                // If mode actually changed due to this request, log it.
                // The main 10Hz control loop will naturally pick up the new mode
                // and broadcast 0x110 SYS_MODE_CMD on its next tick.
                ESP_LOGI(TAG, "HMI changed mode to %s", g_mode_mgr.name());
            }
            break;
        }
        case can::kIdHmiPwrReq: {    // 0x112 — Host power request (1Hz, High→Low via RT)
            can::gen::HmiPwrReq request{};
            if (can::gen::decode_hmi_pwr_req(fr.view(), request) != can::gen::CodecStatus::Ok) break;
            // Validate the request stream (rolling counter + freshness) before use.
            const bool ok = g_pwr_req_val.observe(
                static_cast<std::uint8_t>(request.rolling_counter), xTaskGetTickCount());
            g_power_request_valid.store(ok, std::memory_order_relaxed);
            if (!ok) break;  // stale/replayed request: do not change resolved power
            g_hmi_pwr_on.store(request.req_start != 0, std::memory_order_relaxed);
            break;
        }
        case can::kIdHostEstopResetReq: {  // 0x114 — Host ESTOP reset request (High→Low via RT)
            static bool inited = false;
            static uint8_t rsp_roll = 0;
            if (!inited) {
                g_reset_req_val.set_key(1 /*low*/, can::kIdHostEstopResetReq, kResetReqFreshTicks);
                inited = true;
            }
            can::gen::HostEstopResetReq req{};
            if (can::gen::decode_host_estop_reset_req(fr.view(), req) != can::gen::CodecStatus::Ok) break;

            // Supervise freshness and rolling sequence
            const bool fresh = g_reset_req_val.observe(
                static_cast<std::uint8_t>(req.rolling_counter), xTaskGetTickCount());

            uint16_t blockers = sys::get_estop_reset_blockers(
                /*physical_estop=*/g_safety.estop_active(),
                /*hb_ok=*/g_safety.heartbeat_ok(),
                /*measured_speed_mmps=*/g_wheel_measured_mmps.load(std::memory_order_relaxed),
                /*mtr_fault_flags=*/g_motor_fault_flags.load(std::memory_order_relaxed),
                /*token=*/static_cast<uint16_t>(req.reset_token)
            );

            if (!fresh) {
                blockers |= sys::kResetBlockInvalidToken;
            }

            bool reset_ok = false;
            if (blockers == 0) {
                reset_ok = g_mode_mgr.try_exit_estop_remote(blockers);
                if (reset_ok) {
                    sys::mark_estop_reset(static_cast<uint32_t>(xTaskGetTickCount()));
                    ESP_LOGI(TAG, "ESTOP reset via Host request seq=%u accepted", req.request_seq);
                }
            }

            // Transmit 0x115 SYS_ESTOP_RESET_RSP on low bus (RT forwards to High)
            can::gen::SysEstopResetRsp rsp{};
            rsp.request_seq = req.request_seq;
            rsp.result = reset_ok ? 0u : 1u;
            rsp.blocker_mask = blockers;
            rsp.rolling_counter = rsp_roll++;

            can::Frame rsp_frame;
            if (can::gen::encode_sys_estop_reset_rsp(rsp, rsp_frame) == can::gen::CodecStatus::Ok) {
                send_can(rsp_frame, "reset-rsp");
            }
            if (!reset_ok) {
                ESP_LOGW(TAG, "ESTOP reset request seq=%u rejected blockers=0x%04x",
                         req.request_seq, blockers);
            }
            break;
        }
        case can::kIdMtrMotorFbk: {  // 0x206 — applied-speed-command echo (issue #1: NOT physical speed)
            can::gen::MtrMotorFbk fbk{};

            if (can::gen::decode_mtr_motor_fbk(fr.view(), fbk) != can::gen::CodecStatus::Ok) break;
            g_motor_command_speed_mmps.store(fbk.motor_command_speed_mmps, std::memory_order_relaxed);
            g_motor_fault_flags.store(fbk.fault_flags, std::memory_order_relaxed);
            g_mtr_ack_watchdog.on_feedback_received(fbk.fault_flags);
            g_mtr_gear_state.store(fbk.gear_state, std::memory_order_relaxed);  // C6b
            g_last_mtr_fbk_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);

            // Gap #15: Check if MTR has triggered local ESTOP (ESTOP_ACTIVE bit).
            // MTR sets this bit when its ESTOP GPIO or CAN 0x001 is detected.
            // If SYS missed the ESTOP frame, this provides a redundant path.
            if ((fbk.fault_flags & shared::kMtrFaultEstopActive)
                && g_mode_mgr.mode() != can::Mode::Estop) {
                ESP_LOGW(TAG, "MTR reports ESTOP_ACTIVE in 0x206 fault_flags — propagating");
                g_mode_mgr.force_estop();
                if (can_send_estop()) {
                    send_estop_frame("ESTOP");
                }
            }
            break;
        }
        case can::kIdHostLightCmd: { // 0x302
            can::gen::HostLightCmd lights{};
            if (can::gen::decode_host_light_cmd(fr.view(), lights) != can::gen::CodecStatus::Ok) break;
            uint8_t bits = (lights.left_turn ? 1u : 0u)
                         | (lights.right_turn ? 2u : 0u)
                         | (lights.brake_light ? 4u : 0u)
                         | (lights.headlight ? 8u : 0u);
            g_light_bits.store(bits, std::memory_order_relaxed);
            break;
        }
        case can::kIdSafetyEstop: {  // 0x001 — rate-limited RX + loopback/reset-grace (Gap #14)
            // Rate-limit only downstream actions (logging, CAN forwarding); the
            // safety override itself is always evaluated via rx_estop_suppressed.
            static int        estop_rx_count = 0;
            static TickType_t estop_rx_window_start = 0;
            TickType_t now = xTaskGetTickCount();
            bool within_limit = true;
            if (estop_rx_window_start == 0
                || (now - estop_rx_window_start) >= pdMS_TO_TICKS(sys::kEstopRateLimitWindowMs)) {
                estop_rx_window_start = now;
                estop_rx_count = 1;
            } else {
                ++estop_rx_count;
                within_limit = (estop_rx_count <= sys::kEstopRateLimitMax);
            }
            // Route through the real RX policy (shared with the native-test
            // harness). Suppress only our own 0x001 reflection or a stray
            // in-flight 0x001 during the operator reset grace window so the
            // system cannot livelock re-latching ESTOP. A genuine external
            // 0x001 outside these windows still latches.
            if (sys::rx_estop_suppressed(static_cast<uint32_t>(now))) {
                if (within_limit) {
                    ESP_LOGW(TAG, "ESTOP via CAN 0x001 (suppressed: loopback/reset-grace)");
                }
                break;
            }
            g_mode_mgr.force_estop();
            trigger_estop_ack_watchdog(static_cast<uint32_t>(now));
            if (within_limit) {
                ESP_LOGW(TAG, "ESTOP via CAN 0x001");
            }
            break;
        }
        case can::kIdSebStatus: {  // 0x721
            can::custom::seb::Status value{};
            if (can::custom::seb::decode_status(fr.view(), value) != can::gen::CodecStatus::Ok) break;
            // Store byte 0 atomically (alignment, error_status, control_mode) —
            // eliminates data race with brake task reading raw byte array (H3).
            g_seb_status_byte0.store(value.status_byte, std::memory_order_relaxed);
            // F7: Extract SEB error_status from byte 0 bits 6-7 (architecture §8.10)
            {
                uint8_t es = value.error_status;
                g_seb_error_status.store(es, std::memory_order_relaxed);
                if (es >= 3) {
                    // Issue #5: a confirmed Level-3 brake fault is a latched safety
                    // fault (aligned with the 0x731 L3 path below) — full ESTOP.
                    // The latched reason survives until the explicit reset path
                    // confirms the underlying L3 has cleared.
                    ESP_LOGE(TAG, "SEB error_status L3 in 0x721 (status=0x%02x)", value.status_byte);
                    sys::set_latched_fault(sys::kLatchedSebL3);
                    if (g_mode_mgr.mode() != can::Mode::Estop) {
                        g_mode_mgr.force_estop();
                        trigger_estop_ack_watchdog(xTaskGetTickCount());
                        if (can_send_estop()) {
                            send_estop_frame("ESTOP");
                        }
                    }
                }
            }
            // Extract actual stroke (LE u16 at bytes 2-3, scale 0.05, offset -30).
            // In Pressure mode (ctrl_mode=1), byte 3 is overwritten with pressure
            // data — using it as Stroke[15:8] produces corrupted astronomical values.
            // Use last valid stroke when in Pressure mode (bug 6.2).
            {
                uint8_t seb_ctrl = value.control_mode;
                uint16_t actual_raw;
                if (seb_ctrl == 0) {
                    actual_raw = value.stroke_value_raw;
                } else {
                    actual_raw = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
                }
                g_seb_actual_stroke_raw.store(actual_raw, std::memory_order_relaxed);
            }
            g_seb_seen.store(true, std::memory_order_release);
            g_last_seb_status_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);

            // H1: Track SEB rolling counter — if RT's 0x7B9 is failing, SEB stops
            // acknowledging. SYS detects stale status and resumes sending 0x7B9.
            static uint8_t  last_seb_roll = 0xFF;
            static bool     seb_roll_init = false;
            uint8_t seb_roll = value.rolling_counter;
            if (!seb_roll_init || seb_roll != last_seb_roll) {
                seb_roll_init = true;
                last_seb_roll = seb_roll;
                g_last_seb_roll_change_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
                g_seb_rolling.store(true, std::memory_order_relaxed);  // SEB is acknowledging
            }
            // Brake following error monitor (§8.10, issue #5): cmp cmd vs actual stroke.
            // Only in Stroke mode — in Pressure mode cmd_stroke is fixed at 600
            // (0mm baseline) while the SEB physically moves to build pressure,
            // which would false-trigger the following error (bug 6.1).
            //
            // Two severities:
            //   - transient excursion      -> kInhibitBrakeFollowing (recoverable,
            //                                cleared after N healthy observations)
            //   - confirmed persistent     -> kLatchedBrakeFollowing + force_estop()
            //                                (latched safety fault; only the reset
            //                                path clears it, once the cause is gone)
            {
                uint8_t seb_ctrl = value.control_mode;
                if (seb_ctrl == 0) {  // Stroke mode only
                    uint16_t cmd = g_cmd_stroke_raw.load(std::memory_order_relaxed);
                    uint16_t actual_raw = value.stroke_value_raw;
                    uint16_t diff = (cmd > actual_raw) ? (cmd - actual_raw) : (actual_raw - cmd);

                    // Track the persistence of the current excursion.
                    static bool      follow_active = false;
                    static TickType_t follow_start = 0;
                    static int       follow_recover_count = 0;
                    const bool in_excursion = (diff > sys::kBrakeFollowingErrRaw);
                    if (in_excursion) {
                        if (!follow_active) {
                            follow_active = true;
                            follow_start = xTaskGetTickCount();
                            follow_recover_count = 0;  // fresh excursion: recovery restarts
                            // Excursion present: transient inhibit is active now.
                            sys::set_inhibit(sys::kInhibitBrakeFollowing);
                        } else if ((xTaskGetTickCount() - follow_start)
                                   >= pdMS_TO_TICKS(sys::kBrakeFollowingLatchedMs)) {
                            // Confirmed persistent following error -> latched fault.
                            if (!(sys::g_latched_fault_reasons.load()
                                  & sys::kLatchedBrakeFollowing)) {
                                ESP_LOGE(TAG, "Brake following err latched: cmd=%u actual=%u "
                                              "diff=%u raw (~%d mm)",
                                         cmd, actual_raw, diff, int(diff * 0.05f));
                                sys::set_latched_fault(sys::kLatchedBrakeFollowing);
                                if (g_mode_mgr.mode() != can::Mode::Estop) {
                                    g_mode_mgr.force_estop();
                                    trigger_estop_ack_watchdog(xTaskGetTickCount());
                                    if (can_send_estop()) {
                                        send_estop_frame("ESTOP");
                                    }
                                }
                            }
                        }
                    } else {
                        follow_active = false;
                        // Hysteresis recovery: clear the transient inhibit only after
                        // several consecutive healthy observations. The latched fault
                        // (if set) is NOT cleared here — only the reset path owns it.
                        if (sys::g_inhibit_reasons.load() & sys::kInhibitBrakeFollowing) {
                            if (++follow_recover_count >= sys::kBrakeFollowingRecoverFrames) {
                                sys::clear_inhibit(sys::kInhibitBrakeFollowing);
                                follow_recover_count = 0;
                            }
                        }
                    }
                }
            }
            break;
        }
        case can::kIdSebTest: {     // 0x6FB — SEB_Test telemetry (arch §8.3)
            // Motor current: Byte1-2 i16 LE, scale 0.0078125 A/bit
            // ECU temp: Byte3-4 u16 LE, scale 0.5 C/bit, offset -40
            can::custom::seb::TestTelemetry telemetry{};
            if (can::custom::seb::decode_test(fr.view(), telemetry) != can::gen::CodecStatus::Ok) break;
            uint16_t ecu_raw = telemetry.ecu_temperature_raw;
            int16_t  ecu_temp = int16_t(ecu_raw * 0.5f - 40.0f);
            if (ecu_temp > 80) {
                ESP_LOGW(TAG, "SEB_Test: ECU temp %d C exceeds 80 C threshold", ecu_temp);
            }
            break;
        }
        case can::kIdSebErrInfo: {  // 0x731 — SEB_ErrInfo (arch §8.3)
            can::custom::seb::ErrorInfo error_info{};
            if (can::custom::seb::decode_error_info(fr.view(), error_info) != can::gen::CodecStatus::Ok) break;
            // Check all 16 L3 fault bits per can-dictionary. Any L3 → force_estop.
            // L3 bit positions: 2,3,4,5,6,7,8,9,10,11,13,17,18,20,21,22
            static const int kL3Bits[] = {2,3,4,5,6,7,8,9,10,11,13,17,18,20,21,22};
            static const char* kL3Names[] = {
                "CanCom","ECUTemp","DomainDriveSC","DomainDriveV",
                "DomainDriveT","AngleSensorP_OOC","AngleSensorP_AF","AngleSensorS_OOC",
                "AngleSensorS_AF","NoPreSensor","SensorUCL","MtrStall",
                "MtrD_C","InitOil","SentValue","NoLoad"
            };
            bool l3_found = false;
            for (int i = 0; i < 16; ++i) {
                int byte_idx = kL3Bits[i] / 8;
                if (error_info.raw[byte_idx] & (1 << (kL3Bits[i] % 8))) {
                    ESP_LOGE(TAG, "SEB L3 fault: bit %d = %s", kL3Bits[i], kL3Names[i]);
                    l3_found = true;
                }
            }
            if (l3_found) {
                g_mode_mgr.force_estop();
                // Record ESTOP trigger tick for MTR ACK timeout check (bug 6.5).
                // Without this, the MTR ESTOP ACK safety check in task_safety
                // is permanently bypassed for SEB-triggered ESTOPs.
                trigger_estop_ack_watchdog(xTaskGetTickCount());
                if (can_send_estop()) {
                    send_estop_frame("ESTOP");
                }
                ESP_LOGW(TAG, "ESTOP triggered by SEB 0x731 L3 fault(s)");
            }
            break;
        }
        case can::kIdSebVersion: {  // 0x741 — SEB_Version (arch §8.3)
            if (!g_seb_version_logged.load(std::memory_order_relaxed)) {
                can::custom::seb::Version version{};
                if (can::custom::seb::decode_version(fr.view(), version) != can::gen::CodecStatus::Ok) break;
                ESP_LOGI(TAG, "SEB_Version: SW=%.2f HW=%.1f",
                         version.software_raw * 0.01f, version.hardware_raw * 0.1f);
                g_seb_version_logged.store(true, std::memory_order_relaxed);
            }
            break;
        }
        case can::kIdRtStateRpt: {  // 0x210 — RT safety state for takeover detection
            can::gen::RtStateRpt state{};
            if (can::gen::decode_rt_state_rpt(fr.view(), state) == can::gen::CodecStatus::Ok)
                g_rt_safety_state.store(state.safety_state, std::memory_order_relaxed);
            break;
        }
        case can::kIdRtWheelSpeedSts: {  // 0x122 — physical wheel speed (issue #3)
            can::gen::RtWheelSpeedSts ws{};
            if (can::gen::decode_rt_wheel_speed_sts(fr.view(), ws) == can::gen::CodecStatus::Ok) {
                g_wheel_measured_mmps.store(ws.measured_speed_mmps, std::memory_order_relaxed);
                g_wheel_sensor_state.store(ws.sensor_state, std::memory_order_relaxed);
                g_last_wheel_speed_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
            }
            break;
        }
        case can::kIdRtHeartbeatLow: {  // 0x7FD
            can::gen::RtHeartbeat heartbeat{};
            if (can::gen::decode_rt_heartbeat(fr.view(), heartbeat) == can::gen::CodecStatus::Ok)
                g_safety.feed_heartbeat_rt(heartbeat.alive_ctr);
            break;
        }
        }
    }
}

// ── Safety task (prio 5, 20 Hz) ────────────────────────────────────

[[noreturn]] static void task_safety(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kSafetyCheckHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        // Read hardware ESTOP button (NC fail-safe: HIGH = pressed / open circuit)
#ifdef TESTING
        bool estop_hw = false;
        bool brake_lever = false;
#else
        bool estop_hw = (gpio_get_level(static_cast<gpio_num_t>(sys::kEstopGpio)) == 1);
        bool brake_lever = (gpio_get_level(static_cast<gpio_num_t>(sys::kBrakeLeverGpio)) == 0);
#endif

        g_safety.set_estop(estop_hw);
        g_safety.set_brake_lever(brake_lever);

        // Developer bypass suppresses only missing-dependency faults. The
        // physical ESTOP remains unbypassable.
        bool estop_triggered = g_safety.estop_active()
            || (!g_bench_solo_mode && !g_safety.heartbeat_ok());
        if (estop_triggered) {
            if (g_mode_mgr.mode() != can::Mode::Estop) {
                g_mode_mgr.force_estop();
                trigger_estop_ack_watchdog(xTaskGetTickCount());
                // Broadcast CAN 0x001 ESTOP on low bus (architecture §8.4)
                // Gap #14: rate-limited to prevent bus flooding
                if (can_send_estop()) {
                    send_estop_frame("ESTOP");
                    ESP_LOGW(TAG, "ESTOP triggered — sent CAN 0x001");
                }
            }
        }

        // Toggle external watchdog + per-task alive counter
        g_alive_safety.store(xTaskGetTickCount(), std::memory_order_relaxed);
        // g_wdt.tick();  // GPIO23 toggle

        // Command-path / setpoint-echo consistency check (issue #1). 0x206
        // reports the APPLIED speed COMMAND, not physical speed (no encoder).
        // This detects RT-vs-MTR command-path disagreement only — it is NOT a
        // physical EGAS L2 check and cannot see DAC/relay/motor faults. Only in
        // AUTO mode. Mismatch > threshold for > duration → ESTOP.
        if (!g_bypass_mtr_absent) {
            static bool  egas_fault_active = false;
            static TickType_t egas_fault_start = 0;
            if (g_mode_mgr.mode() == can::Mode::Auto) {
                int32_t cmd     = g_setpoint_speed_mmps.load(std::memory_order_relaxed);
                int16_t applied = g_motor_command_speed_mmps.load(std::memory_order_relaxed);
                int32_t diff    = (cmd > applied) ? (cmd - applied) : (applied - cmd);
                if (diff > sys::kEgasSpeedThresholdMmps) {
                    if (!egas_fault_active) {
                        egas_fault_active = true;
                        egas_fault_start = xTaskGetTickCount();
                    } else if ((xTaskGetTickCount() - egas_fault_start)
                                >= pdMS_TO_TICKS(sys::kEgasFaultDurationMs)) {
                        if (g_mode_mgr.mode() != can::Mode::Estop) {
                            g_mode_mgr.force_estop();
                            trigger_estop_ack_watchdog(xTaskGetTickCount());
                            if (can_send_estop()) {
                                send_estop_frame("ESTOP");
                            }
                            ESP_LOGW(TAG, "Cmd-path mismatch: |0x204 %.0f - applied %.0f| > %d mm/s — ESTOP",
                                     (double)cmd, (double)applied, sys::kEgasSpeedThresholdMmps);
                        }
                    }
                } else {
                    egas_fault_active = false;
                }
            } else {
                egas_fault_active = false;
            }
        }
        // F3: MTR ESTOP ACK check (Gap #15 / BUG-03)
        // After ESTOP triggered, verify MTR sets ESTOP_ACTIVE bit in 0x206 fault_flags with bounded retries.
        if (!g_bypass_mtr_absent) {
            uint8_t flags = g_motor_fault_flags.load(std::memory_order_relaxed);
            auto action = g_mtr_ack_watchdog.check_tick(xTaskGetTickCount(), flags);
            if (action == sys::MtrEstopAckWatchdog::Action::Confirmed) {
                ESP_LOGI(TAG, "MTR ESTOP ACK confirmed by motor controller");
            } else if (action == sys::MtrEstopAckWatchdog::Action::Retry) {
                ESP_LOGW(TAG, "MTR ESTOP ACK timeout — retriggering ESTOP (retries left: %u)",
                         g_mtr_ack_watchdog.retries_left());
                g_mode_mgr.force_estop();
                if (can_send_estop()) {
                    send_estop_frame("ESTOP");
                }
            } else if (action == sys::MtrEstopAckWatchdog::Action::ExhaustedFault) {
                ESP_LOGE(TAG, "MTR ESTOP ACK failed after retries — latched brake fault");
                g_brake_fault_active.store(true, std::memory_order_relaxed);
            }
        }

        // F4: 0x206 staleness check (Gap #15)
        // Warn if no MTR feedback for >200ms (MTR comms lost).
        // Startup grace: skip if never received (g_last_mtr_fbk_tick == 0).
        // Issue #7: MTR feedback loss is a B-class traction inhibit owned by
        // kInhibitMtrFbkLoss (NOT the brake-fault state). It forces power
        // authority OFF in task_mode until MTR feedback is confirmed healthy.
        if (!g_bypass_mtr_absent) {
            uint32_t last_fbk = g_last_mtr_fbk_tick.load(std::memory_order_relaxed);
            bool stale = last_fbk > 0
                && (xTaskGetTickCount() - last_fbk) >= pdMS_TO_TICKS(sys::kMtrFbkStaleMs);
            if (stale) {
                sys::set_inhibit(sys::kInhibitMtrFbkLoss);
                g_setpoint_speed_mmps.store(0, std::memory_order_relaxed);
                g_setpoint_gear.store(0, std::memory_order_relaxed);
                static TickType_t last_warn = 0;
                if (last_warn == 0 || (xTaskGetTickCount() - last_warn) >= pdMS_TO_TICKS(1000)) {
                    ESP_LOGE(TAG, "0x206 MTR_MOTOR_FBK stale — removing power authority (inhibit)");
                    last_warn = xTaskGetTickCount();
                }
            }
            // Confirmed recovery: kMtrFbkRecoverFrames consecutive fresh 0x206
            // observations at this 20 Hz cadence release the inhibit bit. This
            // is not a single-frame recovery.
            static int mtr_fbk_recover_count = 0;
            if (sys::g_inhibit_reasons.load() & sys::kInhibitMtrFbkLoss) {
                if (!stale) {
                    if (++mtr_fbk_recover_count >= sys::kMtrFbkRecoverFrames) {
                        sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
                        mtr_fbk_recover_count = 0;
                        ESP_LOGI(TAG, "MTR feedback recovered — inhibit cleared");
                    }
                } else {
                    // Still stale: any partial recovery progress is reset so a
                    // release requires a full fresh run of N observations.
                    mtr_fbk_recover_count = 0;
                }
            }
        }

        // Legacy brake-fault auto-recovery (SEB L3 / following-error paths; these
        // are re-classified into the two-mask model by issue #5). MTR-feedback
        // loss no longer sets g_brake_fault_active, so MTR freshness is removed
        // from this recovery gate.
        if (g_brake_fault_active.load(std::memory_order_relaxed)) {
            bool seb_healthy = g_seb_error_status.load(std::memory_order_relaxed) < 3;
            bool not_in_estop = (g_mode_mgr.mode() != can::Mode::Estop);

            static int brake_recovery_count = 0;
            if (seb_healthy && not_in_estop) {
                if (++brake_recovery_count >= 30) {  // 30 * 100ms = 3s
                    g_brake_fault_active.store(false, std::memory_order_relaxed);
                    brake_recovery_count = 0;
                    ESP_LOGI(TAG, "Brake fault cleared — all conditions recovered");
                }
            } else {
                brake_recovery_count = 0;
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Mode task (prio 4, 10 Hz) ──────────────────────────────────────

[[noreturn]] static void task_mode(void*) {
    TickType_t period = pdMS_TO_TICKS(100);  // 10 Hz
    TickType_t last   = xTaskGetTickCount();
    static uint8_t roll_mode_ = 0;
    static uint8_t roll_pwr_ = 0;
    static bool     mode_was_estop = false;
    while (1) {
        g_alive_mode.store(xTaskGetTickCount(), std::memory_order_relaxed);
#ifdef TESTING
        bool mode_btn  = false;
        bool start_btn = false;
#else
        bool mode_btn  = (gpio_get_level(static_cast<gpio_num_t>(sys::kModeBtnGpio)) == 0);
        bool start_btn = (gpio_get_level(static_cast<gpio_num_t>(sys::kStartBtnGpio)) == 0);
#endif

        bool changed = g_mode_mgr.tick(mode_btn, start_btn);
        // Issue #4 / gap #14: an operator reset (START button / MODE long-press)
        // carries the system out of ESTOP. Open the reset-grace window so a
        // 0x001 still in flight on the bus cannot instantly re-latch ESTOP
        // (livelock). The harness shares this exact policy via sys::rx_estop_suppressed.
        if (mode_was_estop && g_mode_mgr.mode() != can::Mode::Estop) {
            sys::mark_estop_reset(static_cast<uint32_t>(xTaskGetTickCount()));
        }
        mode_was_estop = (g_mode_mgr.mode() == can::Mode::Estop);
        if (changed) {
            ESP_LOGI(TAG, "Mode changed to %s", g_mode_mgr.name());
        }

        // Authoritative actuator commands are emitted every cycle (100 ms).
        // 0x110 SYS_MODE_CMD carries the resolved mode + rolling counter.
        // ESTOP is no longer encoded here: the enum is MANUAL/AUTO only, and the
        // latched E-stop state travels on 0x011 SYS_SAFETY_STS.estop_active.
        // During ESTOP we report MANUAL so MTR's existing "0x110 == MANUAL while
        // 0x011.estop_active == 1" rule keeps motion disabled.
        //
        // Issue #5/#7: any active traction inhibit (transient or latched) also
        // clamps the transmitted mode to MANUAL and drops power authority, so a
        // Issue #3: physical wheel-speed EGAS (OPTIONAL). Active only when the
        // SYS build is configured with a fitted wheel encoder
        // (ETRIKE_SYS_PHYSICAL_WHEEL_SENSOR). On the current encoder-less vehicle
        // (kPhysicalWheelSensorInstalled == false) this is compiled out entirely.
        // When enabled it compares the measured wheel speed (0x122, RT) against
        // the 0x204 requested speed and escalates a persistent runaway / direction
        // mismatch / stall to ESTOP — the same latch authority as the command-path
        // EGAS check. If an encoder-equipped vehicle reports FAULT/ACQUIRING or the
        // 0x122 stream goes stale, that is an unavailable sensor and is escalated
        // by the caller's freshness policy (kPhysicalEgasFreshMs), never silently
        // downgraded to "no sensor".
        if constexpr (sys::kPhysicalWheelSensorInstalled) {
            static sys::PhysicalEgasMonitor phys_egas;
            sys::PhysicalEgasInput pin;
            pin.sensor_installed = true;
            const uint32_t ws_tick = g_last_wheel_speed_tick.load(std::memory_order_relaxed);
            pin.frame_fresh = (xTaskGetTickCount() - ws_tick)
                              <= pdMS_TO_TICKS(sys::kPhysicalEgasFreshMs);
            pin.sensor_state = g_wheel_sensor_state.load(std::memory_order_relaxed);
            pin.measured_mmps = g_wheel_measured_mmps.load(std::memory_order_relaxed);
            pin.commanded_mmps = g_setpoint_speed_mmps.load(std::memory_order_relaxed);
            const auto phys_verdict = phys_egas.update(pin);
            if (phys_verdict != sys::PhysicalEgasVerdict::OK) {
                ESP_LOGE(TAG, "Physical EGAS trip (%d): cmd=%d measured=%d state=%u",
                         static_cast<int>(phys_verdict), pin.commanded_mmps,
                         pin.measured_mmps, pin.sensor_state);
                if (g_mode_mgr.mode() != can::Mode::Estop) {
                    g_mode_mgr.force_estop();
                    trigger_estop_ack_watchdog(xTaskGetTickCount());
                    if (can_send_estop()) {
                        send_estop_frame("ESTOP");
                    }
                }
            }
        }

        // MTR-feedback loss or brake fault is an actuator-level safety action,
        // not merely an internal zero.
        {
            const can::Mode resolved = g_mode_mgr.mode();
            const bool mode_is_estop = (resolved == can::Mode::Estop);
            const bool resolved_auto = (resolved == can::Mode::Auto);
            const bool hmi_valid = g_power_request_valid.load(std::memory_order_relaxed);
            const bool power_req = hmi_valid
                ? g_hmi_pwr_on.load(std::memory_order_relaxed) : true;

            const auto auth = sys::resolve_authority(mode_is_estop, resolved_auto, power_req);

            can::Frame fr_m;
            can::gen::SysModeCmd message{};
            message.mode = auth.mode_auto ? 1u : 0u;
            message.rolling_counter = roll_mode_++;
            if (can::gen::encode_sys_mode_cmd(message, fr_m) == can::gen::CodecStatus::Ok)
                send_can(fr_m);

            can::Frame fr_p;
            can::gen::SysPwrCmd pwr_msg{};
            pwr_msg.power_state = auth.power_on ? 1u : 0u;
            pwr_msg.rolling_counter = roll_pwr_++;
            if (can::gen::encode_sys_pwr_cmd(pwr_msg, fr_p) == can::gen::CodecStatus::Ok)
                send_can(fr_p);
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Gear task (prio 3, 50 Hz) ──────────────────────────────────────

[[noreturn]] static void task_gear(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kGearCheckHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_alive_gear.store(xTaskGetTickCount(), std::memory_order_relaxed);
        can::Mode mode = g_mode_mgr.mode();
        // MTR owns motor: monitor gear mismatch via CAN
        uint8_t reported  = g_mtr_gear_state.load(std::memory_order_relaxed);   // from 0x206
        uint8_t commanded = g_setpoint_gear.load(std::memory_order_relaxed);    // from 0x204
        static int mismatch_ticks = 0;
        if (reported != commanded && mode == can::Mode::Auto) {
            if (++mismatch_ticks > 50) {  // 500ms debounce
                ESP_LOGE(TAG, "Gear mismatch: cmd=%d rpt=%d", commanded, reported);
                mismatch_ticks = 0;
            }
        } else { mismatch_ticks = 0; }
        vTaskDelayUntil(&last, period);
    }
}

// ── Brake task (prio 3, 50 Hz) ─────────────────────────────────────

// Issue #3: SYS is the SOLE normal producer of the final SEB brake command
// 0x7B9. RT expresses brake *intent* via 0x205 RT_BRAKE_CMD (kPa); SYS applies
// it (g_brake_pressure_kpa, with the stale-0x205 -> max-brake fallback below)
// through BrakeControl, which also enforces ESTOP/lever override priority. RT
// no longer transmits 0x7B9 in normal AUTO — it only ever becomes the emergency
// fallback writer on SYS-loss (see rt-esp32), so the same-CAN-ID dual-producer
// collision is eliminated by construction.
[[noreturn]] static void task_brake(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kBrakeCmdRateHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_alive_brake.store(xTaskGetTickCount(), std::memory_order_relaxed);
        bool lever     = g_safety.brake_lever_pressed();
        bool estop     = (g_mode_mgr.mode() == can::Mode::Estop);
        can::Mode mode = g_mode_mgr.mode();
        int32_t brake_kpa = g_brake_pressure_kpa.load(std::memory_order_relaxed);
        const TickType_t brake_age =
            xTaskGetTickCount() - g_last_brake_setpoint_tick.load(std::memory_order_relaxed);
        if (mode == can::Mode::Auto
            && brake_age > pdMS_TO_TICKS(sys::kBrakeSetpointStaleMs)) {
            // Never reuse an old RT brake value after its real-time deadline.
            brake_kpa = shared::kMaxBrakeKpa;
        }

        can::custom::seb::Command seb_cmd;
        uint8_t  seb_b0 = g_seb_status_byte0.load(std::memory_order_relaxed);
        uint16_t seb_stroke = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
        bool should_tx = g_brake.tick(lever, estop, brake_kpa, mode,
                                      seb_b0, seb_stroke, seb_cmd);
        // Store commanded stroke for the following-error monitor even when not
        // transmitting (e.g. during boot state).
        if (should_tx) {
            g_cmd_stroke_raw.store(seb_cmd.stroke_request_raw, std::memory_order_relaxed);
        }
        // Sole normal producer: always transmit when BrakeControl says so.
        // No RT-health suppression — RT does not command SEB in normal operation.
        if (should_tx) {
            can::Frame fr;
            if (can::custom::seb::encode_command(seb_cmd, fr) == can::gen::CodecStatus::Ok)
                send_can(fr, "brake"); // 0x7B9 VCU_SEB_REQ
        }

        // 0x721 staleness check (architecture §8.10, issue #5, BUG-06): a lost SEB
        // status stream means brake availability is UNKNOWN. This is a B-class
        // recoverable inhibit (kInhibitSebCommsLoss): while set, the mode task
        // drops power/mode authority (traction inhibited). It clears only after
        // several consecutive fresh 0x721 observations (confirmed recovery).
        // g_bypass_seb_sync (bench/sim without an SEB node) disables the trip.
        {
            const bool seb_seen = g_seb_seen.load(std::memory_order_acquire);
            const TickType_t now_ticks = xTaskGetTickCount();
            if (!g_bypass_seb_sync && !seb_seen) {
                // Startup acquisition window: traction is inhibited until SEB is seen.
                sys::set_inhibit(sys::kInhibitSebCommsLoss);
                if (now_ticks >= pdMS_TO_TICKS(sys::kSebStartupAcquireMs)) {
                    static TickType_t last_boot_warn = 0;
                    if (last_boot_warn == 0
                        || (now_ticks - last_boot_warn) >= pdMS_TO_TICKS(1000)) {
                        ESP_LOGW(TAG, "SEB not detected within startup deadline (%d ms) — traction inhibited",
                                 sys::kSebStartupAcquireMs);
                        last_boot_warn = now_ticks;
                    }
                }
            } else if (!g_bypass_seb_sync) {
                // SEB has been acquired at least once; monitor runtime staleness.
                TickType_t last = g_last_seb_status_tick.load(std::memory_order_relaxed);
                const bool stale = (now_ticks - last) >= pdMS_TO_TICKS(sys::kSebStatusTimeoutMs);
                if (stale) {
                    sys::set_inhibit(sys::kInhibitSebCommsLoss);
                    static TickType_t last_staleness_warn = 0;
                    if (last_staleness_warn == 0
                        || (now_ticks - last_staleness_warn) >= pdMS_TO_TICKS(1000)) {
                        ESP_LOGW(TAG, "0x721 SEB_STATUS stale — %lu ms since last frame",
                                 (unsigned long)((now_ticks - last) * portTICK_PERIOD_MS));
                        last_staleness_warn = now_ticks;
                    }
                } else if (sys::g_inhibit_reasons.load() & sys::kInhibitSebCommsLoss) {
                    // Fresh status present: confirmed recovery
                    // (consecutive fresh observations at this 50 Hz cadence).
                    static int seb_comms_recover_count = 0;
                    if (++seb_comms_recover_count >= 3) {
                        sys::clear_inhibit(sys::kInhibitSebCommsLoss);
                        seb_comms_recover_count = 0;
                    }
                }
            } else if (sys::g_inhibit_reasons.load() & sys::kInhibitSebCommsLoss) {
                // Bypassed mode recovery
                sys::clear_inhibit(sys::kInhibitSebCommsLoss);
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Lights task (prio 3, 20 Hz) ────────────────────────────────────

[[noreturn]] static void task_lights(void*) {
    TickType_t period = pdMS_TO_TICKS(50);  // 20 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        can::Mode mode = g_mode_mgr.mode();
        bool lever     = g_safety.brake_lever_pressed();
        uint8_t bits   = g_light_bits.load(std::memory_order_relaxed);

#ifdef TESTING
        bool sw_L = false, sw_R = false, sw_H = false;
#else
        bool sw_L = (gpio_get_level(static_cast<gpio_num_t>(sys::kSwitchLeftTurn)) == 0);
        bool sw_R = (gpio_get_level(static_cast<gpio_num_t>(sys::kSwitchRightTurn)) == 0);
        bool sw_H = (gpio_get_level(static_cast<gpio_num_t>(sys::kSwitchHeadlight)) == 0);
#endif

        // Brake light OR-logic (§8.6): add SEB stroke check — if SEB is actually
        // braking (stroke > 0.5mm ≈ raw 610), light the brake lamp.
        uint16_t seb_raw = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
        bool seb_braking = (seb_raw > 610);  // 610 raw ≈ 0.5mm
        auto out = g_lights.tick(mode, lever, bits, sw_L, sw_R, sw_H, seb_braking);
#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightBrake), out.brake_lamp ? 1 : 0);
#endif

        // Pack light output state for 0x011 byte 2 (v0.0.5 — CAN feedback)
        uint8_t ls = 0;
        if (out.left_lamp)  ls |= (1u << 0);
        if (out.right_lamp) ls |= (1u << 1);
        if (out.brake_lamp) ls |= (1u << 2);
        if (out.head_lamp)  ls |= (1u << 3);
        g_light_state.store(ls, std::memory_order_relaxed);

        vTaskDelayUntil(&last, period);
    }
}

// ── Indicator task (prio 2, 5 Hz) ──────────────────────────────────

[[noreturn]] static void task_indicator(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        [[maybe_unused]] auto out = g_indicator.tick(g_mode_mgr.mode());
#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbAuto), out.auto_bulb ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbManual), out.manual_bulb ? 1 : 0);
#endif

        // Green "ready" bulb: AUTO or MANUAL, RT alive, no brake/traction fault
        can::Mode mode = g_mode_mgr.mode();
        [[maybe_unused]] bool ready = (mode == can::Mode::Auto || mode == can::Mode::Manual)
                  && g_safety.heartbeat_ok()
                  && !sys::traction_fault_present();
        // Red "ESTOP" bulb: dedicated, independent of brake lamp
        [[maybe_unused]] bool estop = (mode == can::Mode::Estop);

#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbReady), ready ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbEstop), estop ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbBypass), g_bench_solo_mode ? 1 : 0);
#endif

        vTaskDelayUntil(&last, period);
    }
}

// ── Power task (prio 2, 5 Hz) ─────────────────────────────────────

[[noreturn]] static void task_power(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        [[maybe_unused]] bool on = (g_mode_mgr.mode() != can::Mode::Estop);
#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kPower12vRelay), on ? 1 : 0);
#endif

        vTaskDelayUntil(&last, period);
    }
}

// ── CAN TX task (prio 2, 5 Hz) — 0x011 SYS_SAFETY_STS ──────────────

// ── 0x500 SYS_NODE_STATUS (observational, issue-added NODE_STATUS) ──
// Strictly observational: never changes mode/authority. Reports SYS's own
// node_state + reason mask so hosts/RT can see the authoritative latch without
// inferring it from 0x011. block_mask = low 8 bits transient inhibit reasons |
// high 8 bits latched fault reasons (see inhibit_state.h).
static can::gen::SysNodeStatus build_sys_node_status() {
    can::gen::SysNodeStatus ns{};
    const can::Mode m = g_mode_mgr.mode();
    const bool estop = sys_estop_latched();
    const bool inhibit = sys::any_inhibit();
    if (estop) {
        ns.node_state = can::gen::SysNodeStatus::kNodeStateEstop;
    } else if (inhibit) {
        ns.node_state = can::gen::SysNodeStatus::kNodeStateInhibited;
    } else if (m == can::Mode::Auto) {
        ns.node_state = can::gen::SysNodeStatus::kNodeStateActive;
    } else {
        ns.node_state = can::gen::SysNodeStatus::kNodeStateStandby;
    }
    const uint32_t inhibit_bits =
        sys::g_inhibit_reasons.load(std::memory_order_relaxed);
    const uint32_t latched_bits =
        sys::g_latched_fault_reasons.load(std::memory_order_relaxed);
    ns.block_mask = static_cast<uint16_t>(
        (inhibit_bits & 0xFFu) | ((latched_bits & 0xFFu) << 8));
    ns.estop_active = estop;
    ns.estop_latched = estop;
    ns.ready = !estop && !inhibit && g_safety.heartbeat_ok();
    ns.output_enabled = !estop && !inhibit;
    ns.degraded = g_brake_fault_active.load(std::memory_order_relaxed)
               || sys::traction_fault_present();
    ns.recovery_pending = false;
    return ns;
}

[[noreturn]] static void task_can_tx(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz (SYS_SAFETY_STS cycle)
    TickType_t last   = xTaskGetTickCount();
    static uint8_t safety_roll = 0;
    static uint8_t node_status_roll = 0;
    while (1) {
        g_alive_can_tx.store(xTaskGetTickCount(), std::memory_order_relaxed);
        can::Frame fr;
        const uint8_t lights = g_light_state.load(std::memory_order_relaxed);
        can::gen::SysSafetySts message{};
        // ESTOP authority is reported here as a latched safety state, separate
        // from 0x110 SYS_MODE_CMD (which is clamped to MANUAL/AUTO). While the
        // E-stop is latched the mode may read MANUAL, but estop_active stays
        // set until a validated REARM clears it.
        message.estop_active = sys_estop_latched();
        message.heartbeat_ok = g_safety.heartbeat_ok();
        message.light_left = lights & 0x01;
        message.light_right = lights & 0x02;
        message.light_brake = lights & 0x04;
        message.light_head = lights & 0x08;
        message.rolling_counter = safety_roll++;
        // E2E: CRC-8 over protected payload bytes [0..3]; fill the CRC field
        // before the final encode.
        message.e2e_crc = 0;
        can::Frame tmp;
        if (can::gen::encode_sys_safety_sts(message, tmp) == can::gen::CodecStatus::Ok) {
            message.e2e_crc = can::e2e::sys_safety_sts_crc(tmp.data.data());
            if (can::gen::encode_sys_safety_sts(message, fr) == can::gen::CodecStatus::Ok)
                send_can(fr, "safety");
        }

        // ── 0x500 SYS_NODE_STATUS (same 5 Hz cadence) ──────────────
        can::gen::SysNodeStatus ns = build_sys_node_status();
        ns.rolling_counter = node_status_roll++;
        ns.e2e_crc = 0;
        if (can::gen::encode_sys_node_status(ns, tmp) == can::gen::CodecStatus::Ok) {
            ns.e2e_crc = can::e2e::crc8_h2f(tmp.data.data(), 7u, 0u);
            if (can::gen::encode_sys_node_status(ns, fr) == can::gen::CodecStatus::Ok)
                send_can(fr, "node-status");
        }

        vTaskDelayUntil(&last, period);
    }
}
// ── Diag task (prio 1, 1 Hz) — 0x600 SYS_DIAG_RPT ──────────────────

[[noreturn]] static void task_diag(void*) {
    TickType_t period = pdMS_TO_TICKS(1000);  // 1 Hz
    TickType_t last   = xTaskGetTickCount();
    static int bus_off_count = 0;
    static uint8_t previous_task_health = 0xFF;
    while (1) {
        const TickType_t now_ticks = xTaskGetTickCount();
        const TickType_t task_deadline = pdMS_TO_TICKS(1500);
        auto fresh = [now_ticks, task_deadline](const std::atomic<uint32_t>& alive) {
            const TickType_t last_alive = alive.load(std::memory_order_relaxed);
            return last_alive != 0 && (now_ticks - last_alive) <= task_deadline;
        };
        uint8_t task_health = (fresh(g_alive_safety)   ? 0x01 : 0)
                            | (fresh(g_alive_brake)    ? 0x02 : 0)
                            | (fresh(g_alive_dispatch) ? 0x04 : 0)
                            | (fresh(g_alive_can_tx)   ? 0x08 : 0)
                            | (fresh(g_alive_can_ctrl) ? 0x10 : 0)
                            | (fresh(g_alive_hb)       ? 0x20 : 0)
                            | (fresh(g_alive_mode)     ? 0x40 : 0)
                            | (fresh(g_alive_gear)     ? 0x80 : 0);
        g_task_health_bits.store(task_health, std::memory_order_relaxed);
        if (task_health != previous_task_health) {
            if (task_health == 0xFF) {
                ESP_LOGI(TAG, "SYS task health recovered: mask=0x%X", task_health);
            } else {
                ESP_LOGE(TAG, "SYS task deadline missed: mask=0x%X expected=0xFF", task_health);
            }
            previous_task_health = task_health;
        }

        // Issue RC2: SYS must REACT to the death of its own safety-critical
        // tasks, not merely log it. task_safety (bit0) polls the physical ESTOP
        // button + RT heartbeat; task_brake (bit1) is the sole normal 0x7B9
        // producer; task_dispatch (bit2) feeds every RX decoder; task_can_tx
        // (bit3) publishes the 0x011 authority stream; task_mode (bit6) publishes
        // 0x110/0x113 authority. If any of these misses its 1.5 s deadline for
        // >= 2 consecutive 1 Hz diag cycles, SYS cannot guarantee a safe stop, so
        // it latches ESTOP and broadcasts 0x001. (task_hb loss is handled
        // downstream by RT's 0x7FE timeout -> SYS_DEGRADED; lights/indicator/gear
        // and can_ctrl are non-life-critical and excluded.)
        static constexpr uint8_t kCriticalTaskMask =
            0x01 /*safety*/ | 0x02 /*brake*/ | 0x04 /*dispatch*/
            | 0x08 /*can_tx*/ | 0x40 /*mode*/;
        static int critical_miss_count = 0;
        if ((task_health & kCriticalTaskMask) != kCriticalTaskMask) {
            if (++critical_miss_count >= 2) {   // persistent >= 2 s miss
                if (g_mode_mgr.mode() != can::Mode::Estop) {
                    ESP_LOGE(TAG, "SYS critical task(s) dead (mask=0x%X need 0x%02X) — "
                                  "forcing ESTOP", task_health, kCriticalTaskMask);
                    g_mode_mgr.force_estop();
                    if (can_send_estop()) {
                        send_estop_frame("ESTOP");
                    }
                }
            }
        } else {
            critical_miss_count = 0;
        }

        // Send 0x600 with real TEC/REC
        uint8_t tec = 0, rec = 0;
        g_can.get_error_counters(tec, rec);

        can::gen::SysDiagRpt rpt;
        rpt.mode = g_mode_mgr.mode_u8();
        rpt.brake_engaged = g_safety.brake_lever_pressed();
        rpt.brake_fault = g_brake_fault_active.load(std::memory_order_relaxed)
                       || sys::traction_fault_present();
        rpt.heartbeat_ok  = g_safety.heartbeat_ok();
        rpt.estop_active = (g_mode_mgr.mode() == can::Mode::Estop);
        rpt.free_heap_kb = static_cast<uint16_t>(esp_get_free_heap_size() / 1024);
        rpt.tec = tec; rpt.rec = rec;
        // Report CAN RX overflow count (6-bit, saturated at 63)
        {
            uint32_t ov = g_can_rx_overflow.load(std::memory_order_relaxed);
            rpt.rx_overflow = ov > 63 ? 63 : static_cast<uint8_t>(ov);
        }
        can::Frame fr;
        if (can::gen::encode_sys_diag_rpt(rpt, fr) == can::gen::CodecStatus::Ok) send_can(fr);

        // CAN bus-off monitoring is state-driven. TEC/REC are telemetry only.
        const auto can_health = g_can.health_snapshot();
        if (can_health.state == can::CanDriver::HealthState::Passive)
            ESP_LOGW(TAG, "CAN error-passive: TEC=%u REC=%u", tec, rec);
        if (can_health.state == can::CanDriver::HealthState::BusOff) {
            ESP_LOGE(TAG, "CAN bus-off: TEC=%u REC=%u", tec, rec);
            bus_off_count++;
            if (bus_off_count >= 5) {
                ESP_LOGE(TAG, "CAN bus-off persistent — forcing ESTOP");
                g_mode_mgr.force_estop();
                if (can_send_estop()) {
                    send_estop_frame("ESTOP");
                }
            }
        } else { bus_off_count = 0; }

        vTaskDelayUntil(&last, period);
    }
}

// ── Heartbeat task (prio 1, 10 Hz) — 0x7FE SYS_HEARTBEAT ────────────

[[noreturn]] static void task_hb(void*) {
    TickType_t period = pdMS_TO_TICKS(sys::kHeartbeatIntervalMs);
    TickType_t last   = xTaskGetTickCount();
    uint8_t alive_ctr = 0;
    while (1) {
        g_alive_hb.store(xTaskGetTickCount(), std::memory_order_relaxed);
        can::gen::SysHeartbeat message{};
        message.alive_ctr = ++alive_ctr;
        message.heartbeat_ok = g_safety.heartbeat_ok();
        message.estop_active = sys_estop_latched();
        message.mode_auto = g_mode_mgr.mode() == can::Mode::Auto;
        const uint8_t task_health = g_task_health_bits.load(std::memory_order_relaxed);
        message.task_safety_ok = task_health & 0x01;
        message.task_brake_ok = task_health & 0x02;
        message.task_dispatch_ok = task_health & 0x04;
        message.task_can_tx_ok = task_health & 0x08;
        // can_ok: set only while below the CAN error-passive threshold.
        {
            const auto can_health = g_can.health_snapshot();
            message.can_ok = can_health.state == can::CanDriver::HealthState::Active
                          || can_health.state == can::CanDriver::HealthState::Warning;
        }
        can::Frame fr;
        if (can::gen::encode_sys_heartbeat(message, fr) == can::gen::CodecStatus::Ok) send_can(fr);

        vTaskDelayUntil(&last, period);
    }
}

// ── Task handles ────────────────────────────────────────────────────

static TaskHandle_t h_can_rx, h_safety, h_dispatch, h_mode;
static TaskHandle_t h_gear, h_brake, h_lights;
static TaskHandle_t h_indicator, h_power, h_can_tx, h_can_control, h_diag, h_hb;

// Put every connected SYS GPIO in a deterministic, non-actuating state before
// starting CAN or tasks. GPIO reset defaults leave button inputs floating.
static void init_board_gpio() {
    constexpr uint64_t kOutputPins = (1ULL << sys::kLightBrake)
                                  | (1ULL << sys::kBulbAuto)
                                  | (1ULL << sys::kBulbManual)
                                  | (1ULL << sys::kBulbReady)
                                  | (1ULL << sys::kBulbEstop)
                                  | (1ULL << sys::kBulbBypass)
                                  | (1ULL << sys::kPower12vRelay);
                                  // | (1ULL << sys::kWdtToggleGpio);
    constexpr uint64_t kPullupInputs = (1ULL << sys::kBrakeLeverGpio)
                                    | (1ULL << sys::kStartBtnGpio)
                                    | (1ULL << sys::kModeBtnGpio)
                                    | (1ULL << sys::kSwitchLeftTurn)
                                    | (1ULL << sys::kSwitchRightTurn)
                                    | (1ULL << sys::kSwitchHeadlight);

    // Latch LOW before enabling output drivers so relay and lamp drivers do
    // not receive an indeterminate boot pulse.
    for (int pin : {sys::kLightBrake,
                    sys::kBulbAuto, sys::kBulbManual,
                    sys::kBulbReady, sys::kBulbEstop, sys::kBulbBypass, sys::kPower12vRelay/*,
                    sys::kWdtToggleGpio*/}) {
        ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(pin), 0));
    }

    gpio_config_t outputs = {};
    outputs.pin_bit_mask = kOutputPins;
    outputs.mode = GPIO_MODE_OUTPUT;
    outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&outputs));

    gpio_config_t estop = {};
    estop.pin_bit_mask = 1ULL << sys::kEstopGpio;
    estop.mode = GPIO_MODE_INPUT;
    estop.pull_up_en = GPIO_PULLUP_ENABLE;
    estop.pull_down_en = GPIO_PULLDOWN_DISABLE;
    estop.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&estop));

    gpio_config_t inputs = {};
    inputs.pin_bit_mask = kPullupInputs;
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_ENABLE;
    inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    inputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&inputs));

}

// ── app_main ────────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "SYS ESP32-S3 initializing...");
    init_board_gpio();
    
    // Evaluate System Run Mode
    {
        bool override_pin_low = false;
        if (SYSTEM_RUN_MODE == 1) {
            gpio_set_direction(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN), GPIO_MODE_INPUT);
            gpio_pullup_en(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN));
            // Brief delay to let pull-up stabilize
            vTaskDelay(pdMS_TO_TICKS(10));
            override_pin_low = (gpio_get_level(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN)) == 0);
        }

        const etrike::BypassState bypass = etrike::evaluate_run_mode_bypasses(SYSTEM_RUN_MODE, override_pin_low);
        g_bench_solo_mode = bypass.bench_solo_mode;
        g_bypass_eps_sync = bypass.bypass_eps_sync;
        g_bypass_seb_sync = bypass.bypass_seb_sync;
        g_bypass_mtr_absent = bypass.bypass_mtr_absent;

        if (g_bench_solo_mode) {
            ESP_LOGE(TAG, "***********************************");
            ESP_LOGE(TAG, "* DEVELOPER BYPASS ACTIVE         *");
            ESP_LOGE(TAG, "* BYPASSING SAFETY SYNC CHECKS!   *");
            ESP_LOGE(TAG, "***********************************");
        } else if (SYSTEM_RUN_MODE == 1) {
            ESP_LOGI(TAG, "Prototype mode: Override pin not jumped. Enforcing safety.");
        } else {
            ESP_LOGI(TAG, "Production mode: Safety checks enforced.");
        }
    }

    // 0. NVS init + crash persistence
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        nvs_handle_t nvs;
        uint32_t reset_count = 0;
        esp_reset_reason_t reason = esp_reset_reason();

        if (nvs_open("sys_diag", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_get_u32(nvs, "reset_count", &reset_count);
            reset_count++;
            nvs_set_u32(nvs, "reset_count", reset_count);
            nvs_set_u32(nvs, "reset_reason", static_cast<uint32_t>(reason));
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        ESP_LOGI(TAG, "Reset reason: %d, reset count: %lu", static_cast<int>(reason), static_cast<unsigned long>(reset_count));
    }

    // 1. Init CAN driver
    if (!g_can.init()) {
        ESP_LOGE(TAG, "CAN init failed");
        return;
    }

    // 2. Init modules
    g_safety.init();
    g_mode_mgr.init();
    g_brake.init();
    g_lights.init();
    g_indicator.init();
    // g_wdt.init();

    // Init status bulbs (green=ready, red=ESTOP, amber=bypass) — start ready/estop OFF, bypass reflects solo mode
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbReady), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(sys::kBulbReady), 0);
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbEstop), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(sys::kBulbEstop), 0);
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbBypass), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(sys::kBulbBypass), g_bench_solo_mode ? 1 : 0);


    // 3. Create queues
    g_can_rx_queue   = xQueueCreate(16, sizeof(can::Frame));
    ESP_LOGI(TAG, "Queues created");

    // 4. Create tasks (priority, stack from architecture.md §8.7)
    xTaskCreate(task_can_rx,    "can_rx",    4608, nullptr, 5, &h_can_rx);
    xTaskCreate(task_safety,    "safety",    4608, nullptr, 5, &h_safety);
    xTaskCreate(task_dispatch,  "dispatch",  3584, nullptr, 4, &h_dispatch);
    xTaskCreate(task_mode,      "mode",      2560, nullptr, 4, &h_mode);
    xTaskCreate(task_gear,      "gear",      2048, nullptr, 3, &h_gear);
    xTaskCreate(task_brake,     "brake",     3584, nullptr, 3, &h_brake);
    xTaskCreate(task_lights,    "lights",    2560, nullptr, 3, &h_lights);
    xTaskCreate(task_indicator, "indicator", 2560, nullptr, 2, &h_indicator);
    xTaskCreate(task_power,     "power",     2560, nullptr, 2, &h_power);
    xTaskCreate(task_can_tx,    "can_tx",    3584, nullptr, 2, &h_can_tx);
    xTaskCreate(task_can_control,"can_ctrl",  2560, nullptr, 2, &h_can_control);
    xTaskCreate(task_diag,      "diag",      3584, nullptr, 1, &h_diag);
    xTaskCreate(task_hb,        "hb",        2560, nullptr, 1, &h_hb);

    ESP_LOGI(TAG, "Ready — 13 tasks running (vehicle, MTR owns motor). Mode=%s", g_mode_mgr.name());
    vTaskDelete(nullptr);
}
