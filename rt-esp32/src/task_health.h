#pragma once

#include <cstdint>

namespace rt {

constexpr uint8_t kTaskHealthHigh     = 1u << 0;
constexpr uint8_t kTaskHealthLow      = 1u << 1;
constexpr uint8_t kTaskHealthControl  = 1u << 2;
constexpr uint8_t kTaskHealthAllTasks = 0x07u;
constexpr uint8_t kTaskHealthLegacyOk = 1u << 3;

inline uint8_t task_health_from_timestamps(
    int64_t now_us,
    int64_t high_us,
    int64_t low_us,
    int64_t control_us,
    int64_t timeout_us = 500'000) {
    uint8_t health = 0;
    if (now_us - high_us <= timeout_us) health |= kTaskHealthHigh;
    if (now_us - low_us <= timeout_us) health |= kTaskHealthLow;
    if (now_us - control_us <= timeout_us) health |= kTaskHealthControl;
    if ((health & kTaskHealthAllTasks) == kTaskHealthAllTasks)
        health |= kTaskHealthLegacyOk;
    return health;
}

}  // namespace rt