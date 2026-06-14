#pragma once
// Obstacle speed limiting — RT applies as safety backstop on Jetson 0x400 data.
#include <cstdint>
#include "config.h"
namespace rt {
inline int32_t obstacle_limit_speed(int32_t target_mmps, uint32_t dist_mm) {
    if (dist_mm <= kObstacleStopDistMM) return 0;
    if (dist_mm >= kObstacleClearDistMM) return target_mmps;
    return int32_t(int64_t(target_mmps) * (dist_mm - kObstacleStopDistMM)
           / (kObstacleClearDistMM - kObstacleStopDistMM));
}
}
