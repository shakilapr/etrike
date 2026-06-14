#pragma once
// DC-DC converter CAN control. Architecture.md §8.6. 0x012 on state change.
#include "can/can_protocol.h"
namespace sys {
class DcdcControl {
public:
    void init() { m_enabled = false; m_last = false; }
    // Returns true if 0x012 should be sent (state changed)
    bool tick(bool estop) {
        bool want = !estop;
        if (want == m_last) return false;
        m_last = want; m_enabled = want; return true;
    }
    bool enabled() const { return m_enabled; }
    void build_frame(can::Frame& f) const {
        f.id=0x012;f.dlc=1;f.put_u8(0,m_enabled?1:0);
    }
private:
    bool m_enabled=false, m_last=false;
};
}
