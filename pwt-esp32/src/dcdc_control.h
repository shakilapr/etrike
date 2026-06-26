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
        m_state = State::ON;  // DC-DC ON by default (per architecture §3: ON in all modes)
        ESP_LOGI("dcdc", "init: state=ON, can_id=0x%08lX", kDcdcCmdId);
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

        if (!m_can->send(f)) {
            ESP_LOGW("dcdc", "TX fail");
        }
    }

    void enable()        { m_state = State::ON; }
    void disable()       { m_state = State::OFF; }
    void request_reset() { m_reset_requested = true; }
    bool is_on() const   { return m_state == State::ON; }

private:
    CanDriver* m_can            = nullptr;
    State      m_state          = State::ON;
    bool       m_reset_requested = false;
};

} // namespace pwt
