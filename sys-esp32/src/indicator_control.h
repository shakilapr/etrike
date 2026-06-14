#pragma once
// Mode indicator bulbs + 12V relay. Architecture.md §8.6.
#include "can/can_protocol.h"
namespace sys {
struct IndicatorOutputs { bool auto_bulb, manual_bulb, relay_12v; };
class IndicatorControl {
public:
    void init() {}  // no-op (no persistent state needed)
    IndicatorOutputs tick(can::Mode mode) {
        IndicatorOutputs o;
        o.auto_bulb=(mode==can::Mode::Auto);
        o.manual_bulb=(mode==can::Mode::Manual);
        o.relay_12v=(mode!=can::Mode::Estop);
        return o;
    }
};
}
