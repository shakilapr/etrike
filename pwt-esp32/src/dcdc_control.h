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
#include <cstring>

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

        PwtFrame f = PwtFrame::make_ext(kDcdcCmdId, 8);

        // Byte 0: Control
        f.set_u8(0, (m_state == State::ON) ? kDcdcEnable : kDcdcDisable);

        // Bytes 1-6: Reserved
        for (int i = 1; i <= 6; ++i) {
            f.set_u8(i, kDcdcReserved);
        }

        // Byte 7: Reset Control
        f.set_u8(7, m_reset_requested ? kDcdcReset : kDcdcNoReset);
        if (m_reset_requested) {
            m_reset_requested = false;
        }

        if (m_can->send(f)) {
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
