#pragma once
#include <cstdint>
#include <algorithm>
namespace rt {
inline int32_t brake_arbitrate(int32_t obstacle_kpa, int32_t jetson_kpa) {
    return std::max(obstacle_kpa, jetson_kpa);
}
}
