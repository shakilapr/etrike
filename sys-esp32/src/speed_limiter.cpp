// SYS-side speed limiting.

#include "speed_limiter.h"
#include "config.h"
#include <limits>

namespace sys {

int32_t limit_forward_speed_for_obstacle(int32_t target_mmps, unsigned obstacle_mm) {
    if (target_mmps <= 0) return target_mmps;
    if (obstacle_mm == std::numeric_limits<unsigned>::max()) return target_mmps;
    if (obstacle_mm <= kObstacleStopDistMM) return 0;
    if (obstacle_mm >= kObstacleClearDistMM) return target_mmps;

    const float scale = static_cast<float>(obstacle_mm - kObstacleStopDistMM)
        / static_cast<float>(kObstacleClearDistMM - kObstacleStopDistMM);
    return static_cast<int32_t>(target_mmps * scale);
}

}  // namespace sys
