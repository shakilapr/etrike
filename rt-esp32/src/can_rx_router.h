#pragma once
// Testable private CAN RX routing policy for SYS.

#include "can_protocol.h"

namespace sys {

struct CanRxRoute {
    bool enqueue = false;
};

CanRxRoute classify_can_rx_frame(const can::Frame& frame);

}  // namespace sys
