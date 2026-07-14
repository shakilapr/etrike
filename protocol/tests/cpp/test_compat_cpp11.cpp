#include <array>
#include <cstdint>

#include "protocol/compat/transport.hpp"

struct DriverFrame {
    std::uint32_t id;
    bool extended;
    std::uint8_t dlc;
    std::uint8_t data[8];
};

struct ShortDriverFrame {
    std::uint32_t id;
    bool extended;
    std::uint8_t dlc;
    std::uint8_t data[2];
};

int main() {
    DriverFrame driver = {0x321u, false, 3u, {1u, 2u, 3u}};
    etrike::protocol::Frame frame;
    if (!etrike::protocol::compat::to_protocol_frame(driver, frame)) return 1;
    if (frame.id != 0x321u || frame.dlc != 3u || frame.data[2] != 3u) return 2;

    DriverFrame round_trip = {};
    if (!etrike::protocol::compat::from_protocol_frame(frame, round_trip)) return 3;
    if (round_trip.id != driver.id || round_trip.extended != driver.extended ||
        round_trip.dlc != driver.dlc || round_trip.data[1] != driver.data[1])
        return 4;

    driver.dlc = 9u;
    const etrike::protocol::Frame preserved = frame;
    if (etrike::protocol::compat::to_protocol_frame(driver, frame)) return 5;
    if (frame.id != preserved.id || frame.data != preserved.data) return 6;

    const std::array<std::uint8_t, 2> bytes = {{0xAAu, 0x55u}};
    const etrike::protocol::FrameView view(0x1ABCDEu, true, 2u, bytes.data());
    if (!etrike::protocol::is_valid_frame(view)) return 7;
    const ShortDriverFrame short_frame = {0x123u, false, 3u, {1u, 2u}};
    if (etrike::protocol::compat::to_protocol_frame(short_frame, frame)) return 8;
    ShortDriverFrame short_output = {};
    if (etrike::protocol::compat::from_protocol_frame(preserved, short_output)) return 9;
    return 0;
}
