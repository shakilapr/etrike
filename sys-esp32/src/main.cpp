// SYS ESP32-S3 — Safety, Motor Actuation & Body Control.
// Architecture: architecture.md §8.
// 15 FreeRTOS tasks, all wired to real implementation modules.
// Phases S1-S4: CAN RX, dispatch, motor, safety, mode, throttle, brake,
//               lights, dcdc, indicator, power, can_tx, diag, heartbeat.

#include <cstdio>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "config.h"
#include "can/can_protocol.h"
#include "can/can_driver.h"
#include "safety_monitor.h"
#include "mode_manager.h"
#include "throttle_input.h"
#include "mcp4725_dac.h"
#include "motor_driver.h"
#include "gear_control.h"
#include "brake_control.h"
#include "light_control.h"
#include "dcdc_control.h"
#include "indicator_control.h"
#include "wdt_toggle.h"
#include "diagnostics.h"

static const char* TAG = "sys";

// ── Application state ──────────────────────────────────────────────

static can::CanDriver      g_can(can::CanDriver::Config{sys::kCanTxGpio,
                                                         sys::kCanRxGpio,
                                                         sys::kCanBitrateHz});
static sys::SafetyMonitor  g_safety;
static sys::ModeManager    g_mode_mgr;
static sys::ThrottleInput  g_throttle;
static sys::Mcp4725Dac     g_dac;
static sys::MotorDriver    g_motor;
static sys::GearControl    g_gear;
static sys::BrakeControl   g_brake;
static sys::LightControl   g_lights;
static sys::DcdcControl    g_dcdc;
static sys::IndicatorControl g_indicator;
static sys::WdtToggle      g_wdt;
static sys::Diagnostics    g_diag;

// Shared state (written by dispatch, read by actuators)
static std::atomic<int32_t>  g_setpoint_speed_mmps{0};
static std::atomic<uint8_t>  g_setpoint_gear{0};
static std::atomic<int32_t>  g_brake_pressure_kpa{0};
static std::atomic<uint8_t>  g_light_bits{0};
static std::atomic<bool>     g_estop_flag{false};
static uint8_t               g_seb_status_raw[8] = {};
static std::atomic<uint8_t>  g_rt_hb_ctr{0};
static std::atomic<bool>     g_rt_hb_received{false};

// 0x204 staleness tracking (arch §8.6: 200ms timeout → zero speed + neutral)
static std::atomic<uint32_t> g_last_setpoint_tick{0};

// Queues
static QueueHandle_t g_can_rx_queue   = nullptr;  // 16 deep, can::Frame

// ── CAN RX task (prio 5) ───────────────────────────────────────────

[[noreturn]] static void task_can_rx(void*) {
    can::Frame fr;
    while (1) {
        if (g_can.receive(fr, 100)) {
            xQueueSend(g_can_rx_queue, &fr, 0);  // non-blocking drop-if-full
        }
    }
}

// ── Dispatch task (prio 4) ────────────────────────────────────────

[[noreturn]] static void task_dispatch(void*) {
    can::Frame fr;
    while (1) {
        if (xQueueReceive(g_can_rx_queue, &fr, portMAX_DELAY) != pdTRUE) continue;

        // Manual dispatch into atomic state (struct-based dispatch_frame not used
        // because some targets are std::atomic<T> rather than plain T*)
        switch (fr.id) {
        case can::kIdRtDriveCmd: {   // 0x204
            auto sp = can::RtDriveCmd::from_frame(fr);
            g_setpoint_speed_mmps.store(sp.motor_speed_mmps, std::memory_order_relaxed);
            g_setpoint_gear.store(sp.gear, std::memory_order_relaxed);
            g_last_setpoint_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
            break;
        }
        case can::kIdRtBrakeCmd: {   // 0x205
            auto brk = can::RtBrakeCmd::from_frame(fr);
            g_brake_pressure_kpa.store(brk.brake_pressure_kpa, std::memory_order_relaxed);
            break;
        }
        case can::kIdHostLightCmd:   // 0x302
            g_light_bits.store(fr.u8_at(0), std::memory_order_relaxed);
            break;
        case can::kIdSafetyEstop:    // 0x001
            g_estop_flag.store(true, std::memory_order_relaxed);
            g_mode_mgr.force_estop();
            ESP_LOGW(TAG, "ESTOP via CAN 0x001");
            break;
        case can::kIdSyntreeSebStatus:      // 0x721
            for (int i = 0; i < 8 && i < fr.dlc; ++i) {
                g_seb_status_raw[i] = fr.data[i];
            }
            break;
        case can::kIdRtHeartbeatLow:    // 0x7FD
            g_rt_hb_ctr.store(fr.u8_at(0), std::memory_order_relaxed);
            g_rt_hb_received.store(true, std::memory_order_relaxed);
            g_safety.feed_heartbeat_rt(fr.u8_at(0));
            break;
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
                // Broadcast CAN 0x001 ESTOP on low bus (architecture §8.4)
                // Only send once on transition to avoid flooding
                can::Frame estop_fr;
                estop_fr.id = can::kIdSafetyEstop;
                estop_fr.dlc = 0;
                g_can.send(estop_fr);
                ESP_LOGW(TAG, "ESTOP triggered — sent CAN 0x001");
            }
        }

        // Toggle external watchdog
        g_wdt.tick();  // GPIO23 toggle

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

        if (g_mode_mgr.tick(mode_btn, start_btn)) {
            // Mode changed → send 0x110 SYS_MODE_CMD
            can::Frame fr;
            can::SysModeCmd{g_mode_mgr.mode_u8()}.to_frame(fr);
            g_can.send(fr);
        }

        vTaskDelayUntil(&last, period);
    }
}

// ── Motor task (prio 4, 100 Hz) ────────────────────────────────────

[[noreturn]] static void task_motor(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kControlLoopHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        can::Mode mode = g_mode_mgr.mode();

        if (mode == can::Mode::Estop) {
            g_dac.write(0);   // MCP4725 = 0V
        } else if (mode == can::Mode::Manual) {
            // Pass-through: ADC → DAC
            g_throttle.poll();
            g_dac.set_speed_mmps(g_throttle.read_mmps());
        } else {
            // AUTO: CAN setpoint → DAC with staleness check
            // Architecture §8.6: if 0x204 stale >200ms → zero speed + neutral
            TickType_t now = xTaskGetTickCount();
            TickType_t last = g_last_setpoint_tick.load(std::memory_order_relaxed);
            if ((now - last) >= pdMS_TO_TICKS(200)) {
                g_setpoint_speed_mmps.store(0, std::memory_order_relaxed);
                g_setpoint_gear.store(0, std::memory_order_relaxed);
            }
            int32_t speed = g_setpoint_speed_mmps.load(std::memory_order_relaxed);
            g_dac.set_speed_mmps(speed);
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
#ifdef TESTING
        uint8_t sense = 0;
#else
        uint8_t sense = 0;
        if (gpio_get_level(static_cast<gpio_num_t>(sys::kGearDSense)) == 0) sense |= 0x01;
        if (gpio_get_level(static_cast<gpio_num_t>(sys::kGearSSense)) == 0) sense |= 0x02;
        if (gpio_get_level(static_cast<gpio_num_t>(sys::kGearRSense)) == 0) sense |= 0x04;
#endif
        uint8_t set_gear = g_setpoint_gear.load(std::memory_order_relaxed);
        g_gear.tick(mode, sense, set_gear);  // actuates relay GPIOs
        vTaskDelayUntil(&last, period);
    }
}

// ── Throttle task (prio 3, 100 Hz) ─────────────────────────────────

[[noreturn]] static void task_throttle(void*) {
    TickType_t period = pdMS_TO_TICKS(10);  // 100 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_throttle.poll();

        // Send 0x120 SYS_THROTTLE_STS
        can::Frame fr;
        can::SysThrottleSts{int16_t(g_throttle.read_mmps())}.to_frame(fr);
        g_can.send(fr);

        vTaskDelayUntil(&last, period);
    }
}

// ── Brake task (prio 3, 50 Hz) ─────────────────────────────────────

[[noreturn]] static void task_brake(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / sys::kBrakeCmdRateHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        bool lever     = g_safety.brake_lever_pressed();
        bool estop     = (g_mode_mgr.mode() == can::Mode::Estop);
        int32_t brake_kpa = g_brake_pressure_kpa.load(std::memory_order_relaxed);

        can::VcuSebReq seb_cmd;
        if (g_brake.tick(lever, estop, brake_kpa, g_seb_status_raw, seb_cmd)) {
            can::Frame fr;
            seb_cmd.to_frame(fr);
            g_can.send(fr);  // 0x7B9 VCU_SEB_REQ
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

        auto out = g_lights.tick(mode, lever, bits, sw_L, sw_R, sw_H);
#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightLeftTurn), out.left_lamp ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightRightTurn), out.right_lamp ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightBrake), out.brake_lamp ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kLightHead), out.head_lamp ? 1 : 0);
#endif

        vTaskDelayUntil(&last, period);
    }
}

// ── DCDC task (prio 3, 5 Hz) ──────────────────────────────────────

[[noreturn]] static void task_dcdc(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        bool estop = (g_mode_mgr.mode() == can::Mode::Estop);
        if (g_dcdc.tick(estop)) {
            can::Frame fr;
            g_dcdc.build_frame(fr);
            g_can.send(fr);  // 0x012 SYS_DCDC_CMD
        }
        vTaskDelayUntil(&last, period);
    }
}

// ── Indicator task (prio 2, 5 Hz) ──────────────────────────────────

[[noreturn]] static void task_indicator(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        auto out = g_indicator.tick(g_mode_mgr.mode());
#ifndef TESTING
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbAuto), out.auto_bulb ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kBulbManual), out.manual_bulb ? 1 : 0);
#endif

        vTaskDelayUntil(&last, period);
    }
}

// ── Power task (prio 2, 5 Hz) ─────────────────────────────────────

[[noreturn]] static void task_power(void*) {
    TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        bool on = (g_mode_mgr.mode() != can::Mode::Estop);
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
        can::Frame fr;
        can::SysSafetySts{
            g_mode_mgr.mode() == can::Mode::Estop,
            g_safety.heartbeat_ok()
        }.to_frame(fr);
        g_can.send(fr);  // 0x011 SYS_SAFETY_STS

        vTaskDelayUntil(&last, period);
    }
}

// ── Diag task (prio 1, 1 Hz) — 0x600 SYS_DIAG_RPT ──────────────────

[[noreturn]] static void task_diag(void*) {
    TickType_t period = pdMS_TO_TICKS(1000);  // 1 Hz
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_diag.report(
            g_mode_mgr.mode_u8(),
            g_safety.brake_lever_pressed(),
            g_safety.heartbeat_ok(),
            g_mode_mgr.mode() == can::Mode::Estop
        );
        // Send 0x600
        can::SysDiagRpt rpt;
        rpt.mode         = g_mode_mgr.mode_u8();
        rpt.brake_engaged = g_safety.brake_lever_pressed();
        rpt.heartbeat_ok  = g_safety.heartbeat_ok();
        rpt.estop_active  = (g_mode_mgr.mode() == can::Mode::Estop);
        rpt.free_heap_kb  = 0;
        rpt.tec = 0; rpt.rec = 0;
        can::Frame fr;
        rpt.to_frame(fr);
        g_can.send(fr);

        vTaskDelayUntil(&last, period);
    }
}

// ── Heartbeat task (prio 1, 2 Hz) — 0x7FE SYS_HEARTBEAT ────────────

[[noreturn]] static void task_hb(void*) {
    TickType_t period = pdMS_TO_TICKS(sys::kHeartbeatIntervalMs);
    TickType_t last   = xTaskGetTickCount();
    uint8_t alive_ctr = 0;
    while (1) {
        can::Frame fr;
        fr.id  = can::kIdSysHeartbeat;
        fr.dlc = 1;
        fr.put_u8(0, ++alive_ctr);
        g_can.send(fr);

        vTaskDelayUntil(&last, period);
    }
}

// ── Task handles ────────────────────────────────────────────────────

static TaskHandle_t h_can_rx, h_safety, h_dispatch, h_mode, h_motor;
static TaskHandle_t h_throttle, h_gear, h_brake, h_lights, h_dcdc;
static TaskHandle_t h_indicator, h_power, h_can_tx, h_diag, h_hb;

// ── app_main ────────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "SYS ESP32-S3 initializing...");

    // 1. Init CAN driver
    if (!g_can.init()) {
        ESP_LOGE(TAG, "CAN init failed");
        return;
    }

    // 2. Init modules
    g_safety.init();
    g_mode_mgr.init();
    g_throttle.init();
    g_dac.init();
    g_motor.init();
    g_gear.init();
    g_brake.init();
    g_lights.init();
    g_dcdc.init();
    g_indicator.init();
    g_wdt.init();
    g_diag.init();

    // 3. Create queues
    g_can_rx_queue   = xQueueCreate(16, sizeof(can::Frame));
    ESP_LOGI(TAG, "Queues created");

    // 4. Create tasks (priority, stack from architecture.md §8.7)
    xTaskCreate(task_can_rx,    "can_rx",    4096, nullptr, 5, &h_can_rx);
    xTaskCreate(task_safety,    "safety",    2048, nullptr, 5, &h_safety);
    xTaskCreate(task_dispatch,  "dispatch",  3072, nullptr, 4, &h_dispatch);
    xTaskCreate(task_mode,      "mode",      2048, nullptr, 4, &h_mode);
    xTaskCreate(task_motor,     "motor",     2048, nullptr, 4, &h_motor);
    xTaskCreate(task_gear,      "gear",      1536, nullptr, 3, &h_gear);
    xTaskCreate(task_throttle,  "throttle",  1536, nullptr, 3, &h_throttle);
    xTaskCreate(task_brake,     "brake",     2048, nullptr, 3, &h_brake);
    xTaskCreate(task_lights,    "lights",    1536, nullptr, 3, &h_lights);
    xTaskCreate(task_dcdc,      "dcdc",      1024, nullptr, 3, &h_dcdc);
    xTaskCreate(task_indicator, "indicator", 1024, nullptr, 2, &h_indicator);
    xTaskCreate(task_power,     "power",     1024, nullptr, 2, &h_power);
    xTaskCreate(task_can_tx,    "can_tx",    3072, nullptr, 2, &h_can_tx);
    xTaskCreate(task_diag,      "diag",      2048, nullptr, 1, &h_diag);
    xTaskCreate(task_hb,        "hb",        2048, nullptr, 1, &h_hb);

    ESP_LOGI(TAG, "Ready — 15 tasks running. Mode=%s", g_mode_mgr.name());
    vTaskDelete(nullptr);
}
