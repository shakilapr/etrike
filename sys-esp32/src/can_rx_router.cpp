// SYS private CAN RX routing policy.

#include "can_rx_router.h"

namespace sys {

CanRxRoute classify_can_rx_frame(const can::Frame& frame) {
    CanRxRoute route;
    route.enqueue = frame.id == can::kIdSyntreeEpsStatus
                 || frame.id == can::kIdSyntreeSebStatus;
    return route;
}

}  // namespace sys
