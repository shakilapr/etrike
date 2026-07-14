#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace etrike {
namespace protocol {

constexpr std::size_t kClassicCanPayloadSize = 8;
constexpr std::uint32_t kStandardCanIdMax = 0x7FFu;
constexpr std::uint32_t kExtendedCanIdMax = 0x1FFFFFFFu;

class FrameView;

struct Frame {
    constexpr Frame(std::uint32_t can_id = 0, bool is_extended = false,
                    std::uint8_t length = 0) noexcept
        : id(can_id), extended(is_extended), dlc(length), data() {}

    std::uint32_t id;
    bool extended;
    std::uint8_t dlc;
    std::array<std::uint8_t, kClassicCanPayloadSize> data;

    static constexpr Frame standard(std::uint32_t can_id, std::uint8_t length) noexcept {
        return Frame(can_id, false, length);
    }

    static constexpr Frame extended_frame(std::uint32_t can_id, std::uint8_t length) noexcept {
        return Frame(can_id, true, length);
    }

    FrameView view() const noexcept;
};

// FrameView never owns or permits mutation of the payload it references.
class FrameView {
public:
    constexpr FrameView(std::uint32_t id, bool extended, std::uint8_t dlc,
                        const std::uint8_t* data,
                        std::size_t capacity = kClassicCanPayloadSize) noexcept
        : id_(id), extended_(extended), dlc_(dlc), data_(data), capacity_(capacity) {}

    FrameView(const Frame& frame) noexcept
        : FrameView(frame.id, frame.extended, frame.dlc, frame.data.data(), frame.data.size()) {}

    constexpr std::uint32_t id() const noexcept { return id_; }
    constexpr bool extended() const noexcept { return extended_; }
    constexpr std::uint8_t dlc() const noexcept { return dlc_; }
    constexpr const std::uint8_t* data() const noexcept { return data_; }
    constexpr std::size_t capacity() const noexcept { return capacity_; }
    constexpr bool has_data() const noexcept { return data_ != nullptr; }
    constexpr std::uint8_t operator[](std::size_t index) const noexcept { return data_[index]; }

private:
    std::uint32_t id_;
    bool extended_;
    std::uint8_t dlc_;
    const std::uint8_t* data_;
    std::size_t capacity_;
};

inline FrameView Frame::view() const noexcept {
    return FrameView(*this);
}

inline constexpr bool is_valid_frame(FrameView frame) noexcept {
    return frame.dlc() <= kClassicCanPayloadSize && frame.dlc() <= frame.capacity() &&
           frame.id() <= (frame.extended() ? kExtendedCanIdMax : kStandardCanIdMax) &&
           (frame.dlc() == 0 || frame.has_data());
}

inline bool copy_frame(FrameView source, Frame& destination) noexcept {
    if (!is_valid_frame(source)) return false;
    Frame value(source.id(), source.extended(), source.dlc());
    for (std::size_t index = 0; index < source.dlc(); ++index) value.data[index] = source[index];
    destination = value;
    return true;
}

}  // namespace protocol
}  // namespace etrike
