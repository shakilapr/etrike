#pragma once
// TWAI CAN driver — low-level CAN bus (built-in ESP32-S3 controller).
// Architecture.md §7.2: TX=GPIO5, RX=GPIO4, 500 kbit/s.

#include <cstdint>
#include "protocol/compat/can.hpp"
#include "config.h"

namespace rt {

class TwaiDriver {
public:
    struct Config {
        int tx_gpio;
        int rx_gpio;
        int bitrate_hz;
    };

    explicit TwaiDriver(const Config& config) : m_config(config) {}
    ~TwaiDriver();

    TwaiDriver(const TwaiDriver&) = delete;
    TwaiDriver& operator=(const TwaiDriver&) = delete;

    bool init();
    bool recovery();
    bool receive(can::Frame& out, uint32_t timeout_ms = 100);
    bool send(const can::Frame& frame, uint32_t timeout_ms = 10);
    void get_error_counters(uint8_t& tec, uint8_t& rec) const;

private:
    Config m_config;
    bool m_initialized = false;
};

// Initialize the low-level TWAI CAN bus. Returns true on success.
bool can_low_init(int tx_gpio = rt::kCanLowTxGpio,
                   int rx_gpio = rt::kCanLowRxGpio,
                   int bitrate_hz = rt::kCanLowBitrateHz);

// Get the driver instance (nullptr if not initialized).
TwaiDriver* can_low_driver();

}  // namespace rt
