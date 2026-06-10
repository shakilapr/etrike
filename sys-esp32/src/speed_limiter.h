#pragma once
// Shared SYS-side motor speed limiting helpers.

#include <cstdint>

namespace sys {

int32_t limit_forward_speed_for_obstacle(int32_t target_mmps, unsigned obstacle_mm);

}  // namespace sys
