#pragma once
// TWAI CAN driver — low-level CAN bus (built-in ESP32-S3 controller).
// Architecture.md §7.2: TX=GPIO5, RX=GPIO4, 500 kbit/s.

#include <cstdint>
#include <atomic>
#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "protocol/compat/can.hpp"
#include "config.h"

namespace rt {

class TwaiDriver {
public:
    enum class HealthState : uint8_t { Active, Warning, Passive, BusOff };
    struct HealthSnapshot {
        HealthState state;
        uint16_t tec;
        uint16_t rec;
        bool recovery_in_progress;
        uint32_t recovery_attempts;
        uint32_t last_transition_tick;
    };
    struct Config {
        int tx_gpio;
        int rx_gpio;
        int bitrate_hz;
    };

    explicit TwaiDriver(const Config& config) : m_config(config) {}
    ~TwaiDriver();

    TwaiDriver(const TwaiDriver&) = delete;
    TwaiDriver& operator=(const TwaiDriver&) = delete;

    bool init();
    bool recovery();
    bool service_recovery(int64_t now_us);
    bool recovery_needed() const;
    void set_tx_admission(bool admitted) {
        const bool transport_active =
            m_state.load(std::memory_order_acquire) != TWAI_ERROR_BUS_OFF;
        m_tx_admitted.store(admitted && transport_active,
                            std::memory_order_release);
    }
    bool tx_admitted() const {
        return m_tx_admitted.load(std::memory_order_acquire);
    }
    HealthSnapshot health_snapshot() const;
    bool receive(can::Frame& out, uint32_t timeout_ms = 100);
    // Strictly non-blocking. Periodic callers regenerate current values next cycle.
    bool send(const can::Frame& frame, uint32_t timeout_ms = 0);
    void get_error_counters(uint8_t& tec, uint8_t& rec) const;
    // state is twai_state_t cast to uint32_t (0=stopped … 3=bus-off).
    bool status(uint32_t& state, uint32_t& tec, uint32_t& rec) const;

private:
    // ESP-IDF 5.5 abandons the active frame without on_tx_done on Bus-Off.
    // One driver/application slot lets recovery reclaim it deterministically.
    static constexpr uint8_t kTxSlots = 1;
    // With single-shot TX a frame that loses arbitration is abandoned by the
    // ESP on-chip driver WITHOUT a matching on_tx_done, so the TX slot would
    // leak and block all further Low-bus transmits. Track the in-flight slot
    // and reclaim it from send() if tx_done has not arrived within a short
    // deadline (tx_done may fire late or never on arbitration loss).
    static constexpr int64_t kTxReclaimUs = 5000;
    std::atomic<uint8_t>  m_inflight_slot{0xFF};  // 0xFF = none in flight
    std::atomic<uint32_t> m_inflight_id{0};       // id of in-flight frame (for diagnostics)
    std::atomic<int64_t>  m_inflight_us{0};
    struct RxItem {
        uint32_t id;
        uint8_t dlc;
        bool extended;
        uint8_t data[8];
    };
    struct TxSlot {
        twai_frame_t frame{};
        uint8_t data[8]{};
    };

    static bool on_rx_done(twai_node_handle_t node,
                           const twai_rx_done_event_data_t* event,
                           void* user_ctx);
    static bool on_tx_done(twai_node_handle_t node,
                           const twai_tx_done_event_data_t* event,
                           void* user_ctx);
    static bool on_state_change(twai_node_handle_t node,
                                const twai_state_change_event_data_t* event,
                                void* user_ctx);
    static HealthState map_state(twai_error_state_t state);
    static constexpr uint32_t actuation_pending_bit(uint32_t id) {
        return id == 0x204u ? 1u << 0
             : id == 0x205u ? 1u << 1
             : id == 0x169u ? 1u << 2
             : id == 0x7B9u ? 1u << 3
             : 0u;
    }
    void log_first_io_after_recovery(bool rx);
    void reset_tx_slots();

    Config m_config;
    twai_node_handle_t m_node = nullptr;
    QueueHandle_t m_rx_queue = nullptr;
    QueueHandle_t m_free_tx_slots = nullptr;
    SemaphoreHandle_t m_control_mutex = nullptr;
    TxSlot m_tx_slots[kTxSlots]{};
    bool m_initialized = false;
    std::atomic<bool> m_tx_admitted{false};
    std::atomic<uint32_t> m_actuation_pending{0};
    std::atomic<twai_error_state_t> m_state{TWAI_ERROR_ACTIVE};
    std::atomic<bool> m_recovery_in_progress{false};
    std::atomic<bool> m_recovery_completed_pending{false};
    std::atomic<uint32_t> m_recovery_attempts{0};
    std::atomic<uint32_t> m_last_transition_tick{0};
    std::atomic<int64_t> m_last_recovery_attempt_us{0};
    std::atomic<uint32_t> m_bus_off_started_tick{0};
    std::atomic<bool> m_first_rx_pending{false};
    std::atomic<bool> m_first_tx_pending{false};
    std::atomic<uint32_t> m_consecutive_bus_offs{0};
    std::atomic<int64_t> m_tx_resume_not_before_us{0};
    // Last failed TX completion (id + timestamp) so a task-context log can
    // explain WHY a frame was abandoned (arbitration loss / missing ACK)
    // without doing IO inside the ISR.
    std::atomic<uint32_t> m_last_tx_fail_id{0};
    std::atomic<int64_t>  m_last_tx_fail_us{0};
    std::atomic<uint32_t> m_bus_off_stuck_slot{0xFF};  // slot held at Bus-Off entry
    std::atomic<int64_t>  m_last_send_fail_log_us{0};  // rate-limit TX-fail logs

#if ETRIKE_RT_TWAI_INSTRUMENT
    // Instrumentation counters — all zero-initialized, never cleared after init.
    // Examined in service_recovery() to prove or disprove the slot-leak hypothesis.
    //   tx_submitted:       incremented each time twai_node_transmit() succeeds in send()
    //   tx_done_ok:         incremented in on_tx_done when tx_success = true
    //   tx_done_fail:       incremented in on_tx_done when tx_success = false
    //   tx_done_no_slot:    incremented in on_tx_done when done_tx_frame matches no slot
    //                       (indicates driver called back for a frame not in our pool)
    //   free_slots_at_busoff:     depth of m_free_tx_slots at Bus-Off entry (ISR context)
    //   free_slots_at_recovery:   depth of m_free_tx_slots at recovery completion (ISR context)
    //   busoff_count:       total Bus-Off events observed
    std::atomic<uint32_t> m_instr_tx_submitted{0};
    std::atomic<uint32_t> m_instr_tx_done_ok{0};
    std::atomic<uint32_t> m_instr_tx_done_fail{0};
    std::atomic<uint32_t> m_instr_tx_done_no_slot{0};
    std::atomic<uint8_t>  m_instr_free_slots_at_busoff{0};
    std::atomic<uint8_t>  m_instr_free_slots_at_recovery{0};
    std::atomic<uint32_t> m_instr_busoff_count{0};
#endif
};

// Initialize the low-level TWAI CAN bus. Returns true on success.
bool can_low_init(int tx_gpio = rt::kCanLowTxGpio,
                   int rx_gpio = rt::kCanLowRxGpio,
                   int bitrate_hz = rt::kCanLowBitrateHz);

// Get the driver instance (nullptr if not initialized).
TwaiDriver* can_low_driver();

}  // namespace rt
