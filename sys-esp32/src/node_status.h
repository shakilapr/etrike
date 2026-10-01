#pragma once
// ── 0x500 SYS_NODE_STATUS local types (purely observational) ───────────────
//
// BLOCK MASK vocabulary (protocol/contracts/sys.yaml):
//   block_mask_low  (byte 2) — transient blocks, self-clearing when the cause
//                             disappears.
//   block_mask_high (byte 3) — latched faults, cleared only by the operator
//                             reset path (MODE 5 s).
//
// `command_executing` / `command_rejected` are derived from the same inputs.

#include <cstdint>

namespace sys {

// Transient (byte 2)
constexpr uint8_t kBlkBrakeLever      = 0x01;  // rider pulling the handlebar lever
constexpr uint8_t kBlkSebSyncing      = 0x02;  // SEB brake actuator acquiring/syncing
constexpr uint8_t kBlkMtrFbkUnacked   = 0x04;  // MTR ESTOP ack still pending
constexpr uint8_t kBlkRtSetpointStale = 0x08;  // RT drive setpoint stream stale
constexpr uint8_t kBlkStartUnlatched  = 0x10;  // cockpit START run-latch released
constexpr uint8_t kBlkStartupAcquire  = 0x20;  // boot/startup acquisition window

// Latched (byte 3)
constexpr uint8_t kBlkSebL3           = 0x01;  // SEB Level-3 critical brake fault
constexpr uint8_t kBlkEgasMismatch    = 0x02;  // throttle/EGAS correlation mismatch
constexpr uint8_t kBlkMtrFbkTimeout   = 0x04;  // MTR feedback stream timed out
constexpr uint8_t kBlkTaskDeadline    = 0x08;  // critical FreeRTOS task deadline missed

}  // namespace sys
