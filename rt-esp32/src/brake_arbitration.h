#pragma once
#include <cstdint>
#include <algorithm>
namespace rt {
inline int32_t brake_arbitrate(int32_t obstacle_kpa, int32_t host_kpa) {
    return std::max(obstacle_kpa, host_kpa);
}
}
