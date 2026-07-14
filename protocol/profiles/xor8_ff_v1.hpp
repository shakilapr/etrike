#pragma once

#include <cstddef>
#include <cstdint>

namespace etrike::protocol::profiles {

inline constexpr char kXor8FfV1Id[] = "xor8_ff_v1";

constexpr std::uint8_t xor8_ff_v1(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint8_t value = 0;
    for (std::size_t index = 0; index < size; ++index) value ^= data[index];
    return static_cast<std::uint8_t>(value ^ 0xFFu);
}

constexpr bool verify_xor8_ff_v1(const std::uint8_t* data, std::size_t size,
                                 std::uint8_t checksum) noexcept {
    return data != nullptr && xor8_ff_v1(data, size) == checksum;
}

}  // namespace etrike::protocol::profiles
