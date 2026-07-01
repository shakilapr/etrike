#pragma once
#include <cstdint>
#include <algorithm>
#include "shared_config.h"
namespace rt {
inline int32_t brake_arbitrate(int32_t obstacle_kpa, int32_t host_kpa) {
    return std::clamp(std::max(obstacle_kpa, host_kpa), int32_t(0), int32_t(shared::kMaxBrakeKpa));
}
}
