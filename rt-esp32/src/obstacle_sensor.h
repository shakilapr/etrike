#pragma once
#include <cstdint>
#include "config.h"
namespace rt {
inline uint32_t obstacle_distance_mm(uint32_t echo_us) {
    if (echo_us>30000) return UINT32_MAX;
    return echo_us*343/2000; // mm = us * speed_of_sound / 2 / 1000
}
inline int32_t obstacle_limit_speed(int32_t target, uint32_t dist_mm) {
    if (dist_mm<=kObstacleStopDistMM) return 0;
    if (dist_mm>=kObstacleClearDistMM) return target;
    return int32_t(int64_t(target)*(dist_mm-kObstacleStopDistMM)/(kObstacleClearDistMM-kObstacleStopDistMM));
}
}
