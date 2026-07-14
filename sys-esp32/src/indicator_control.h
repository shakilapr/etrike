#pragma once
// Mode indicator bulbs + 12V relay. Architecture.md §8.6.
#include "config.h"
namespace sys {
struct IndicatorOutputs { bool auto_bulb, manual_bulb; };
class IndicatorControl {
public:
    void init() {}  // no-op (no persistent state needed)
    IndicatorOutputs tick(can::Mode mode) {
        IndicatorOutputs o;
        o.auto_bulb=(mode==can::Mode::Auto);
        o.manual_bulb=(mode==can::Mode::Manual);
        return o;
    }
};
}
