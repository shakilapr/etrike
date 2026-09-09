#pragma once
// RM-ESP32-T12D — RC Receiver Driver via ESP-IDF UART peripheral.
// Decodes RadioLink T12D / R16F SBUS frames (100k baud, 8E2, inverted)
// and publishes thread-safe atomic snapshots of driver inputs using a seqlock.

#include <cstdint>
#include <atomic>
#include "driver/uart.h"
#include "protocol/compat/can.hpp"
#include "config.h"
#include "sbus_parser.h"
#include "rc_decoder.h"

namespace rm {

class RcReceiver {
public:
    RcReceiver() = default;
    ~RcReceiver();

    // Initialize the hardware UART peripheral with line inversion for SBUS
    bool init();

    // Drain UART buffer, process SBUS stream, and update snapshot
    void sample(uint32_t now_ms);

    // Get latest atomic, tear-free snapshot (thread-safe across cores)
    RcSnapshot snapshot() const;

private:
    SbusParser parser_;
    SbusFrame  latest_frame_{};

    std::atomic<uint32_t> last_frame_time_ms_{0};

    // Lock-free seqlock for atomic snapshot consistency across Core 1 and Core 0
    mutable std::atomic<uint32_t> seq_{0};
    RcSnapshot snap_{};
};

}  // namespace rm
