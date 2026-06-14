#pragma once
// SYS ESP32-S3 CAN driver — thin wrapper over shared CanDriver.
// Low-level CAN bus only (built-in TWAI, GPIO4/5, 500 kbit/s).

#include "config.h"
#include "can/can_driver.h"

namespace sys {

inline can::CanDriver::Config make_can_config() {
    can::CanDriver::Config cfg;
    cfg.tx_gpio    = sys::kCanTxGpio;
    cfg.rx_gpio    = sys::kCanRxGpio;
    cfg.bitrate_hz = sys::kCanBitrateHz;
    return cfg;
}

}  // namespace sys
