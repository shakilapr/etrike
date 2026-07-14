#pragma once

#include <cstdint>

#include "protocol/codecs/detail.hpp"

namespace etrike::protocol::codecs::pwt {

inline constexpr std::uint32_t kDcdcCommandId = 0x10262B27u;
inline constexpr std::uint8_t kDcdcCommandDlc = 8;
inline constexpr std::uint8_t kReservedValue = 0xFFu;

struct DcdcCommand {
    bool enabled{true};
    bool reset{false};
};

inline CodecStatus encode_dcdc_command(const DcdcCommand& value, Frame& out) noexcept {
    Frame frame = Frame::extended_frame(kDcdcCommandId, kDcdcCommandDlc);
    frame.data[0] = value.enabled ? 0x01u : 0x00u;
    for (std::size_t index = 1; index <= 6; ++index) frame.data[index] = kReservedValue;
    frame.data[7] = value.reset ? 0x01u : 0x00u;
    out = frame;
    return CodecStatus::Ok;
}

inline CodecStatus decode_dcdc_command(FrameView frame, DcdcCommand& out) noexcept {
    const CodecStatus status =
        detail::validate_frame(frame, kDcdcCommandId, true, kDcdcCommandDlc);
    if (status != CodecStatus::Ok) return status;
    if (frame[0] > 1 || frame[7] > 1) return CodecStatus::InvalidEnum;
    for (std::size_t index = 1; index <= 6; ++index) {
        if (frame[index] != kReservedValue) return CodecStatus::ConstantMismatch;
    }
    DcdcCommand value{};
    value.enabled = frame[0] == 1;
    value.reset = frame[7] == 1;
    out = value;
    return CodecStatus::Ok;
}

}  // namespace etrike::protocol::codecs::pwt
