// PWT ESP32-S3 — Powertrain CAN Gateway
// STUB: Phase 1 — DC-DC converter control on 250 kbit/s powertrain bus.
// CAN gateway (bridging 0x012, 0x001) and heartbeat (0x7FB) not yet
// implemented. Architecture specifies 5 tasks; currently 2 are active.
// See pwt-esp32/pwt-architecture.md for target architecture.
//
// Standalone unit — does not depend on shared/.

#include "config.h"
#include "can_driver.h"
#include "dcdc_control.h"
#include "wdt_toggle.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── Global singletons ────────────────────────────────────────────────

static pwt::CanDriver   g_can;
static pwt::DcdcControl g_dcdc;
static pwt::WdtToggle   g_wdt;

// ── Tasks ────────────────────────────────────────────────────────────

[[noreturn]] static void task_wdt(void*) {
    TickType_t period = pdMS_TO_TICKS(1000 / pwt::kWdtToggleRateHz);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_wdt.tick();
        vTaskDelayUntil(&last, period);
    }
}

[[noreturn]] static void task_dcdc(void*) {
    TickType_t period = pdMS_TO_TICKS(pwt::kDcdcCycleMs);
    TickType_t last   = xTaskGetTickCount();
    while (1) {
        g_dcdc.tick();
        vTaskDelayUntil(&last, period);
    }
}

// ── Entry point ──────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI("pwt", "── PWT ESP32-S3 starting ──");

    // 1. Initialize powertrain CAN bus (TWAI1, 250 kbit/s)
    if (!g_can.init(pwt::kCanPwtTxGpio, pwt::kCanPwtRxGpio, pwt::kCanPwtBitrateHz)) {
        ESP_LOGE("pwt", "CAN init failed — aborting");
        return;
    }
    ESP_LOGI("pwt", "CAN init OK: TWAI1, %d kbit/s, TX=GPIO%d RX=GPIO%d",
             pwt::kCanPwtBitrateHz / 1000, pwt::kCanPwtTxGpio, pwt::kCanPwtRxGpio);

    // 2. Initialize DC-DC control (enabled by default)
    g_dcdc.init(&g_can);

    // 3. Initialize external watchdog toggle (GPIO21)
    g_wdt.init();
    ESP_LOGI("pwt", "WDT init OK: GPIO%d @ %d Hz",
             pwt::kWdtToggleGpio, pwt::kWdtToggleRateHz);

    // 4. Create FreeRTOS tasks
    xTaskCreate(task_wdt,  "wdt",  1536, nullptr, 5, nullptr);  // prio 5, 20 Hz
    xTaskCreate(task_dcdc, "dcdc", 2048, nullptr, 3, nullptr);  // prio 3, 10 Hz

    ESP_LOGI("pwt", "── PWT Ready (2 tasks, DC-DC control active) ──");

    // 5. Done — delete self
    vTaskDelete(nullptr);
}
