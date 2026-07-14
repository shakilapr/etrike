// SYS ESP32-S3 — Safety, Motor Actuation & Body Control.
// Architecture: architecture.md §8.
// 15 FreeRTOS tasks, all wired to real implementation modules.
// Phases S1-S4: CAN RX, dispatch, motor, safety, mode, throttle, brake,
//               lights, indicator, power, can_tx, diag, heartbeat.

// Runtime System Mode Configuration
#include "system_mode.h"

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
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#error "ESP-IDF 5.0 or later required"
#endif

#include "config.h"
#include "can/can_protocol.h"
#include "can/codec_transport.h"
#include "can/manual/vendor_protocol.h"
#include "can/can_driver.h"
#include "safety_monitor.h"
#include "mode_manager.h"

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
        if (fr.id == 0x7B9 || fr.id == 0x001 || fr.id == 0x011) {
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
            ESP_LOGW(TAG, "CAN TX mailbox full (%s) — frame %03X dropped", caller, fr.id);
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
static std::atomic<uint8_t>  g_seb_status_byte0{0xFF}; // byte 0 from 0x721 (alignment + error_status), 0xFF = no frame yet
static std::atomic<uint8_t>  g_mtr_gear_state{0};     // gear state from 0x206 MTR_MOTOR_FBK (C6b)

// 0x204 staleness tracking (arch §8.6: 200ms timeout → zero speed + neutral)
static std::atomic<uint32_t> g_last_setpoint_tick{0};

// Gap #14: Rate-limit 0x001 ESTOP broadcasts. Prevents flooding.
static std::atomic<int64_t>  g_last_estop_sent_us{0};

static bool can_send_estop() {
    return shared::should_send_estop_now(g_last_estop_sent_us, esp_timer_get_time());
}

// ── Motor feedback from 0x206 MTR_MOTOR_FBK ─────────────────────────
static std::atomic<int16_t>  g_actual_speed_mmps{0};
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

// ── ESTOP trigger timestamp (Gap #15: MTR ACK check) ────────────────
static std::atomic<uint32_t> g_last_estop_trigger_tick{0};

// ── 0x206 staleness tracking (Gap #15) ───────────────────────────────
static std::atomic<uint32_t> g_last_mtr_fbk_tick{0};

// ── SEB fault state for 0x600 diag (Gap #13) ─────────────────────────
static std::atomic<uint8_t>  g_seb_error_status{0};   // from 0x721 byte0 bits6-7
static std::atomic<bool>     g_brake_fault_active{false};

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
        }
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
            if (can::decode_frame(fr, sp) != can::gen::CodecStatus::Ok) break;
            g_setpoint_speed_mmps.store(sp.motor_speed_mmps, std::memory_order_relaxed);
            g_setpoint_gear.store(sp.gear, std::memory_order_relaxed);
            g_last_setpoint_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
            break;
        }
        case can::kIdRtBrakeCmd: {   // 0x205
            can::gen::RtBrakeCmd brk{};
            if (can::decode_frame(fr, brk) != can::gen::CodecStatus::Ok) break;
            g_brake_pressure_kpa.store(brk.brake_pressure_kpa, std::memory_order_relaxed);
            break;
        }
        case can::kIdHmiModeReq: {   // 0x111 — HMI mode heartbeat (1Hz)
            can::gen::HmiModeReq request{};
            if (can::decode_frame(fr, request) != can::gen::CodecStatus::Ok) break;
            if (g_mode_mgr.parse_hmi_mode(request.hmi_req_mode ? 1 : 0)) {
                // If mode actually changed due to this request, log it.
                // The main 10Hz control loop will naturally pick up the new mode 
                // and broadcast 0x110 SYS_MODE_CMD on its next tick.
                ESP_LOGI(TAG, "HMI changed mode to %s", g_mode_mgr.name());
            }
            break;
        }
        case can::kIdHmiPwrReq: {    // 0x112 — HMI power heartbeat (1Hz)
#if ENABLE_CAN_HMI
            // Phase 0/1: Just log it for now. Ignition control will be wired later
            // to a specific GPIO or power manager task.
            // uint8_t req_start = fr.u8_at(0);
#endif
            break;
        }
        case can::kIdMtrMotorFbk: {  // 0x206 — EGAS L2 feedback (arch §8.3)
            can::gen::MtrMotorFbk fbk{};
            if (can::decode_frame(fr, fbk) != can::gen::CodecStatus::Ok) break;
            g_actual_speed_mmps.store(fbk.actual_speed_mmps, std::memory_order_relaxed);
            g_motor_fault_flags.store(fbk.fault_flags, std::memory_order_relaxed);
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
                    can::Frame ef; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                    send_can(ef, "ESTOP");
                }
            }
            break;
        }
        case can::kIdHostLightCmd: { // 0x302
            can::gen::HostLightCmd lights{};
            if (can::decode_frame(fr, lights) != can::gen::CodecStatus::Ok) break;
            uint8_t bits = (lights.left_turn ? 1u : 0u)
                         | (lights.right_turn ? 2u : 0u)
                         | (lights.brake_light ? 4u : 0u)
                         | (lights.headlight ? 8u : 0u);
            g_light_bits.store(bits, std::memory_order_relaxed);
            break;
        }
        case can::kIdSafetyEstop: {  // 0x001 — rate-limited RX (Gap #14)
            // Always process the safety state change — rate-limiting must
            // never suppress safety override processing (bug 6.3).
            // Rate-limit only downstream actions (logging, CAN forwarding).
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
            g_mode_mgr.force_estop();
            g_last_estop_trigger_tick.store(now, std::memory_order_relaxed);
            if (within_limit) {
                ESP_LOGW(TAG, "ESTOP via CAN 0x001");
            }
            break;
        }
        case can::kIdBbwStatus: {  // 0x721
            can::manual::SebStatusValue value{};
            if (can::manual::decode_seb_status(fr, value) != can::gen::CodecStatus::Ok) break;
            // Store byte 0 atomically (alignment, error_status, control_mode) —
            // eliminates data race with brake task reading raw byte array (H3).
            g_seb_status_byte0.store(value.status_byte, std::memory_order_relaxed);
            // F7: Extract SEB error_status from byte 0 bits 6-7 (architecture §8.10)
            {
                uint8_t es = value.error_status;
                g_seb_error_status.store(es, std::memory_order_relaxed);
                if (es >= 3) {
                    ESP_LOGE(TAG, "SEB error_status L3 in 0x721 (status=0x%02x)", value.status_byte);
                    g_brake_fault_active.store(true, std::memory_order_relaxed);
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
                    actual_raw = value.stroke_value;
                } else {
                    actual_raw = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
                }
                g_seb_actual_stroke_raw.store(actual_raw, std::memory_order_relaxed);
            }
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
            // Brake following error monitor (§8.10): cmp cmd vs actual stroke.
            // Only in Stroke mode — in Pressure mode cmd_stroke is fixed at 600
            // (0mm baseline) while the SEB physically moves to build pressure,
            // which would false-trigger the following error (bug 6.1).
            {
                uint8_t seb_ctrl = value.control_mode;
                if (seb_ctrl == 0) {  // Stroke mode only
                    uint16_t cmd = g_cmd_stroke_raw.load(std::memory_order_relaxed);
                    uint16_t actual_raw = value.stroke_value;
                    uint16_t diff = (cmd > actual_raw) ? (cmd - actual_raw) : (actual_raw - cmd);
                    static bool  brake_follow_active = false;
                    static TickType_t brake_follow_start = 0;
                    if (diff > sys::kBrakeFollowingErrRaw) {
                        if (!brake_follow_active) {
                            brake_follow_active = true;
                            brake_follow_start = xTaskGetTickCount();
                        } else if ((xTaskGetTickCount() - brake_follow_start)
                                    >= pdMS_TO_TICKS(sys::kBrakeFollowingErrMs)) {
                            ESP_LOGE(TAG, "Brake following err: cmd=%u actual=%u diff=%u raw (~%d mm)",
                                     cmd, actual_raw, diff, int(diff * 0.05f));
                            g_brake_fault_active.store(true, std::memory_order_relaxed);
                            brake_follow_active = false;  // log once per event
                        }
                    } else {
                        brake_follow_active = false;
                    }
                }
            }
            break;
        }
        case can::kIdBbwTest: {     // 0x6FB — SEB_Test telemetry (arch §8.3)
            // Motor current: Byte1-2 i16 LE, scale 0.0078125 A/bit
            // ECU temp: Byte3-4 u16 LE, scale 0.5 C/bit, offset -40
            can::manual::TestTelemetry telemetry{};
            if (can::manual::decode_test<can::gen::SebTest>(fr, telemetry) != can::gen::CodecStatus::Ok) break;
            uint16_t ecu_raw = telemetry.ecu_temperature;
            int16_t  ecu_temp = int16_t(ecu_raw * 0.5f - 40.0f);
            if (ecu_temp > 80) {
                ESP_LOGW(TAG, "SEB_Test: ECU temp %d C exceeds 80 C threshold", ecu_temp);
            }
            break;
        }
        case can::kIdBbwErrInfo: {  // 0x731 — SEB_ErrInfo (arch §8.3)
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
                if (byte_idx < fr.dlc && (fr.data[byte_idx] & (1 << (kL3Bits[i] % 8)))) {
                    ESP_LOGE(TAG, "SEB L3 fault: bit %d = %s", kL3Bits[i], kL3Names[i]);
                    l3_found = true;
                }
            }
            if (l3_found) {
                g_mode_mgr.force_estop();
                // Record ESTOP trigger tick for MTR ACK timeout check (bug 6.5).
                // Without this, the MTR ESTOP ACK safety check in task_safety
                // is permanently bypassed for SEB-triggered ESTOPs.
                g_last_estop_trigger_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
                if (can_send_estop()) {
                    can::Frame ef; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                    send_can(ef, "ESTOP");
                }
                ESP_LOGW(TAG, "ESTOP triggered by SEB 0x731 L3 fault(s)");
            }
            break;
        }
        case can::kIdBbwVersion: {  // 0x741 — SEB_Version (arch §8.3)
            if (!g_seb_version_logged.load(std::memory_order_relaxed)) {
                uint8_t sw_raw = fr.data[0];
                uint8_t hw_raw = fr.data[1];
                ESP_LOGI(TAG, "SEB_Version: SW=%.2f HW=%.1f",
                         sw_raw * 0.01f, hw_raw * 0.1f);
                g_seb_version_logged.store(true, std::memory_order_relaxed);
            }
            break;
        }
        case can::kIdRtStateRpt: {  // 0x210 — RT safety state for takeover detection
            can::gen::RtStateRpt state{};
            if (can::decode_frame(fr, state) == can::gen::CodecStatus::Ok)
                g_rt_safety_state.store(state.safety_state, std::memory_order_relaxed);
            break;
        }
        case can::kIdRtHeartbeatLow: {  // 0x7FD
            can::gen::RtHeartbeat heartbeat{};
            if (can::decode_frame(fr, heartbeat) == can::gen::CodecStatus::Ok)
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
        // Read hardware ESTOP button (NC: LOW = pressed)
#ifdef TESTING
        bool estop_hw = false;
        bool brake_lever = false;
#else
        bool estop_hw = (gpio_get_level(static_cast<gpio_num_t>(sys::kEstopGpio)) == 0);
        bool brake_lever = (gpio_get_level(static_cast<gpio_num_t>(sys::kBrakeLeverGpio)) == 0);
#endif

        g_safety.set_estop(estop_hw);
        g_safety.set_brake_lever(brake_lever);

        bool estop_triggered = g_safety.estop_active() || !g_safety.heartbeat_ok();
        if (estop_triggered) {
            if (g_mode_mgr.mode() != can::Mode::Estop) {
                g_mode_mgr.force_estop();
                g_last_estop_trigger_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
                // Broadcast CAN 0x001 ESTOP on low bus (architecture §8.4)
                // Gap #14: rate-limited to prevent bus flooding
                if (can_send_estop()) {
                    can::Frame estop_fr;
                    estop_fr.id = can::kIdSafetyEstop;
                    estop_fr.dlc = 0;
                    send_can(estop_fr, "ESTOP");
                    ESP_LOGW(TAG, "ESTOP triggered — sent CAN 0x001");
                }
            }
        }

        // Toggle external watchdog + per-task alive counter
        g_alive_safety.store(xTaskGetTickCount(), std::memory_order_relaxed);
        // g_wdt.tick();  // GPIO23 toggle

        // EGAS L2: compare 0x204 setpoint vs 0x206 actual speed (arch §6.1)
        // Only in AUTO mode. Mismatch > threshold for > duration → ESTOP.
        if (!g_bypass_mtr_absent) {
            static bool  egas_fault_active = false;
            static TickType_t egas_fault_start = 0;
            if (g_mode_mgr.mode() == can::Mode::Auto) {
                int32_t cmd    = g_setpoint_speed_mmps.load(std::memory_order_relaxed);
                int16_t actual = g_actual_speed_mmps.load(std::memory_order_relaxed);
                int32_t diff   = (cmd > actual) ? (cmd - actual) : (actual - cmd);
                if (diff > sys::kEgasSpeedThresholdMmps) {
                    if (!egas_fault_active) {
                        egas_fault_active = true;
                        egas_fault_start = xTaskGetTickCount();
                    } else if ((xTaskGetTickCount() - egas_fault_start)
                                >= pdMS_TO_TICKS(sys::kEgasFaultDurationMs)) {
                        if (g_mode_mgr.mode() != can::Mode::Estop) {
                            g_mode_mgr.force_estop();
                            if (can_send_estop()) {
                                can::Frame ef; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                                send_can(ef, "ESTOP");
                            }
                            ESP_LOGW(TAG, "EGAS L2: speed mismatch %ld mm/s > %d — ESTOP",
                                     (long)diff, sys::kEgasSpeedThresholdMmps);
                        }
                    }
                } else {
                    egas_fault_active = false;
                }
            } else {
                egas_fault_active = false;
            }
        }
        // F3: MTR ESTOP ACK check (Gap #15)
        // After ESTOP triggered, verify MTR sets ESTOP_ACTIVE bit in 0x206 fault_flags.
        {
            uint32_t last_trig = g_last_estop_trigger_tick.load(std::memory_order_relaxed);
            if (last_trig > 0
                && (xTaskGetTickCount() - last_trig) >= pdMS_TO_TICKS(sys::kMtrEstopAckTimeoutMs)) {
                uint8_t flags = g_motor_fault_flags.load(std::memory_order_relaxed);
                if (!(flags & shared::kMtrFaultEstopActive)) {
                    ESP_LOGE(TAG, "MTR ESTOP ACK timeout — retriggering ESTOP");
                    g_mode_mgr.force_estop();
                    if (can_send_estop()) {
                        can::Frame ef{}; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                        send_can(ef, "ESTOP");
                    }
                    g_brake_fault_active.store(true, std::memory_order_relaxed);
                }
                g_last_estop_trigger_tick.store(0, std::memory_order_relaxed);  // reset
            }
        }

        // F4: 0x206 staleness check (Gap #15)
        // Warn if no MTR feedback for >200ms (MTR comms lost).
        // Startup grace: skip if never received (g_last_mtr_fbk_tick == 0).
        {
            uint32_t last_fbk = g_last_mtr_fbk_tick.load(std::memory_order_relaxed);
            if (last_fbk > 0
                && (xTaskGetTickCount() - last_fbk) >= pdMS_TO_TICKS(sys::kMtrFbkStaleMs)) {
                ESP_LOGE(TAG, "0x206 MTR_MOTOR_FBK stale — zeroing speed + neutral");
                g_setpoint_speed_mmps.store(0, std::memory_order_relaxed);
                g_setpoint_gear.store(0, std::memory_order_relaxed);
                g_brake_fault_active.store(true, std::memory_order_relaxed);
            }
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Mode task (prio 4, 10 Hz) ──────────────────────────────────────

[[noreturn]] static void task_mode(void*) {
    TickType_t period = pdMS_TO_TICKS(100);  // 10 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
#ifdef TESTING
        bool mode_btn  = false;
        bool start_btn = false;
#else
        bool mode_btn  = (gpio_get_level(static_cast<gpio_num_t>(sys::kModeBtnGpio)) == 0);
        bool start_btn = (gpio_get_level(static_cast<gpio_num_t>(sys::kStartBtnGpio)) == 0);
#endif

        bool changed = g_mode_mgr.tick(mode_btn, start_btn);
        static int refresh_ctr = 0;
        if (changed || ++refresh_ctr >= 10) {  // on-change OR every 1s
            refresh_ctr = 0;
            can::Frame fr;
            can::gen::SysModeCmd message{g_mode_mgr.mode_u8()};
            if (can::encode_frame(message, fr) == can::gen::CodecStatus::Ok) send_can(fr);
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Gear task (prio 3, 50 Hz) ──────────────────────────────────────

[[noreturn]] static void task_gear(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kGearCheckHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
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

// Gap #12 / Option D: In AUTO mode, RT sends 0x7B9 directly to SEB (1-hop).
// SYS suppresses its own 0x7B9 to avoid bus collision. SYS resumes sending
// in MANUAL, ESTOP, when lever is pressed (rider override), or when RT
// heartbeat is lost (takeover fallback).
[[noreturn]] static void task_brake(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kBrakeCmdRateHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_alive_brake.store(xTaskGetTickCount(), std::memory_order_relaxed);
        bool lever     = g_safety.brake_lever_pressed();
        bool estop     = (g_mode_mgr.mode() == can::Mode::Estop);
        can::Mode mode = g_mode_mgr.mode();
        int32_t brake_kpa = g_brake_pressure_kpa.load(std::memory_order_relaxed);

        // Suppress SYS 0x7B9 in AUTO when RT is healthy, no rider override,
        // AND RT safety_state is Normal (not in InternalEstop/takeover).
        // H1: Also require SEB rolling counter to be incrementing — if RT's
        // 0x7B9 is failing, SEB stops acknowledging and SYS resumes sending.
        bool rt_alive     = g_safety.heartbeat_ok();
        bool rt_normal    = (g_rt_safety_state.load(std::memory_order_relaxed) == 0);
        TickType_t now_ticks = xTaskGetTickCount();
        TickType_t last_roll_change = g_last_seb_roll_change_tick.load(std::memory_order_relaxed);
        bool seb_ack = g_seb_rolling.load(std::memory_order_relaxed)
                    && last_roll_change != 0
                    && (now_ticks - last_roll_change) <= pdMS_TO_TICKS(sys::kSebRollingTimeoutMs);
        g_seb_rolling.store(seb_ack, std::memory_order_relaxed);

        // Fast-path deadman (gap C4): if RT 0x204 setpoint is stale (>200ms),
        // RT has likely crashed — resume direct brake control immediately.
        // This is faster than waiting for the 1000ms heartbeat timeout.
        bool rt_setpoint_fresh = (now_ticks - g_last_setpoint_tick.load(std::memory_order_relaxed))
                                 < pdMS_TO_TICKS(sys::kSetpointStaleMs);
        bool suppress_seb = (mode == can::Mode::Auto) && rt_alive && rt_normal
                           && seb_ack && !lever && !estop && rt_setpoint_fresh;

        can::VcuSebReq seb_cmd;
        uint8_t  seb_b0 = g_seb_status_byte0.load(std::memory_order_relaxed);
        uint16_t seb_stroke = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
        bool should_tx = g_brake.tick(lever, estop, brake_kpa, mode,
                                      seb_b0, seb_stroke, seb_cmd);
        // Store commanded stroke for following-error monitor even when suppressed
        if (should_tx) {
            g_cmd_stroke_raw.store(seb_cmd.stroke_req, std::memory_order_relaxed);
        }
        if (should_tx && !suppress_seb) {
            can::Frame fr;
            if (can::manual::encode(seb_cmd, fr) == can::gen::CodecStatus::Ok)
                send_can(fr, "brake"); // 0x7B9 VCU_SEB_REQ
        }

        // 0x721 staleness check (architecture §8.10): warn if no status for >100ms
        {
            TickType_t last = g_last_seb_status_tick.load(std::memory_order_relaxed);
            if (last > 0) {
                TickType_t age = xTaskGetTickCount() - last;
                if (age >= pdMS_TO_TICKS(sys::kSebStatusTimeoutMs)) {
                    static TickType_t last_staleness_warn = 0;
                    if (last_staleness_warn == 0
                        || (xTaskGetTickCount() - last_staleness_warn)
                            >= pdMS_TO_TICKS(1000)) {
                        ESP_LOGW(TAG, "0x721 SEB_STATUS stale — %lu ms since last frame",
                                 (unsigned long)(age * portTICK_PERIOD_MS));
                        last_staleness_warn = xTaskGetTickCount();
                    }
                }
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
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightLeftTurn), out.left_lamp ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightRightTurn), out.right_lamp ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightBrake), out.brake_lamp ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightHead), out.head_lamp ? 1 : 0);
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

        // Green "ready" bulb: AUTO or MANUAL, RT alive, no brake fault
        can::Mode mode = g_mode_mgr.mode();
        [[maybe_unused]] bool ready = (mode == can::Mode::Auto || mode == can::Mode::Manual)
                  && g_safety.heartbeat_ok()
                  && !g_brake_fault_active.load(std::memory_order_relaxed);
        // Red "ESTOP" bulb: dedicated, independent of brake lamp
        [[maybe_unused]] bool estop = (mode == can::Mode::Estop);

#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbReady), ready ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbEstop), estop ? 1 : 0);
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

[[noreturn]] static void task_can_tx(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_alive_can_tx.store(xTaskGetTickCount(), std::memory_order_relaxed);
        can::Frame fr;
        const uint8_t lights = g_light_state.load(std::memory_order_relaxed);
        can::gen::SysSafetySts message{};
        message.estop_active = g_mode_mgr.mode() == can::Mode::Estop;
        message.heartbeat_ok = g_safety.heartbeat_ok();
        message.light_left = lights & 0x01;
        message.light_right = lights & 0x02;
        message.light_brake = lights & 0x04;
        message.light_head = lights & 0x08;
        if (can::encode_frame(message, fr) == can::gen::CodecStatus::Ok)
            send_can(fr, "safety");

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
                            | (fresh(g_alive_can_tx)   ? 0x08 : 0);
        g_task_health_bits.store(task_health, std::memory_order_relaxed);
        if (task_health != previous_task_health) {
            if (task_health == 0x0F) {
                ESP_LOGI(TAG, "SYS task health recovered: mask=0x%X", task_health);
            } else {
                ESP_LOGE(TAG, "SYS task deadline missed: mask=0x%X expected=0xF", task_health);
            }
            previous_task_health = task_health;
        }

        // Send 0x600 with real TEC/REC
        uint8_t tec = 0, rec = 0;
        g_can.get_error_counters(tec, rec);

        can::gen::SysDiagRpt rpt;
        rpt.sys_diag_mode = g_mode_mgr.mode_u8();
        rpt.sys_diag_brake_engaged = g_safety.brake_lever_pressed();
        rpt.sys_diag_brake_fault = g_brake_fault_active.load(std::memory_order_relaxed);
        rpt.heartbeat_ok  = g_safety.heartbeat_ok();
        rpt.sys_diag_estop_active = (g_mode_mgr.mode() == can::Mode::Estop);
        rpt.sys_diag_free_heap_kb = static_cast<uint16_t>(esp_get_free_heap_size() / 1024);
        rpt.sys_diag_tec = tec; rpt.sys_diag_rec = rec;
        // Report CAN RX overflow count (6-bit, saturated at 63)
        {
            uint32_t ov = g_can_rx_overflow.load(std::memory_order_relaxed);
            rpt.rx_overflow = ov > 63 ? 63 : static_cast<uint8_t>(ov);
        }
        can::Frame fr;
        if (can::encode_frame(rpt, fr) == can::gen::CodecStatus::Ok) send_can(fr);

        // CAN bus-off monitoring (architecture §8.10)
        if (tec > 128)
            ESP_LOGW(TAG, "CAN error-warning: TEC=%u REC=%u", tec, rec);
        if (tec >= 255) {
            ESP_LOGE(TAG, "CAN bus-off: TEC=%u REC=%u", tec, rec);
            bus_off_count++;
            if (bus_off_count >= 5) {
                ESP_LOGE(TAG, "CAN bus-off persistent — forcing ESTOP");
                g_mode_mgr.force_estop();
                if (can_send_estop()) {
                    can::Frame ef; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                    send_can(ef, "ESTOP");
                }
            }
            g_can.recovery();  // lightweight bus-off recovery via twai_initiate_recovery
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
        can::gen::SysHeartbeat message{};
        message.sys_alive_ctr = ++alive_ctr;
        // byte 1: health_flags using shared constants from can_protocol.h
        // bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok
        uint8_t health = (g_safety.heartbeat_ok() ? can::kHbHealthBitHeartbeatOk : 0)
                       | (g_safety.estop_active() ? can::kHbHealthBitEstopActive : 0)
                       | (g_mode_mgr.mode() == can::Mode::Auto ? can::kHbHealthBitModeAuto : 0)
                       | static_cast<uint8_t>(g_task_health_bits.load(std::memory_order_relaxed) << 4);
        // can_ok: set only while below the CAN error-passive threshold.
        {
            uint8_t tec = 0, rec = 0;
            g_can.get_error_counters(tec, rec);
            if (tec < 128) health |= can::kHbHealthBitCanOk;
        }
        message.heartbeat_ok = health & can::kHbHealthBitHeartbeatOk;
        message.estop_active = health & can::kHbHealthBitEstopActive;
        message.mode_auto = health & can::kHbHealthBitModeAuto;
        message.can_ok = health & can::kHbHealthBitCanOk;
        message.task_safety_ok = health & 0x10;
        message.task_brake_ok = health & 0x20;
        message.task_dispatch_ok = health & 0x40;
        message.task_can_tx_ok = health & 0x80;
        can::Frame fr;
        if (can::encode_frame(message, fr) == can::gen::CodecStatus::Ok) send_can(fr);

        vTaskDelayUntil(&last, period);
    }
}

// ── Task handles ────────────────────────────────────────────────────

static TaskHandle_t h_can_rx, h_safety, h_dispatch, h_mode;
static TaskHandle_t h_gear, h_brake, h_lights;
static TaskHandle_t h_indicator, h_power, h_can_tx, h_diag, h_hb;

// Put every connected SYS GPIO in a deterministic, non-actuating state before
// starting CAN or tasks. GPIO reset defaults leave button inputs floating.
static void init_board_gpio() {
    constexpr uint64_t kOutputPins = (1ULL << sys::kLightLeftTurn)
                                  | (1ULL << sys::kLightRightTurn)
                                  | (1ULL << sys::kLightBrake)
                                  | (1ULL << sys::kLightHead)
                                  | (1ULL << sys::kBulbAuto)
                                  | (1ULL << sys::kBulbManual)
                                  | (1ULL << sys::kBulbReady)
                                  | (1ULL << sys::kBulbEstop)
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
    for (int pin : {sys::kLightLeftTurn, sys::kLightRightTurn, sys::kLightBrake,
                    sys::kLightHead, sys::kBulbAuto, sys::kBulbManual,
                    sys::kBulbReady, sys::kBulbEstop, sys::kPower12vRelay/*,
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
    estop.pull_up_en = GPIO_PULLUP_DISABLE;
    estop.pull_down_en = GPIO_PULLDOWN_ENABLE;
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
    if (SYSTEM_RUN_MODE == 2) {
        ESP_LOGE(TAG, "***********************************");
        ESP_LOGE(TAG, "* PURE SOFTWARE SIMULATION MODE   *");
        ESP_LOGE(TAG, "* BYPASSING SAFETY SYNC CHECKS!   *");
        ESP_LOGE(TAG, "***********************************");
        g_bench_solo_mode = true;
        g_bypass_eps_sync = true;
        g_bypass_seb_sync = true;
        g_bypass_mtr_absent = true;
    } else if (SYSTEM_RUN_MODE == 1) {
        gpio_set_direction(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN), GPIO_MODE_INPUT);
        gpio_pullup_en(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN));
        
        // Brief delay to let pull-up stabilize
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (gpio_get_level(static_cast<gpio_num_t>(DEVELOPER_OVERRIDE_PIN)) == 0) {
            ESP_LOGE(TAG, "***********************************");
            ESP_LOGE(TAG, "* HARDWARE OVERRIDE PIN DETECTED! *");
            ESP_LOGE(TAG, "* BYPASSING SAFETY SYNC CHECKS!   *");
            ESP_LOGE(TAG, "***********************************");
            g_bench_solo_mode = true;
            g_bypass_eps_sync = true;
            g_bypass_seb_sync = true;
            g_bypass_mtr_absent = true;
        } else {
            ESP_LOGI(TAG, "Prototype mode: Override pin not jumped. Enforcing safety.");
        }
    } else {
        ESP_LOGI(TAG, "Production mode: Safety checks enforced.");
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

    // Init status bulbs (green=ready, red=ESTOP) — start both OFF
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbReady), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(sys::kBulbReady), 0);
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbEstop), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(sys::kBulbEstop), 0);


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
    xTaskCreate(task_diag,      "diag",      3584, nullptr, 1, &h_diag);
    xTaskCreate(task_hb,        "hb",        2560, nullptr, 1, &h_hb);

    ESP_LOGI(TAG, "Ready — 12 tasks running (vehicle, MTR owns motor). Mode=%s", g_mode_mgr.name());
    vTaskDelete(nullptr);
}
