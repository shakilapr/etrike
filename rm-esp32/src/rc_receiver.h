#pragma once
// RM-ESP32 — RC Receiver Driver via ESP-IDF RMT hardware peripheral.
// Normalizes raw RMT pulse captures into validated microsecond durations
// and decodes an atomic snapshot of driver inputs.

#include <cstdint>
#include <atomic>
#include "driver/rmt.h"
#include "protocol/compat/can.hpp"
#include "config.h"
#include "rc_decoder.h"

namespace rm {

class RcReceiver {
public:
    RcReceiver() = default;
    ~RcReceiver() = default;

    // Initialize the 6 RMT RX channels
    bool init();

    // Drain ringbuffers, extract latest pulse widths (us), and update state
    void sample(uint32_t now_ms);

    // Get latest atomic snapshot
    RcSnapshot snapshot() const;

private:
    static rmt_channel_t channel_from_index(uint8_t index) {
        return static_cast<rmt_channel_t>(RMT_CHANNEL_0 + index);
    }

    static gpio_num_t gpio_from_index(uint8_t index) {
        switch (index) {
        case 0: return static_cast<gpio_num_t>(kRcDriveGpio);
        case 1: return static_cast<gpio_num_t>(kRcBrakeGpio);
        case 2: return static_cast<gpio_num_t>(kRcAuxAnalogGpio);
        case 3: return static_cast<gpio_num_t>(kRcPassGpio);
        case 4: return static_cast<gpio_num_t>(kRcIgnitionGpio);
        case 5: return static_cast<gpio_num_t>(kRcGearGpio);
        default: return GPIO_NUM_NC;
        }
    }

    // Channel raw high pulse width in microseconds (1 count = 1.0 us)
    uint32_t raw_high_us_[kNumRcChannels]{0};
    uint32_t last_edge_time_ms_[kNumRcChannels]{0};
    RingbufHandle_t ringbufs_[kNumRcChannels]{nullptr};

    // Filtered/Hysteresis values
    uint32_t past_filtered_us_[kNumRcChannels]{0};

    // Thread-safe snapshot state
    std::atomic<float>    snap_steering_{0.0f};
    std::atomic<float>    snap_brake_{0.0f};
    std::atomic<float>    snap_speed_trim_{1.0f};
    std::atomic<float>    snap_aux_pass_{0.0f};
    std::atomic<bool>     snap_ignition_{false};
    std::atomic<can::Gear> snap_gear_{can::Gear::N};
    std::atomic<bool>     snap_valid_{false};
    std::atomic<uint32_t> snap_last_update_ms_{0};
};

}  // namespace rm
