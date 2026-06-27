#pragma once
// Motor driver — wraps DAC + throttle for init. Architecture.md §8.6.
// Actual motor control uses global g_dac / g_throttle directly in task_motor().
#include "throttle_input.h"
#include "mcp4725_dac.h"
namespace sys {
class MotorDriver {
public:
    void init() { /* DAC and throttle init done via globals g_dac/g_throttle */ }
private:
    Mcp4725Dac m_dac;
    ThrottleInput m_throttle;
};
}
