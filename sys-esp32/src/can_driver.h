#pragma once
// SYS ESP32-S3 CAN driver — thin wrapper over shared CanDriver.
// Low-level CAN bus only (built-in TWAI, GPIO4/5, 500 kbit/s).
// CAN config is constructed inline in main.cpp via can::CanDriver::Config{}.

#include "config.h"
#include "can/can_driver.h"
