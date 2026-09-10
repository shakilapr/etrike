#pragma once
// CAN bus health monitoring — called from t_control at 100 Hz.
#include "diag_rt.h"
// Checks both buses for error-warning, bus-off, and triggers
// ESTOP or recovery actions. Included into main.cpp for access
// to static globals.

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
                rt::diag().raise(etrike::diagnostics::DiagId::RtCanBusOff,
                                 static_cast<std::uint16_t>(
                                     (static_cast<std::uint16_t>(health.tec) << 8)
                                     | static_cast<std::uint16_t>(health.rec)));
                drv->set_tx_admission(false);
                if (g_high_to_low_gw_q) xQueueReset(g_high_to_low_gw_q);
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
                    rt::HostDriveSnapshot zero{};
                    if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
                    g_steering_estop_request.store(true);
                    g_estop_reason.store(rt::kEstopReasonBusOff);
                    if (can_send_estop()) {
                        can::Frame ef;
                        can::gen::SafetyEstop estop_msg{};
                        if (can::gen::encode_safety_estop(estop_msg, ef)
                            == can::gen::CodecStatus::Ok) {
                            post_gateway_frame(ef);
                        }
                    }
                }
            }
        } else {
            bus_off_count_low = 0;
        }
    }

    // High bus (MCP2515) — atomic state check only; SPI calls & recovery are owned by can_high_task
    {
        if (g_can_high.bus_off()) {
            bus_off_count_high++;
            if (bus_off_count_high >= 5 && !g_bench_solo_mode) {
                ESP_LOGE(TAG, "High CAN bus-off persistent - zeroing setpoints");
                g_estop_reason.store(rt::kEstopReasonBusOff);
                rt::HostDriveSnapshot zero{};
                if (g_host_cmd_mailbox) xQueueOverwrite(g_host_cmd_mailbox, &zero);
                g_steering_estop_request.store(true);
            }
        } else {
            bus_off_count_high = 0;
        }
    }
}
