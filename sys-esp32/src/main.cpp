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
#include "sys_status_led.h"
#include "system_ready.h"
#include "node_status.h"
#include "estop_status.h"
// #include "wdt_toggle.h"


static const char* TAG = "sys";

static shared::led::Ws2812Strip g_status_led;


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

// ── Relay/lamp output helper ────────────────────────────────────────
// Single point of truth for output polarity. The vehicle has no ULN2803A and
// uses an active-LOW relay module wired directly to the GPIO, so a LOW pin
// energizes the relay (lamp ON). kRelayOutputActiveLow inverts the positive
// logic used throughout the tasks.
static inline void set_relay(int pin, bool on) {
    const int level = (on ^ sys::kRelayOutputActiveLow) ? 1 : 0;
    gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

static bool send_estop_frame(const char* caller) {
    can::Frame frame;
    can::gen::SafetyEstop message{};
    if (can::gen::encode_safety_estop(message, frame) != can::gen::CodecStatus::Ok)
        return false;
    sys::mark_estop_broadcast(static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount())));
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

// Peer node-status (observational READY inputs): 0x501 RT / 0x502 MTR.
static std::atomic<bool>     g_rt_node_ready{false};
static std::atomic<bool>     g_rt_node_degraded{false};
static std::atomic<uint8_t>  g_rt_node_presence{0};  // RT's node_presence (SES source)
static std::atomic<uint32_t> g_last_rt_node_tick{0};
static std::atomic<bool>     g_mtr_node_ready{false};
static std::atomic<bool>     g_mtr_node_degraded{false};
static std::atomic<uint32_t> g_last_mtr_node_tick{0};
// Debounced system READY level (see system_ready.h); observational only.
static std::atomic<uint8_t>  g_sys_ready_level{
    static_cast<uint8_t>(sys::SystemReadyLevel::Blocked)};

// ── 0x500 cockpit I/O telemetry (observational) ─────────────────────
// Raw physical inputs (as seen at the GPIOs, before any authority resolution)
// and the FINAL executed lamp/relay outputs (whoever commanded them — manual
// handlebar switches or AUTO/0x302). Written by the owning tasks.
static std::atomic<bool> g_hw_estop_pressed{false};      // task_safety (GPIO1)
static std::atomic<bool> g_hw_start_latched{false};      // task_mode   (GPIO41)
static std::atomic<bool> g_hw_brake_lever{false};        // task_safety (GPIO2)
static std::atomic<bool> g_hw_mode_btn{false};           // task_mode   (GPIO11)
static std::atomic<bool> g_hw_sw_left{false};            // task_lights (GPIO9)
static std::atomic<bool> g_hw_sw_right{false};           // task_lights (GPIO6)
static std::atomic<bool> g_hw_sw_head{false};            // task_lights (GPIO7)
static std::atomic<bool> g_out_power12v{false};          // task_indicator (GPIO40)
static std::atomic<bool> g_out_ready_bulb{false};        // task_indicator (GPIO17)
static std::atomic<bool> g_out_bypass_bulb{false};       // task_indicator (GPIO14)
static std::atomic<bool> g_out_estop_bulb{false};        // task_indicator (GPIO18)

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

// ── ESTOP trigger timestamp & MTR ACK tracking ──────────────────────
#include "mtr_estop_ack.h"
static std::atomic<uint32_t> g_last_estop_trigger_tick{0};
static sys::MtrEstopAckWatchdog g_mtr_ack_watchdog;

// 0x011 ESTOP source/reason (SYS catalog, config.h). Latched with the mode; the
// values are only reported while sys_estop_latched() is true, so a validated
// reset implicitly clears them on the wire.
static std::atomic<uint8_t> g_estop_source{0};
static std::atomic<uint8_t> g_estop_reason{0};

// Authoritative ESTOP entry helper.
static void enter_estop(uint8_t reason, uint8_t source, const char* note = nullptr) {
    const bool was_estop = (g_mode_mgr.mode() == can::Mode::Estop);
    if (!was_estop) {
        g_estop_reason.store(reason, std::memory_order_relaxed);
        g_estop_source.store(source, std::memory_order_relaxed);
        g_mode_mgr.force_estop();
        const uint32_t now_ms = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
        g_last_estop_trigger_tick.store(now_ms, std::memory_order_relaxed);
        g_mtr_ack_watchdog.trigger(now_ms, g_motor_fault_flags.load(std::memory_order_relaxed));
        ESP_LOGE(TAG, "ESTOP entered [edge]: %s%s%s", sys::estop_reason_name(reason),
                 note ? " — " : "", note ? note : "");
    }
    // Broadcast 0x001 on CAN (rate-limited by can_send_estop)
    if (can_send_estop()) {
        send_estop_frame(note ? note : sys::estop_reason_name(reason));
    }
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

// ── System READY level (OBSERVATIONAL ONLY) ─────────────────────────────
// Gathers live evidence for system_ready.h. It must never be wired into
// resolve_authority(), inhibit_state, ESTOP, or any TX path — it exists only to
// drive the green lamp, the WS2812 cadence, and 0x500.ready.
static sys::SystemReadyInputs sys_system_ready_inputs() {
    const TickType_t now = xTaskGetTickCount();
    auto fresh = [now](const std::atomic<uint32_t>& t) {
        const uint32_t last = t.load(std::memory_order_relaxed);
        return last != 0 && (now - last) <= pdMS_TO_TICKS(sys::kNodeStatusFreshMs);
    };

    sys::SystemReadyInputs in;
    in.estop_or_inhibit = sys_estop_latched() || sys::any_inhibit();

    // RT is the command conduit (heartbeat 0x7FD + node status 0x501, which
    // folds in SES/steering health via RT's ready/degraded bits).
    in.rt_ok = g_safety.heartbeat_ok()
            && fresh(g_last_rt_node_tick)
            && g_rt_node_ready.load(std::memory_order_relaxed)
            && !g_rt_node_degraded.load(std::memory_order_relaxed);

    // MTR node status 0x502 proves ignition + output.
    in.mtr_ok = fresh(g_last_mtr_node_tick)
             && g_mtr_node_ready.load(std::memory_order_relaxed)
             && !g_mtr_node_degraded.load(std::memory_order_relaxed);

    // SEB has no node-status frame: infer from 0x721 freshness + rolling + L3.
    in.seb_ok = g_seb_seen.load(std::memory_order_relaxed)
             && fresh(g_last_seb_status_tick)
             && g_seb_rolling.load(std::memory_order_relaxed)
             && g_seb_error_status.load(std::memory_order_relaxed) < 3;

    in.host_ok = g_mode_request_valid.load(std::memory_order_relaxed)
              && g_power_request_valid.load(std::memory_order_relaxed);

    in.mtr_required = !g_bypass_mtr_absent;
    in.seb_required = !g_bypass_seb_sync;
    return in;
}

static sys::SystemReadyLevel sys_system_ready_level() {
    return sys::evaluate_system_ready(sys_system_ready_inputs());
}

// ── 0x011 node_presence (observational) ─────────────────────────────────
// SYS's view of which nodes are live. SES is taken from RT 0x501 (SYS has no
// direct SES_STATUS receiver); every other bit is SYS's own observation.
static uint8_t sys_node_presence() {
    using namespace shared;
    const TickType_t now = xTaskGetTickCount();
    auto fresh = [now](const std::atomic<uint32_t>& t) {
        const uint32_t last = t.load(std::memory_order_relaxed);
        return last != 0 && (now - last) <= pdMS_TO_TICKS(kNodePresenceFreshMs);
    };
    uint8_t p = kNodePresenceSys;
    if (g_safety.heartbeat_ok())                                   p |= kNodePresenceRt;
    if (fresh(g_last_mtr_fbk_tick) || fresh(g_last_mtr_node_tick)) p |= kNodePresenceMtr;
    if (fresh(g_last_seb_status_tick))                             p |= kNodePresenceSeb;
    if (g_rt_node_presence.load(std::memory_order_relaxed) & kNodePresenceSes)
        p |= kNodePresenceSes;
    if (g_mode_request_valid.load(std::memory_order_relaxed)
        && g_power_request_valid.load(std::memory_order_relaxed))
        p |= kNodePresenceHost;
    return p;
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
                /*mtr_ack_confirmed=*/(g_bypass_mtr_absent || g_mtr_ack_watchdog.has_acknowledged()),
                /*token=*/static_cast<uint16_t>(req.reset_token)
            );

            if (!fresh) {
                blockers |= sys::kResetBlockInvalidToken;
            }

            bool reset_ok = false;
            if (blockers == 0) {
                reset_ok = g_mode_mgr.try_exit_estop_remote(blockers);
                if (reset_ok) {
                    sys::mark_estop_reset(static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount())));
                    g_mtr_ack_watchdog.reset();
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
            // Guarded with rx_estop_suppressed() so that during an operator reset out of ESTOP,
            // the lingering acknowledgment from MTR does not immediately re-trip SYS into ESTOP.
            const uint32_t now_ms = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
            if ((fbk.fault_flags & shared::kMtrFaultEstopActive)
                && g_mode_mgr.mode() != can::Mode::Estop
                && !sys::rx_estop_suppressed(now_ms)) {
                ESP_LOGW(TAG, "MTR reports ESTOP_ACTIVE in 0x206 fault_flags — propagating");
                enter_estop(sys::kEstopReasonMtrFault, sys::kEstopSourcePeerFault,
                            "MTR ESTOP_ACTIVE propagated");
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
            enter_estop(sys::kEstopReasonCan001, sys::kEstopSourceCan001, "CAN 0x001");
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
                    ESP_LOGE(TAG, "SEB error_status L3 in 0x721 (status=0x%02x) — latching brake fault", value.status_byte);
                    sys::set_latched_fault(sys::kLatchedSebL3);
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
            // SEB health and actuation are supervised via hardware rolling counter freshness
            // (g_seb_rolling / g_last_seb_roll_change_tick), status arrival watchdog, and
            // SEB L3 hardware fault decoding below. Software stroke following-error tracking
            // is omitted to prevent false traction inhibits and false latched ESTOPs during normal
            // hydraulic fluid transit and operator lever actuation.
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
                sys::set_latched_fault(sys::kLatchedSebL3);
                g_seb_error_status.store(3, std::memory_order_relaxed);
                ESP_LOGW(TAG, "Brake fault latched by SEB 0x731 L3 fault(s)");
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
        case can::gen::RtNodeStatus::kId: {  // 0x501 — observational READY input
            can::gen::RtNodeStatus st{};
            if (can::gen::decode_rt_node_status(fr.view(), st) != can::gen::CodecStatus::Ok) break;
            g_rt_node_ready.store(st.ready, std::memory_order_relaxed);
            g_rt_node_degraded.store(st.degraded, std::memory_order_relaxed);
            g_rt_node_presence.store(st.node_presence, std::memory_order_relaxed);
            g_last_rt_node_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
            break;
        }
        case can::gen::MtrNodeStatus::kId: {  // 0x502 — observational READY input
            can::gen::MtrNodeStatus st{};
            if (can::gen::decode_mtr_node_status(fr.view(), st) != can::gen::CodecStatus::Ok) break;
            g_mtr_node_ready.store(st.ready, std::memory_order_relaxed);
            g_mtr_node_degraded.store(st.degraded, std::memory_order_relaxed);
            g_last_mtr_node_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
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
        bool estop_hw = (gpio_get_level(static_cast<gpio_num_t>(sys::kEstopGpio))
                         == (sys::kEstopActiveHigh ? 1 : 0));
        bool brake_lever = (gpio_get_level(static_cast<gpio_num_t>(sys::kBrakeLeverGpio)) == 0);
#endif

        g_safety.set_estop(estop_hw);
        g_safety.set_brake_lever(brake_lever);
        // 0x500 cockpit I/O telemetry: raw physical switch states.
        g_hw_estop_pressed.store(estop_hw, std::memory_order_relaxed);
        g_hw_brake_lever.store(brake_lever, std::memory_order_relaxed);

        // Driver brake takeover: pulling the lever in AUTO transitions to MANUAL immediately
        // and zeros motor propulsion setpoints so the motor never drives against the brakes.
        if (g_mode_mgr.handle_driver_brake_takeover(brake_lever)) {
            ESP_LOGW(TAG, "Driver brake lever takeover: transitioning AUTO -> MANUAL");
            g_setpoint_speed_mmps.store(0, std::memory_order_relaxed);
            g_setpoint_gear.store(0, std::memory_order_relaxed);
        }

        // Developer bypass suppresses only missing-dependency faults. The
        // physical ESTOP remains unbypassable.
        if (g_safety.estop_active()) {
            enter_estop(sys::kEstopReasonHwButton, sys::kEstopSourceLocalSys, "Hardware ESTOP button");
        } else if (!g_bench_solo_mode && !g_safety.heartbeat_ok()) {
            enter_estop(sys::kEstopReasonRtHbLost, sys::kEstopSourceLocalSys, "RT heartbeat loss");
        }

        // Toggle external watchdog + per-task alive counter
        g_alive_safety.store(xTaskGetTickCount(), std::memory_order_relaxed);
        // g_wdt.tick();  // GPIO23 toggle

        // 0x204 staleness watchdog (arch §8.6: >200ms without RT_DRIVE_CMD ->
        // zero speed + neutral). g_last_setpoint_tick is stamped on every 0x204
        // receive (main.cpp:293) but was previously written and never enforced.
        // Now actively fail-safe: a stale/absent RT_DRIVE_CMD cannot keep a last
        // non-zero throttle applied.
        {
            static constexpr TickType_t kSetpointStaleTicks = pdMS_TO_TICKS(200);
            const TickType_t since_setpoint =
                xTaskGetTickCount() - g_last_setpoint_tick.load(std::memory_order_relaxed);
            if (since_setpoint > kSetpointStaleTicks) {
                g_setpoint_speed_mmps.store(0, std::memory_order_relaxed);
                g_setpoint_gear.store(static_cast<uint8_t>(can::Gear::N), std::memory_order_relaxed);
            }
        }

        // F3: MTR ESTOP ACK non-blocking supervision (BUG-03)
        // If in ESTOP on a vehicle build, monitor if MTR confirmed ESTOP_ACTIVE in 0x206.
        // Never force ESTOP or flood CAN on retry; log rate-limited warning if unconfirmed > 500ms.
        if (!g_bypass_mtr_absent && g_mode_mgr.mode() == can::Mode::Estop) {
            if (!g_mtr_ack_watchdog.has_acknowledged()) {
                uint32_t estop_start = g_last_estop_trigger_tick.load(std::memory_order_relaxed);
                uint32_t now_ms = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
                if (estop_start > 0 && (now_ms - estop_start) >= 500) {
                    static TickType_t last_ack_warn = 0;
                    if (last_ack_warn == 0 || (xTaskGetTickCount() - last_ack_warn) >= pdMS_TO_TICKS(5000)) {
                        ESP_LOGW(TAG, "MTR ESTOP ACK unconfirmed (waiting for 0x206)");
                        last_ack_warn = xTaskGetTickCount();
                    }
                }
            }
        }

        // F4: 0x206 staleness check (Gap #15)
        // 0x206 is setpoint-echo telemetry without physical speed sensing;
        // traction loss / runaway supervision will be handled when encoders are fitted.
        // RT and SYS do not cut traction power authority or inhibit AUTO on 0x206 timeout.
        if (!g_bypass_mtr_absent) {
            uint32_t last_fbk = g_last_mtr_fbk_tick.load(std::memory_order_relaxed);
            bool stale = last_fbk > 0
                && (xTaskGetTickCount() - last_fbk) >= pdMS_TO_TICKS(sys::kMtrFbkStaleMs);
            if (stale) {
                static TickType_t last_warn = 0;
                if (last_warn == 0 || (xTaskGetTickCount() - last_warn) >= pdMS_TO_TICKS(5000)) {
                    ESP_LOGW(TAG, "0x206 MTR_MOTOR_FBK stale (>%d ms)", sys::kMtrFbkStaleMs);
                    last_warn = xTaskGetTickCount();
                }
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
        // START is a latching NC button to GND (pulled up): idle = LOW,
        // latched/pressed = open = HIGH.
        bool start_btn = (gpio_get_level(static_cast<gpio_num_t>(sys::kStartBtnGpio))
                          == (sys::kStartPressedHigh ? 1 : 0));
#endif

        bool changed = g_mode_mgr.tick(mode_btn, start_btn, g_safety.estop_active());
        // 0x500 cockpit I/O telemetry: raw buttons + resolved run latch.
        g_hw_mode_btn.store(mode_btn, std::memory_order_relaxed);
        g_hw_start_latched.store(start_btn, std::memory_order_relaxed);
        // START run/enable latch: on release (unlatch) immediately zero the
        // motion setpoints. resolve_authority() below drops 0x113 power and
        // clamps 0x110 to MANUAL; this is a stop, never an ESTOP.
        {
            static bool was_run_enabled = false;
            const bool run_now = g_mode_mgr.run_enabled();
            if (was_run_enabled && !run_now) {
                g_setpoint_speed_mmps.store(0, std::memory_order_relaxed);
                g_setpoint_gear.store(0, std::memory_order_relaxed);
                ESP_LOGW(TAG, "START released — run disabled (motion inhibited)");
            }
            was_run_enabled = run_now;
        }
        // Issue #4 / gap #14: an operator reset (MODE long-press) carries the
        // system out of ESTOP. Open the reset-grace window so a 0x001 still in
        // flight on the bus cannot instantly re-latch ESTOP (livelock). The
        // harness shares this exact policy via sys::rx_estop_suppressed.
        if (mode_was_estop && g_mode_mgr.mode() != can::Mode::Estop) {
            sys::mark_estop_reset(static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount())));
            g_mtr_ack_watchdog.reset();
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
                enter_estop(sys::kEstopReasonEgasFault, sys::kEstopSourceLocalSys, "Physical EGAS trip");
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

            const auto auth = sys::resolve_authority(mode_is_estop, resolved_auto, power_req,
                                                      g_mode_mgr.run_enabled());

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
            // Request assisted stopping pressure rather than instant 5000 kPa mechanical clamp.
            brake_kpa = shared::kAssistStopKpa;
        }

        can::custom::seb::Command seb_cmd;
        uint8_t  seb_b0 = g_seb_status_byte0.load(std::memory_order_relaxed);
        uint16_t seb_stroke = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
        if ((g_bench_solo_mode || g_bypass_seb_sync) && seb_b0 == 0xFF) {
            // Bench/sim without an SEB node: assume an aligned, released actuator
            // so BrakeControl skips LISTEN_SYNC->DEGRADED and applies the 0x205
            // intent through the normal ACTIVE path. This is an internal bench
            // assumption (SYSTEM_RUN_MODE=1) only — no frame is put on the bus,
            // mirroring the steering bench bypass (steering_control.h listen-sync).
            seb_b0 = 0x01;   // alignment bit -> BrakeControl ACTIVE
            seb_stroke = 0;
        }
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
                static int seb_comms_recover_count = 0;
                if (stale) {
                    sys::set_inhibit(sys::kInhibitSebCommsLoss);
                    seb_comms_recover_count = 0;
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
        // 0x500 cockpit I/O telemetry: raw handlebar switch states.
        g_hw_sw_left.store(sw_L, std::memory_order_relaxed);
        g_hw_sw_right.store(sw_R, std::memory_order_relaxed);
        g_hw_sw_head.store(sw_H, std::memory_order_relaxed);

        // Brake light OR-logic (§8.6): add SEB stroke check — if SEB is actually
        // braking (stroke > 0.5mm ≈ raw 610), light the brake lamp.
        uint16_t seb_raw = g_seb_actual_stroke_raw.load(std::memory_order_relaxed);
        bool seb_braking = (seb_raw > 610);  // 610 raw ≈ 0.5mm
        auto out = g_lights.tick(mode, lever, bits, sw_L, sw_R, sw_H, seb_braking);
#ifndef TESTING
        set_relay(sys::kLightBrake, out.brake_lamp);
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
    TickType_t period = pdMS_TO_TICKS(40);  // 25 Hz (was 5 Hz, renders smooth breathing and pips)
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        [[maybe_unused]] auto out = g_indicator.tick(g_mode_mgr.mode());
#ifndef TESTING
        set_relay(sys::kBulbAuto, out.auto_bulb);
        set_relay(sys::kBulbManual, out.manual_bulb);
#endif

        // ── Observational system READY ──────────────────────────────────
        // Green lamp is ON whenever the command path is up (any non-Blocked
        // level) and OFF on a real fault. Only the WS2812 encodes the level
        // cadence (Full=solid, Host-absent=breathe, MTR-absent=fast blink).
        // This NEVER affects authority, ESTOP, or any outgoing command.
        can::Mode mode = g_mode_mgr.mode();
        const uint32_t now_ms = static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
        {
            const sys::SystemReadyLevel raw = sys_system_ready_level();
            static sys::SystemReadyLevel seen    = raw;
            static sys::SystemReadyLevel held    = raw;
            static uint32_t              seen_at = now_ms;
            if (raw != seen) { seen = raw; seen_at = now_ms; }
            if (seen != held
                && (now_ms - seen_at) >= static_cast<uint32_t>(sys::kSystemReadyHoldMs)) {
                held = seen;
            }
            g_sys_ready_level.store(static_cast<uint8_t>(held), std::memory_order_relaxed);
        }
        [[maybe_unused]] bool ready =
            g_sys_ready_level.load(std::memory_order_relaxed)
                != static_cast<uint8_t>(sys::SystemReadyLevel::Blocked);
        // Red "ESTOP" bulb: dedicated, independent of brake lamp
        [[maybe_unused]] bool estop = (mode == can::Mode::Estop);
        // 0x500 board output telemetry: FINAL executed bulb/relay states.
        g_out_ready_bulb.store(ready, std::memory_order_relaxed);
        g_out_estop_bulb.store(estop, std::memory_order_relaxed);
        g_out_bypass_bulb.store(g_bench_solo_mode, std::memory_order_relaxed);
        g_out_power12v.store(true, std::memory_order_relaxed);

#ifndef TESTING
        set_relay(sys::kBulbReady, ready);
        set_relay(sys::kBulbEstop, estop);
        set_relay(sys::kBulbBypass, g_bench_solo_mode);
        // Keep 12V accessory relay energized even during ESTOP so indicator bulbs remain visible
        set_relay(sys::kPower12vRelay, true);
#endif

        // Status LED Evaluation (25 Hz)
        {
            sys::SysLedInputs led_in{};
            led_in.system_ready_level   = static_cast<sys::SystemReadyLevel>(
                g_sys_ready_level.load(std::memory_order_relaxed));
            led_in.twai_bus_off         = g_can.recovery_needed();
            led_in.estop_active         = sys_estop_latched() || (mode == can::Mode::Estop);
            led_in.local_estop_cause    = g_safety.estop_active() || (sys::g_latched_fault_reasons.load(std::memory_order_relaxed) != 0);
            led_in.mtr_ack_retrying     = g_mtr_ack_watchdog.is_pending();
            led_in.actuator_fault       = (sys::g_latched_fault_reasons.load(std::memory_order_relaxed) != 0)
                                       || g_mtr_ack_watchdog.has_latched_fault();
            led_in.actuator_inhibit     = sys::any_inhibit();

            led_in.rt_hb_ok             = g_bench_solo_mode || g_safety.heartbeat_ok();
            led_in.rt_cmd_stale         = (g_setpoint_speed_mmps.load(std::memory_order_relaxed) == 0 && sys::any_inhibit());
            led_in.boot_grace_active    = (now_ms < shared::kStartupGracePeriodMs);
            led_in.mode_auto            = (mode == can::Mode::Auto);
            led_in.drive_cmd_nonzero    = (g_setpoint_speed_mmps.load(std::memory_order_relaxed) != 0);
            led_in.manual_active_input  = g_safety.brake_lever_pressed()
                                       || (g_motor_command_speed_mmps.load(std::memory_order_relaxed) != 0);
            led_in.brake_lever_override = (mode == can::Mode::Auto && g_safety.brake_lever_pressed());

            const auto pat = sys::evaluate_sys_led(led_in);
            const auto rgb = shared::led::render(pat, now_ms);
            g_status_led.set(rgb);
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── CAN TX task (prio 2, 5 Hz) — 0x011 SYS_SAFETY_STS ──────────────

// ── 0x500 SYS_NODE_STATUS (observational, issue-added NODE_STATUS) ──

// ── 0x500 SYS_NODE_STATUS (observational, issue-added NODE_STATUS) ──
// Strictly observational: never changes mode/authority. Reports SYS's own
// ── 0x500 SYS_NODE_STATUS — blocker bitmask (observational) ─────────
// Byte 2 (block_mask_low)  = transient/recoverable conditions.
// Byte 3 (block_mask_high) = latched faults (cleared only by the reset path).
// Bit values live in node_status.h so tests share the shipped vocabulary.
static constexpr uint32_t kBlkSetpointStaleMs = 200;  // matches task_safety enforcement

// Safety-critical task bits inside g_task_health_bits (task_diag); a miss on
// any of these latches ESTOP after 2 consecutive 1 Hz cycles.
static constexpr uint8_t kCriticalTaskMask =
    0x01 /*safety*/ | 0x02 /*brake*/ | 0x04 /*dispatch*/
    | 0x08 /*can_tx*/ | 0x40 /*mode*/;

static uint16_t sys_block_mask() {
    const TickType_t now = xTaskGetTickCount();
    auto stale = [now](const std::atomic<uint32_t>& t, uint32_t window_ms) {
        const uint32_t last = t.load(std::memory_order_relaxed);
        return last == 0 || (now - last) > pdMS_TO_TICKS(window_ms);
    };

    uint8_t low = 0, high = 0;
    if (g_safety.brake_lever_pressed())                       low  |= sys::kBlkBrakeLever;
    if (!g_seb_seen.load(std::memory_order_relaxed))          low  |= sys::kBlkSebSyncing;
    if (g_mtr_ack_watchdog.is_pending())                      low  |= sys::kBlkMtrFbkUnacked;
    if (stale(g_last_setpoint_tick, kBlkSetpointStaleMs))     low  |= sys::kBlkRtSetpointStale;
    if (!g_mode_mgr.run_enabled())                            low  |= sys::kBlkStartUnlatched;
    if (now < pdMS_TO_TICKS(sys::kSebStartupAcquireMs))       low  |= sys::kBlkStartupAcquire;

    const uint32_t latched = sys::g_latched_fault_reasons.load(std::memory_order_relaxed);
    if (latched & sys::kLatchedSebL3)                         high |= sys::kBlkSebL3;
    // kBlkEgasMismatch: reserved — the physical/command-path EGAS detector is
    // compiled out on the current encoder-less vehicle (config.h).
    if (stale(g_last_mtr_fbk_tick, sys::kMtrFbkStaleMs)
        && !g_bypass_mtr_absent)                              high |= sys::kBlkMtrFbkTimeout;
    if ((g_task_health_bits.load(std::memory_order_relaxed)
         & kCriticalTaskMask) != kCriticalTaskMask)           high |= sys::kBlkTaskDeadline;
    return static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
}

// ── 0x500 SYS_NODE_STATUS — Vehicle Operational & I/O Status ─────────
// Strictly observational: never changes mode/authority. Command-execution
// outcome, system readiness, developer-bypass flags, the blocker bitmask, raw
// cockpit hardware inputs and the FINAL executed relay/lamp outputs.
static can::gen::SysNodeStatus build_sys_node_status() {
    can::gen::SysNodeStatus ns{};
    const can::Mode m = g_mode_mgr.mode();
    const bool estop = sys_estop_latched();
    const bool nonzero = g_setpoint_speed_mmps.load(std::memory_order_relaxed) != 0;

    // Byte 0 — command execution
    ns.command_received = g_last_setpoint_tick.load(std::memory_order_relaxed) != 0
        && (xTaskGetTickCount() - g_last_setpoint_tick.load(std::memory_order_relaxed))
               <= pdMS_TO_TICKS(kBlkSetpointStaleMs);
    ns.command_nonzero = nonzero;
    ns.command_executing = nonzero && !estop && !sys::any_inhibit()
                        && g_mode_mgr.run_enabled() && (m == can::Mode::Auto);
    ns.command_rejected = nonzero && !ns.command_executing;
    ns.driver_override = g_safety.brake_lever_pressed();

    // Byte 1 — readiness & developer overrides
    const uint8_t ready_level = g_sys_ready_level.load(std::memory_order_relaxed);
    ns.system_ready = (ready_level == static_cast<uint8_t>(sys::SystemReadyLevel::Full));
    ns.bypass_active = g_bench_solo_mode;
    ns.bench_solo_mode = g_bench_solo_mode;
    ns.bypass_mtr_absent = g_bypass_mtr_absent;
    ns.bypass_seb_sync = g_bypass_seb_sync;
    ns.degraded = (ready_level == static_cast<uint8_t>(sys::SystemReadyLevel::MtrAbsent))
               || (ready_level == static_cast<uint8_t>(sys::SystemReadyLevel::HostAbsent))
               || g_brake_fault_active.load(std::memory_order_relaxed)
               || sys::any_inhibit();

    // Bytes 2-3 — blocker bitmask
    const uint16_t blockers = sys_block_mask();
    ns.block_mask_low = static_cast<uint8_t>(blockers & 0xFFu);
    ns.block_mask_high = static_cast<uint8_t>((blockers >> 8) & 0xFFu);

    // Byte 4 — raw cockpit hardware inputs
    ns.hw_estop_btn_pressed  = g_hw_estop_pressed.load(std::memory_order_relaxed);
    ns.hw_start_btn_latched  = g_hw_start_latched.load(std::memory_order_relaxed);
    ns.hw_brake_lever_pulled = g_hw_brake_lever.load(std::memory_order_relaxed);
    ns.hw_mode_btn_pressed   = g_hw_mode_btn.load(std::memory_order_relaxed);
    ns.hw_sw_left_turn       = g_hw_sw_left.load(std::memory_order_relaxed);
    ns.hw_sw_right_turn      = g_hw_sw_right.load(std::memory_order_relaxed);
    ns.hw_sw_headlight       = g_hw_sw_head.load(std::memory_order_relaxed);
    ns.run_latch_enabled     = g_mode_mgr.run_enabled();

    // Byte 5 — FINAL executed relay/lamp outputs (manual or AUTO commanded)
    const uint8_t lights = g_light_state.load(std::memory_order_relaxed);
    ns.power_12v_relay_on = g_out_power12v.load(std::memory_order_relaxed);
    ns.ready_bulb_on      = g_out_ready_bulb.load(std::memory_order_relaxed);
    ns.bypass_bulb_on     = g_out_bypass_bulb.load(std::memory_order_relaxed);
    ns.estop_bulb_on      = g_out_estop_bulb.load(std::memory_order_relaxed);
    ns.light_left_on      = (lights & 0x01u) != 0;
    ns.light_right_on     = (lights & 0x02u) != 0;
    ns.light_brake_on     = (lights & 0x04u) != 0;
    ns.light_head_on      = (lights & 0x08u) != 0;
    return ns;
}

[[noreturn]] static void task_can_tx(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz (SYS_SAFETY_STS cycle)
    TickType_t last   = xTaskGetTickCount();
    static uint8_t safety_roll = 0;
    while (1) {
        g_alive_can_tx.store(xTaskGetTickCount(), std::memory_order_relaxed);
        can::Frame fr;
        can::gen::SysSafetySts message{};
        // ESTOP authority is reported as a latched safety state, separate from
        // 0x110 SYS_MODE_CMD (clamped to MANUAL/AUTO). estop_source/estop_reason
        // identify WHO/WHY; both are reported only while the ESTOP is latched, so
        // a validated REARM implicitly returns them to NONE on the wire.
        const bool estop = sys_estop_latched();
        message.estop_source    = estop ? g_estop_source.load(std::memory_order_relaxed) : 0u;
        message.estop_reason    = estop ? g_estop_reason.load(std::memory_order_relaxed) : 0u;
        message.node_presence   = sys_node_presence();
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

        // ── 0x500 SYS_NODE_STATUS (same 5 Hz cadence, no CRC/counter) ──
        can::gen::SysNodeStatus ns = build_sys_node_status();
        if (can::gen::encode_sys_node_status(ns, fr) == can::gen::CodecStatus::Ok)
            send_can(fr, "node-status");

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
        static int critical_miss_count = 0;
        if ((task_health & kCriticalTaskMask) != kCriticalTaskMask) {
            if (++critical_miss_count >= 2) {   // persistent >= 2 s miss
                ESP_LOGE(TAG, "SYS critical task(s) dead (mask=0x%X need 0x%02X) — "
                              "forcing ESTOP", task_health, kCriticalTaskMask);
                enter_estop(sys::kEstopReasonTaskDeadline, sys::kEstopSourceLocalSys, "Critical task dead");
            }
        } else {
            critical_miss_count = 0;
        }

        // ── 0x600 SYS_DIAG_RPT — pure ECU/bus health (1 Hz) ────────
        uint8_t tec = 0, rec = 0;
        g_can.get_error_counters(tec, rec);

        can::gen::SysDiagRpt rpt;
        // Byte 0: saturated RX-overflow counter + CAN controller state.
        const auto can_health = g_can.health_snapshot();
        {
            uint32_t ov = g_can_rx_overflow.load(std::memory_order_relaxed);
            rpt.rx_overflow = ov > 63 ? 63 : static_cast<uint8_t>(ov);
        }
        // HealthState: Active=0, Warning=1, Passive=2, BusOff=3. A bus-off
        // controller is reported as RECOVERING (service_recovery drives it back).
        rpt.can_state = static_cast<uint8_t>(can_health.state);
        rpt.tec = tec;
        rpt.rec = rec;
        rpt.task_health_mask = g_task_health_bits.load(std::memory_order_relaxed);
        rpt.free_heap_kb = static_cast<uint8_t>(
            (esp_get_free_heap_size() / 1024) > 255 ? 255 : (esp_get_free_heap_size() / 1024));
        // Boot forensics: map esp_reset_reason_t onto the wire enum.
        {
            const esp_reset_reason_t rst = esp_reset_reason();
            uint8_t mapped;
            switch (rst) {
                case ESP_RST_POWERON:  mapped = 0; break;  // POWER_ON
                case ESP_RST_SW:       mapped = 1; break;  // SW_RESET
                case ESP_RST_TASK_WDT: mapped = 2; break;  // TASK_WDT
                case ESP_RST_WDT:      mapped = 2; break;  // (RTC WDT family)
                case ESP_RST_BROWNOUT: mapped = 3; break;  // BROWNOUT
                case ESP_RST_PANIC:    mapped = 4; break;  // PANIC
                default:               mapped = 5; break;  // UNKNOWN
            }
            rpt.mcu_reset_reason = mapped;
        }
        rpt.uptime_seconds = static_cast<uint16_t>(esp_timer_get_time() / 1000000ULL);
        can::Frame fr;
        if (can::gen::encode_sys_diag_rpt(rpt, fr) == can::gen::CodecStatus::Ok) send_can(fr);

        // CAN bus-off monitoring is state-driven. TEC/REC are telemetry only.
        if (can_health.state == can::CanDriver::HealthState::Passive)
            ESP_LOGW(TAG, "CAN error-passive: TEC=%u REC=%u", tec, rec);
        if (can_health.state == can::CanDriver::HealthState::BusOff) {
            ESP_LOGE(TAG, "CAN bus-off: TEC=%u REC=%u", tec, rec);
            bus_off_count++;
            if (bus_off_count >= 5) {
                ESP_LOGE(TAG, "CAN bus-off persistent — forcing ESTOP");
                enter_estop(sys::kEstopReasonCanBusoff, sys::kEstopSourceLocalSys, "CAN bus-off persistent");
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
static TaskHandle_t h_indicator, h_can_tx, h_can_control, h_diag, h_hb;

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

    // Latch every output to its OFF level before enabling the drivers so no
    // relay/lamp receives an indeterminate boot pulse. With an active-LOW
    // relay module, "OFF" is a HIGH pin.
    constexpr int kOutputOffLevel = sys::kRelayOutputActiveLow ? 1 : 0;
    for (int pin : {sys::kLightBrake,
                    sys::kBulbAuto, sys::kBulbManual,
                    sys::kBulbReady, sys::kBulbEstop, sys::kBulbBypass, sys::kPower12vRelay/*,
                    sys::kWdtToggleGpio*/}) {
        ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(pin), kOutputOffLevel));
    }

    gpio_config_t outputs = {};
    outputs.pin_bit_mask = kOutputPins;
    outputs.mode = GPIO_MODE_OUTPUT;
    outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&outputs));

    // Energize 12V accessory relay so that indicators and warning lamps have power
    set_relay(sys::kPower12vRelay, true);

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
    g_status_led.init(sys::kRgbLedGpio);
    g_status_led.set(shared::led::scale_rgb(shared::led::palette_lookup(shared::led::DomainColor::White), 64));

    
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
    set_relay(sys::kBulbReady, false);
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbEstop), GPIO_MODE_OUTPUT);
    set_relay(sys::kBulbEstop, false);
    gpio_set_direction(static_cast<gpio_num_t>(sys::kBulbBypass), GPIO_MODE_OUTPUT);
    set_relay(sys::kBulbBypass, g_bench_solo_mode);


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
    xTaskCreate(task_can_tx,    "can_tx",    3584, nullptr, 2, &h_can_tx);
    xTaskCreate(task_can_control,"can_ctrl",  2560, nullptr, 2, &h_can_control);
    xTaskCreate(task_diag,      "diag",      3584, nullptr, 1, &h_diag);
    xTaskCreate(task_hb,        "hb",        2560, nullptr, 1, &h_hb);

    ESP_LOGI(TAG, "Ready — 12 tasks running (vehicle, MTR owns motor). Mode=%s", g_mode_mgr.name());
    vTaskDelete(nullptr);
}
