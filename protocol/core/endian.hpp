#pragma once

#include <cstdint>

namespace etrike::protocol {

constexpr std::uint16_t read_le_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
}

constexpr std::int16_t read_le_i16(const std::uint8_t* data) noexcept {
    return static_cast<std::int16_t>(read_le_u16(data));
}

constexpr std::uint32_t read_le_u32(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

constexpr std::uint16_t read_be_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) << 8u) |
           static_cast<std::uint16_t>(data[1]);
}

constexpr std::uint32_t read_be_u32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

constexpr void write_le_u16(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8u);
}

constexpr void write_le_i16(std::uint8_t* data, std::int16_t value) noexcept {
    write_le_u16(data, static_cast<std::uint16_t>(value));
}

constexpr void write_le_u32(std::uint8_t* data, std::uint32_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8u);
    data[2] = static_cast<std::uint8_t>(value >> 16u);
    data[3] = static_cast<std::uint8_t>(value >> 24u);
}

constexpr void write_be_u16(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value >> 8u);
    data[1] = static_cast<std::uint8_t>(value);
}

constexpr void write_be_u32(std::uint8_t* data, std::uint32_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value >> 24u);
    data[1] = static_cast<std::uint8_t>(value >> 16u);
    data[2] = static_cast<std::uint8_t>(value >> 8u);
    data[3] = static_cast<std::uint8_t>(value);
}

}  // namespace etrike::protocol
