#pragma once
// TWAI CAN driver — low-level CAN bus (built-in ESP32-S3 controller).
// Architecture.md §7.2: TX=GPIO5, RX=GPIO4, 500 kbit/s.
// Thin wrapper around shared/can/can_driver.h.

#include <cstdint>
#include "can/can_driver.h"
#include "config.h"

namespace rt {

// Initialize the low-level TWAI CAN bus. Returns true on success.
bool can_low_init(int tx_gpio = rt::kCanLowTxGpio,
                   int rx_gpio = rt::kCanLowRxGpio,
                   int bitrate_hz = rt::kCanLowBitrateHz);

// Get the driver instance (nullptr if not initialized).
can::CanDriver* can_low_driver();

}  // namespace rt
