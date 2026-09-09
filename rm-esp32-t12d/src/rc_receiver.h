#pragma once
// RM-ESP32-T12D — RC Receiver Driver via ESP-IDF UART peripheral.
// Decodes RadioLink T12D / R16F SBUS frames (100k baud, 8E2, inverted)
// and publishes thread-safe atomic snapshots of driver inputs.

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

    // Get latest atomic snapshot
    RcSnapshot snapshot() const;

    uint32_t raw_pulse_us(uint8_t ch) const {
        return (ch < kNumSbusChannels) ? pulse_us_[ch].load(std::memory_order_relaxed) : 0;
    }

    uint16_t raw_sbus_channel(uint8_t ch) const {
        return (ch < kNumSbusChannels) ? raw_sbus_[ch].load(std::memory_order_relaxed) : 0;
    }

    uint32_t last_frame_ms() const {
        return last_frame_time_ms_.load(std::memory_order_relaxed);
    }

    bool hardware_failsafe() const {
        return snap_failsafe_.load(std::memory_order_relaxed);
    }

private:
    SbusParser parser_;
    SbusFrame  latest_frame_{};

    std::atomic<uint32_t> last_frame_time_ms_{0};
    std::atomic<uint16_t> raw_sbus_[kNumSbusChannels]{};
    std::atomic<uint32_t> pulse_us_[kNumSbusChannels]{};

    // Thread-safe snapshot state
    std::atomic<float>    snap_steering_{0.0f};
    std::atomic<float>    snap_brake_{0.0f};
    std::atomic<float>    snap_throttle_norm_{0.0f};
    std::atomic<float>    snap_yaw_spare_{0.0f};
    std::atomic<bool>     snap_ignition_{false};
    std::atomic<can::Gear> snap_gear_{can::Gear::N};
    std::atomic<bool>     snap_switch_a_{false};
    std::atomic<bool>     snap_switch_d_{false};
    std::atomic<float>    snap_dial_vra_{0.0f};
    std::atomic<float>    snap_dial_vrb_{0.0f};
    std::atomic<bool>     snap_frame_lost_{false};
    std::atomic<bool>     snap_failsafe_{false};
    std::atomic<bool>     snap_valid_{false};
    std::atomic<uint32_t> snap_last_update_ms_{0};
};

}  // namespace rm
