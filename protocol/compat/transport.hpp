#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "protocol/core/frame.hpp"

namespace etrike {
namespace protocol {
namespace compat {

namespace detail {

template <std::size_t Size>
inline const std::uint8_t* payload_data(const std::uint8_t (&data)[Size]) noexcept {
    return data;
}

template <std::size_t Size>
inline const std::uint8_t* payload_data(const std::array<std::uint8_t, Size>& data) noexcept {
    return data.data();
}

template <std::size_t Size>
inline constexpr std::size_t payload_size(const std::uint8_t (&)[Size]) noexcept {
    return Size;
}

template <std::size_t Size>
inline constexpr std::size_t payload_size(const std::array<std::uint8_t, Size>&) noexcept {
    return Size;
}

}  // namespace detail

// FrameLike is intentionally structural: existing drivers only need id,
// extended, dlc, and an eight-byte C array or std::array payload.
template <typename FrameLike>
inline FrameView frame_view(const FrameLike& frame) noexcept {
    return FrameView(static_cast<std::uint32_t>(frame.id), static_cast<bool>(frame.extended),
                     static_cast<std::uint8_t>(frame.dlc), detail::payload_data(frame.data),
                     detail::payload_size(frame.data));
}

inline FrameView frame_view(const Frame& frame) noexcept {
    return frame.view();
}

template <typename FrameLike>
inline bool to_protocol_frame(const FrameLike& source, Frame& destination) noexcept {
    return copy_frame(frame_view(source), destination);
}

inline bool to_protocol_frame(FrameView source, Frame& destination) noexcept {
    return copy_frame(source, destination);
}

template <typename FrameLike>
inline bool from_protocol_frame(FrameView source, FrameLike& destination) noexcept {
    if (!is_valid_frame(source)) return false;
    FrameLike value{};
    if (detail::payload_size(value.data) < source.dlc()) return false;
    value.id = source.id();
    value.extended = source.extended();
    value.dlc = source.dlc();
    for (std::size_t index = 0; index < source.dlc(); ++index) value.data[index] = source[index];
    destination = value;
    return true;
}

template <typename FrameLike>
inline bool from_protocol_frame(const Frame& source, FrameLike& destination) noexcept {
    return from_protocol_frame(source.view(), destination);
}

}  // namespace compat
}  // namespace protocol
}  // namespace etrike
