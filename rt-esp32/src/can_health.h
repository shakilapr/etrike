#pragma once
// CAN bus health monitoring — called from t_control at 100 Hz.
// Checks both buses for error-warning, bus-off, and triggers
// ESTOP or recovery actions. Included into main.cpp for access
// to static globals (same pattern as can_dispatch.h).

static void monitor_can_bus_off() {
    static int bus_check_ctr = 0;
    static int bus_off_count_low = 0, bus_off_count_high = 0;
    static bool low_passive_reported = false;
    static uint32_t handled_low_recovery_attempts = 0;
    if (++bus_check_ctr < 10) return;  // check at 10 Hz (was 1 Hz)

    bus_check_ctr = 0;

    // Low bus (TWAI)
    {
        auto* drv = rt::can_low_driver();
        const auto health = drv ? drv->health_snapshot() : rt::TwaiDriver::HealthSnapshot{};
        const bool low_passive =
            drv && health.state == rt::TwaiDriver::HealthState::Passive;
        if (low_passive && !low_passive_reported) {
            ESP_LOGW(TAG, "Low CAN error-passive: TEC=%u REC=%u", health.tec, health.rec);
        } else if (!low_passive && low_passive_reported) {
            ESP_LOGI(TAG, "Low CAN left error-passive state");
        }
        low_passive_reported = low_passive;

        // Recovery can complete between 10 Hz health polls. The monotonic
        // attempt count preserves the Bus-Off event even when current state is
        // already Active again.
        const bool new_bus_off_event =
            drv && health.recovery_attempts != handled_low_recovery_attempts;
        if (new_bus_off_event) {
            handled_low_recovery_attempts = health.recovery_attempts;
        }
        if (drv && (health.state == rt::TwaiDriver::HealthState::BusOff
                    || new_bus_off_event)) {
            bus_off_count_low++;
            if (bus_off_count_low == 1 || new_bus_off_event) {
                drv->set_tx_admission(false);
                xQueueReset(g_gw_tx_low_q);
                if (g_bench_solo_mode) {
                    ESP_LOGW(TAG,
                        "Low CAN unavailable in developer bypass: TEC=%u REC=%u; "
                        "recovery supervisor active; TX held until stable",
                        health.tec, health.rec);
                } else {
                    ESP_LOGE(TAG,
                        "Low CAN bus-off: TEC=%u REC=%u - latching ESTOP; "
                        "transport recovery remains safety-gated",
                        health.tec, health.rec);
                    const rt::SafetyEvent event{
                        rt::SafetyEvent::ESTOP, rt::kEstopReasonBusOff};
                    enqueue_safety_event(event, 0);
                    can::gen::HostDriveCmd zero{};
                    xQueueOverwrite(g_cmd_q, &zero);
                    g_steering_estop_request.store(true);
                    g_estop_reason.store(rt::kEstopReasonBusOff);
                    if (can_send_estop()) {
                        can::Frame ef;
                        can::gen::SafetyEstop estop_msg{};
                        if (can::gen::encode_safety_estop(estop_msg, ef)
                            == can::gen::CodecStatus::Ok) {
                            xQueueSend(g_gw_tx_high_q, &ef, 0);
                        }
                    }
                }
            }
        } else {
            bus_off_count_low = 0;
        }
    }

    // High bus (MCP2515) — interrupt-driven + polled fallback
    {
        // Bus-off is latched by the receive path and remains latched until a
        // complete controller-only recovery succeeds.
        if (g_can_high.bus_off()) {
            bus_off_count_high++;
            static int64_t last_reinit_us = 0;
            int64_t now = esp_timer_get_time();
            if (last_reinit_us == 0 || now - last_reinit_us > 3'000'000) {
                last_reinit_us = now;
                ESP_LOGE(TAG, "High CAN bus-off — controller recovery");
                g_can_high.recover();
            }
            if (bus_off_count_high >= 5 && !g_bench_solo_mode) {
                ESP_LOGE(TAG, "High CAN bus-off persistent - zeroing setpoints");
                g_estop_reason.store(rt::kEstopReasonBusOff);
                can::gen::HostDriveCmd zero{};
                xQueueOverwrite(g_cmd_q, &zero);
                g_steering_estop_request.store(true);
            }
        } else if (!g_can_high.is_recovering()) {
            uint8_t tec = 0, rec = 0;
            g_can_high.get_error_counters(tec, rec);
            if (tec > 128)
                ESP_LOGW(TAG, "High CAN error-warning: TEC=%u REC=%u", tec, rec);
            bus_off_count_high = 0;
        }
    }
}
