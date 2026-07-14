#pragma once
// DC-DC converter control via extended CAN (250 kbit/s powertrain bus).
// Manufacturer protocol: VCU(27H) → DCDC(2BH)
// ID: 0x10262B27 (29-bit extended), DLC=8, 100 ms cycle.
//
// Byte 0: Control      00=Disable  01=Enable
// Bytes 1-6: Reserved  0xFF each
// Byte 7: Reset Ctrl    00=No reset  01=Reset

#include "can_driver.h"
#include "config.h"
#include "esp_log.h"

namespace pwt {

class DcdcControl {
public:
    enum class State : uint8_t { OFF = 0, ON = 1 };

    void init(CanDriver* can) {
        m_can  = can;
        m_state = kDcdcDefaultEnabled ? State::ON : State::OFF;
        ESP_LOGI("dcdc", "init: state=%s, can_id=0x%08lX (local build config)",
                 is_on() ? "ON" : "OFF", kDcdcCmdId);
    }

    void tick() {
        if (!m_can || !m_can->initialized()) return;

        etrike::protocol::Frame frame;
        DcdcCommand message{};
        message.control = m_state == State::ON;
        message.reset_control = m_reset_requested;
        if (etrike::protocol::generated::encode(message, frame) !=
            etrike::protocol::CodecStatus::Ok) return;
        if (m_reset_requested) {
            m_reset_requested = false;
        }

        if (m_can->send(frame)) {
            if (m_consecutive_tx_failures != 0) {
                ESP_LOGI("dcdc", "CAN TX recovered after %lu failures",
                         static_cast<unsigned long>(m_consecutive_tx_failures));
            }
            m_consecutive_tx_failures = 0;
        } else {
            ++m_consecutive_tx_failures;
            ++m_total_tx_failures;
            if (m_consecutive_tx_failures == 1 ||
                (m_consecutive_tx_failures % 50) == 0) {
                uint8_t tec = 0, rec = 0;
                m_can->get_error_counters(tec, rec);
                ESP_LOGW("dcdc", "CAN TX unavailable: streak=%lu total=%lu TEC=%u REC=%u",
                         static_cast<unsigned long>(m_consecutive_tx_failures),
                         static_cast<unsigned long>(m_total_tx_failures), tec, rec);
            }
        }
    }

    void enable()        { m_state = State::ON; }
    void disable()       { m_state = State::OFF; }
    void request_reset() { m_reset_requested = true; }
    bool is_on() const   { return m_state == State::ON; }
    uint32_t consecutive_tx_failures() const { return m_consecutive_tx_failures; }
    uint32_t total_tx_failures() const { return m_total_tx_failures; }

private:
    CanDriver* m_can            = nullptr;
    State      m_state          = State::ON;
    bool       m_reset_requested = false;
    uint32_t   m_consecutive_tx_failures = 0;
    uint32_t   m_total_tx_failures = 0;
};

} // namespace pwt
