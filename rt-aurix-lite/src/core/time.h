#pragma once
// Explicit monotonic time. Domain code never fetches a clock; the caller
// passes now_us in. Tests pass literal values for determinism.

#include <cstdint>

namespace rta {

using TimeUs = std::uint64_t;

}  // namespace rta
