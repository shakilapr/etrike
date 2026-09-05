#pragma once

#include <cstddef>
#include <cstdint>

namespace etrike {
namespace protocol {
namespace e2e {

// AUTOSAR-profile CRC-8 (CRC8H2F style) used to protect SYS_SAFETY_STS:
//   Polynomial       0x2F (MSB-first; equivalent 0x1D LSB-first)
//   Init             0xFF
//   Final XOR        0xFF
//   RefIn/RefOut     false (MSB-first bit processing)
//   Data-ID          16-bit constant folded MSB-first (high byte then low byte)
//                    into the CRC stream BEFORE the protected payload bytes.
// The protected payload for SYS_SAFETY_STS is bytes [0..3]
// (estop_active, heartbeat_ok, the four light bits, rolling_counter). The CRC
// field (byte 4) is excluded from the CRC input, and the bus identity is NOT
// mixed in, so the RT Low->High `same_frame` forward remains valid.
inline constexpr std::uint16_t kDataIdSysSafetySts = 0x3C11u;

inline std::uint8_t crc8_h2f(const std::uint8_t* data, std::size_t length,
                             std::uint16_t data_id = 0u,
                             std::uint8_t init = 0xFFu,
                             std::uint8_t final_xor = 0xFFu) noexcept {
    std::uint8_t crc = init;
    const std::uint8_t id_bytes[2] = {
        static_cast<std::uint8_t>((data_id >> 8) & 0xFFu),
        static_cast<std::uint8_t>(data_id & 0xFFu),
    };
    auto step = [&](std::uint8_t b) noexcept {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80u) {
                crc = static_cast<std::uint8_t>((static_cast<std::uint8_t>(crc << 1)) ^ 0x2Fu);
            } else {
                crc = static_cast<std::uint8_t>(crc << 1);
            }
        }
    };
    step(id_bytes[0]);
    step(id_bytes[1]);
    for (std::size_t i = 0; i < length; ++i) step(data[i]);
    return static_cast<std::uint8_t>(crc ^ final_xor);
}

// CRC over the protected SYS_SAFETY_STS payload: bytes [0..3] of the 5-byte frame.
inline std::uint8_t sys_safety_sts_crc(const std::uint8_t* payload5) noexcept {
    return crc8_h2f(payload5, 4u, kDataIdSysSafetySts);
}

}  // namespace e2e
}  // namespace protocol
}  // namespace etrike
