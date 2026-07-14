#pragma once
// CAN bus health monitoring — called from t_control at 100 Hz.
// Checks both buses for error-warning, bus-off, and triggers
// ESTOP or recovery actions. Included into main.cpp for access
// to static globals (same pattern as can_dispatch.h).

static void monitor_can_bus_off() {
    static int bus_check_ctr = 0;
    static int bus_off_count_low = 0, bus_off_count_high = 0;
    if (++bus_check_ctr < 10) return;  // check at 10 Hz (was 1 Hz)

    bus_check_ctr = 0;

    // Low bus (TWAI)
    {
        uint8_t tec = 0, rec = 0;
        auto* drv = rt::can_low_driver();
        if (drv) drv->get_error_counters(tec, rec);
        if (tec > 128)
            ESP_LOGW(TAG, "Low CAN error-warning: TEC=%u REC=%u", tec, rec);
        if (tec >= 255) {
            ESP_LOGE(TAG, "Low CAN bus-off: TEC=%u REC=%u", tec, rec);
            bus_off_count_low++;
            if (bus_off_count_low >= 5) {
                ESP_LOGE(TAG, "Low CAN bus-off persistent - triggering ESTOP");
                g_estop_reason.store(can::kEstopReasonBusOff);
                if (can_send_estop()) {
                    can::Frame ef{}; ef.id = can::kIdSafetyEstop; ef.dlc = 0;
                    xQueueSend(g_gw_tx_low_q, &ef, 0);
                    xQueueSend(g_gw_tx_high_q, &ef, 0);
                }
            }
            if (drv) drv->recovery();  // lightweight bus-off recovery
        } else {
            bus_off_count_low = 0;
        }
    }

    // High bus (MCP2515) — interrupt-driven + polled fallback
    {
        bool fast_path_handled = false;

        // Fast path: bus-off detected by interrupt (ERRIF handler in receive())
        if (g_can_high.bus_off()) {
            g_can_high.clear_bus_off();
            static int64_t last_reinit_us = 0;
            int64_t now = esp_timer_get_time();
            if (now - last_reinit_us > 500'000) {  // debounce: max 2 reinit/sec
                last_reinit_us = now;
                ESP_LOGE(TAG, "High CAN bus-off (interrupt) — reinitializing");
                bus_off_count_high++;
                g_can_high.init();
            }
            fast_path_handled = true;  // prevent slow path from resetting counter (bug 4.7)
        }

        // Slow path: polled TEC for error-warning and as fallback.
        // Only runs when fast path didn't handle a bus-off this cycle.
        // Without this guard, reinit() in the fast path zeros TEC, causing
        // the slow path to clear bus_off_count_high before it reaches 5.
        if (!fast_path_handled) {
            uint8_t tec = 0, rec = 0;
            g_can_high.get_error_counters(tec, rec);
            if (tec > 128)
                ESP_LOGW(TAG, "High CAN error-warning: TEC=%u REC=%u", tec, rec);
            if (tec >= 255) {
                ESP_LOGE(TAG, "High CAN bus-off: TEC=%u REC=%u", tec, rec);
                bus_off_count_high++;
                if (bus_off_count_high >= 5) {
                    ESP_LOGE(TAG, "High CAN bus-off persistent - zeroing setpoints");
                    g_estop_reason.store(can::kEstopReasonBusOff);
                    can::gen::HostDriveCmd zero{};
                    xQueueOverwrite(g_cmd_q, &zero);
                    g_steering.start_estop(false);
                }
                g_can_high.init();  // attempt recovery
            } else {
                bus_off_count_high = 0;
            }
        }
    }
}
