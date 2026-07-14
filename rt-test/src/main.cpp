// RT CAN Test — sends multiple CAN IDs on both buses at 10 Hz.
// Low bus (TWAI):  0x100, 0x101, 0x102  — look for these on CANalyst-II Ch0
// High bus (MCP2515): 0x200, 0x201, 0x202 — look for these on CANalyst-II Ch1

#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "can_driver_twai.h"
#include "can_driver_mcp2515.h"

static const char* TAG = "rt-test";

// ── CAN instances ────────────────────────────────────────────────────
alignas(rt::TwaiDriver) static unsigned char g_can_low_storage[sizeof(rt::TwaiDriver)];
static rt::TwaiDriver* g_can_low = nullptr;
static rt::Mcp2515Driver g_can_high;

// ── Stats ────────────────────────────────────────────────────────────
static std::atomic<uint32_t> g_low_tx{0}, g_low_fail{0};
static std::atomic<uint32_t> g_high_tx{0}, g_high_fail{0};

// ── Health (1 Hz) ────────────────────────────────────────────────────
[[noreturn]] static void task_health(void*) {
    TickType_t last = xTaskGetTickCount();
    while (1) {
        uint8_t tec = 0, rec = 0;
        if (g_can_low) {
            g_can_low->get_error_counters(tec, rec);
            ESP_LOGI(TAG, "LOW  TWAI   : tx=%5lu fail=%5lu TEC=%3u REC=%3u",
                     g_low_tx.load(), g_low_fail.load(), tec, rec);
        }
        if (g_can_high.is_initialized()) {
            g_can_high.get_error_counters(tec, rec);
            ESP_LOGI(TAG, "HIGH MCP2515: tx=%5lu fail=%5lu TEC=%3u REC=%3u bo=%d",
                     g_high_tx.load(), g_high_fail.load(), tec, rec,
                     g_can_high.bus_off());
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

// ── Send a frame, update stats ───────────────────────────────────────
static void tx_low(uint32_t id, uint8_t d0, uint8_t d1) {
    can::Frame f{};
    f.id = id;
    f.dlc = 2;
    f.data[0] = d0;
    f.data[1] = d1;
    if (g_can_low && g_can_low->send(f)) {
        g_low_tx.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_low_fail.fetch_add(1, std::memory_order_relaxed);
    }
}

static void tx_high(uint32_t id, uint8_t d0, uint8_t d1) {
    can::Frame f{};
    f.id = id;
    f.dlc = 2;
    f.data[0] = d0;
    f.data[1] = d1;
    if (g_can_high.is_initialized() && g_can_high.send(f)) {
        g_high_tx.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_high_fail.fetch_add(1, std::memory_order_relaxed);
    }
}

// ── TX task (10 Hz) — 3 frames per bus per cycle ─────────────────────
[[noreturn]] static void task_tx(void*) {
    TickType_t last = xTaskGetTickCount();
    uint8_t seq = 0;

    while (1) {
        seq++;

        // Low bus: 0x100, 0x101, 0x102
        tx_low(0x100, seq, 0x00);
        tx_low(0x101, seq, 0x01);
        tx_low(0x102, seq, 0x02);

        // High bus: 0x200, 0x201, 0x202
        tx_high(0x200, seq, 0x10);
        tx_high(0x201, seq, 0x11);
        tx_high(0x202, seq, 0x12);

        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}

// ── app_main ─────────────────────────────────────────────────────────
extern "C" void app_main() {
    ESP_LOGI(TAG, "=== RT CAN Test ===");

    g_can_low = new (static_cast<void*>(g_can_low_storage))
        rt::TwaiDriver(rt::TwaiDriver::Config{5, 4, 500'000});
    if (g_can_low->init()) {
        ESP_LOGI(TAG, "TWAI OK — TX=5 RX=4 @ 500k");
    } else {
        ESP_LOGE(TAG, "TWAI FAILED");
        g_can_low = nullptr;
    }

    if (g_can_high.init()) {
        ESP_LOGI(TAG, "MCP2515 OK — starting in Normal mode");
        g_can_high.set_mode(rt::Mcp2515Driver::Mode::Normal);
    } else {
        ESP_LOGE(TAG, "MCP2515 FAILED");
    }

    xTaskCreate(task_tx,    "tx",   3072, nullptr, 3, nullptr);
    xTaskCreate(task_health, "hlth", 3072, nullptr, 1, nullptr);

    ESP_LOGI(TAG, "Sending 0x100-0x102 on low, 0x200-0x202 on high @ 10 Hz");
    ESP_LOGI(TAG, "Ready");
    vTaskDelete(nullptr);
}
