#pragma once

#include <cstdint>

namespace etrike::protocol {

constexpr std::uint32_t low_mask(std::uint8_t width) noexcept {
    return width == 0 ? 0u : (width >= 32 ? 0xFFFFFFFFu : ((std::uint32_t{1} << width) - 1u));
}

constexpr std::uint32_t extract_bits(std::uint32_t value, std::uint8_t offset,
                                     std::uint8_t width) noexcept {
    return offset >= 32 ? 0u : ((value >> offset) & low_mask(width));
}

constexpr std::uint32_t insert_bits(std::uint32_t destination, std::uint32_t value,
                                    std::uint8_t offset, std::uint8_t width) noexcept {
    if (offset >= 32 || width == 0) return destination;
    const std::uint32_t field_mask = low_mask(width) << offset;
    return (destination & ~field_mask) | ((value << offset) & field_mask);
}

constexpr bool test_bit(std::uint32_t value, std::uint8_t bit) noexcept {
    return bit < 32 && (value & (std::uint32_t{1} << bit)) != 0;
}

}  // namespace etrike::protocol
