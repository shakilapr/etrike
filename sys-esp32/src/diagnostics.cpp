// System diagnostics — CAN 0x600 @ 1 Hz.

#include "diagnostics.h"
#include "config.h"
#include "can/can_protocol.h"
#include "can/can_driver.h"
#include "esp_log.h"

namespace sys {
namespace {
constexpr const char* kTag = "diag";
}

void Diagnostics::report(uint8_t mode, bool brake_engaged, bool hb_ok, bool estop) const {
    can::SysDiagRpt diag;
    diag.mode          = mode;
    diag.brake_engaged = brake_engaged;
    diag.heartbeat_ok  = hb_ok;
    diag.estop_active  = estop;
    diag.free_heap_kb  = static_cast<uint16_t>(esp_get_free_heap_size() / 1024);

    // Read real TEC/REC from TWAI driver (architecture §8.10)
    if (m_can) {
        uint8_t tec = 0, rec = 0;
        m_can->get_error_counters(tec, rec);
        diag.tec = tec;
        diag.rec = rec;
    } else {
        diag.tec = 0;
        diag.rec = 0;
    }

    can::Frame fr;
    diag.to_frame(fr);
    // Sending handled by caller (diag_task in main.cpp)
    ESP_LOGD(kTag, "mode=%d brake=%d hb=%d estop=%d heap=%uK tec=%u rec=%u",
             mode, brake_engaged, hb_ok, estop, diag.free_heap_kb, diag.tec, diag.rec);
}

}  // namespace sys
