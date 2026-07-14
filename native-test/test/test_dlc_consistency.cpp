// Golden checks compile the same generated contract consumed by firmware.
#include "protocol/generated/cpp/etrike_protocol.hpp"

#include <cstdio>

namespace generated = etrike::protocol::generated;

static_assert(generated::SafetyEstop::kDlc == 0);
static_assert(generated::SysSafetySts::kDlc == 3);
static_assert(generated::RtStateRpt::kDlc == 6);
static_assert(generated::HostLightCmd::kDlc == 1);
static_assert(generated::HostHeartbeat::kDlc == 2);
static_assert(generated::RtHeartbeat::kDlc == 2);
static_assert(generated::SysHeartbeat::kDlc == 2);

int main() {
    std::printf("PASS: generated production DLC contract (%.*s)\n",
                static_cast<int>(etrike::protocol::kWireHash.size()),
                etrike::protocol::kWireHash.data());
    return 0;
}
