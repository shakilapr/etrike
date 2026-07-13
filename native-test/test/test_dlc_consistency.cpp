// Golden checks compile the same generated contract consumed by firmware.
#include "can/generated/can_data.h"

#include <cstdio>

static_assert(can::data::kDlc_SAFETY_ESTOP == 0);
static_assert(can::data::kDlc_SYS_SAFETY_STS == 3);
static_assert(can::data::kDlc_RT_STATE_RPT == 6);
static_assert(can::data::kDlc_HOST_LIGHT_CMD == 1);
static_assert(can::data::kDlc_HOST_HEARTBEAT == 2);
static_assert(can::data::kDlc_RT_HEARTBEAT == 2);
static_assert(can::data::kDlc_SYS_HEARTBEAT == 2);

int main() {
    std::printf("PASS: generated production DLC contract (%s)\n", can::data::kProtocolHash);
    return 0;
}

