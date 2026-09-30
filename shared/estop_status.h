#pragma once
// Canonical `node_presence` bit positions shared by the NODE_STATUS family:
//   - SYS 0x011 SYS_SAFETY_STS  (byte 1)
//   - RT  0x501 RT_NODE_STATUS  (byte 4)
//
// Each producer fills every bit it can observe, including its own (bit = 1).
// Semantics: the node's primary stream is fresh within the presence window.
//   RT   <- 0x7FD RT_HEARTBEAT
//   MTR  <- 0x206 MTR_MOTOR_FBK / 0x502 MTR_NODE_STATUS
//   SEB  <- 0x721 SEB_STATUS
//   SES  <- 0x201 SES_STATUS (observed by RT; SYS takes this bit from RT 0x501)
//   HOST <- 0x111/0x112 request stream (SYS) / 0x7FC HOST_HEARTBEAT (RT)
//   SYS  <- 0x011/0x110 authority streams

#include <cstdint>

namespace shared {

enum NodePresenceBit : std::uint8_t {
    kNodePresenceRt   = 1u << 0,
    kNodePresenceMtr  = 1u << 1,
    kNodePresenceSeb  = 1u << 2,
    kNodePresenceSes  = 1u << 3,
    kNodePresenceHost = 1u << 4,
    kNodePresenceSys  = 1u << 5,
};

// Freshness window for the actuator/peer presence bits (RT/MTR/SEB/SES).
constexpr int kNodePresenceFreshMs = 500;

}  // namespace shared
