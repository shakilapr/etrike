#pragma once
// HAL interface — GPIO. Logical inputs/outputs (no pins in the portable
// layer). The AURIX backend maps these to the board pins defined in
// platform/aurix/board_pins.h.

#include <cstdint>

namespace rta::hal {

// Logical input signals (rider/body inputs).
enum class InputSignal : std::uint8_t {
    EstopBtn,      // active-low, NC
    BrakeLever,    // active-low
    StartBtn,      // active-low
    ModeBtn,       // active-low
    SwLeftTurn,
    SwRightTurn,
    SwHeadlight,
};

// Logical output signals (body outputs -> relays).
enum class OutputSignal : std::uint8_t {
    LightLeft,
    LightRight,
    BrakeLight,
    Headlight,
    BulbAuto,
    BulbManual,
    Relay12v,
};

class Gpio {
public:
    virtual ~Gpio() = default;

    // Read a logical input (true = asserted/active).
    virtual bool read(InputSignal sig) const = 0;

    // Write a logical output (true = asserted).
    virtual void write(OutputSignal sig, bool asserted) = 0;

    // Read all rider/input states into a packed byte for diagnostics
    // (optional; default returns 0).
    virtual std::uint8_t read_inputs() const { return 0; }
};

}  // namespace rta::hal
