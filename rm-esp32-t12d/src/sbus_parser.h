#pragma once
// RM-ESP32-T12D — Zero-allocation SBUS Protocol Parser & Bit Unpacker
// Decodes 25-byte standard SBUS frames (100k baud, 8E2, inverted).

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace rm {

struct SbusFrame {
    uint16_t channels[16]{0};  // 16 proportional channels (11-bit raw: ~172 to ~1811)
    bool ch17{false};          // Digital channel 17
    bool ch18{false};          // Digital channel 18
    bool frame_lost{false};    // Frame loss flag from receiver
    bool failsafe{false};      // Failsafe active flag from receiver
};

// Converts 11-bit raw SBUS count (~172 to ~1811) to equivalent microsecond pulse (~988 to ~2012 us).
inline uint32_t sbus_to_pulse_us(uint16_t raw) {
    if (raw <= 172) return 988;
    if (raw >= 1811) return 2012;
    float norm = static_cast<float>(raw - 172) / static_cast<float>(1811 - 172);
    return static_cast<uint32_t>(std::round(988.0f + norm * (2012.0f - 988.0f)));
}

// Converts microsecond pulse (~988 to ~2012 us) back to 11-bit SBUS raw count (useful for testing/simulation).
inline uint16_t pulse_us_to_sbus(uint32_t us) {
    if (us <= 988) return 172;
    if (us >= 2012) return 1811;
    float norm = static_cast<float>(us - 988) / static_cast<float>(2012 - 988);
    return static_cast<uint16_t>(std::round(172.0f + norm * (1811.0f - 172.0f)));
}

class SbusParser {
public:
    static constexpr size_t kFrameSize = 25;
    static constexpr uint8_t kHeaderByte = 0x0F;

    SbusParser() = default;

    // Reset parser state machine
    void reset();

    // Feeds a single byte. Returns true if a full valid frame has been received and decoded.
    bool parse_byte(uint8_t byte, SbusFrame& out_frame);

    // Unpacks a 25-byte raw buffer directly into SbusFrame. Returns true if valid header/footer.
    static bool decode_buffer(const uint8_t buf[kFrameSize], SbusFrame& out_frame);

private:
    uint8_t buffer_[kFrameSize]{0};
    size_t  idx_{0};
};

}  // namespace rm
