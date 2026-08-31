#pragma once
// HAL interface — monotonic clock. The application calls this to obtain
// TimeUs and passes it explicitly into domain logic (domain never calls
// the clock itself).

#include <cstdint>
#include "core/time.h"

namespace rta::hal {

class Clock {
public:
    virtual ~Clock() = default;

    // Monotonic microseconds since an arbitrary fixed origin.
    virtual TimeUs monotonic_us() = 0;
};

}  // namespace rta::hal
