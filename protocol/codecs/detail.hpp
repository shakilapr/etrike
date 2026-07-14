#pragma once

#include <cstdint>

#include "protocol/core/codec_status.hpp"
#include "protocol/core/frame.hpp"
#include "protocol/profiles/xor8_ff_v1.hpp"

namespace etrike::protocol::codecs::detail {

inline CodecStatus validate_frame(FrameView frame, std::uint32_t id, bool extended,
                                  std::uint8_t dlc) noexcept {
    if (frame.id() != id) return CodecStatus::WrongMessageId;
    if (frame.extended() != extended) return CodecStatus::WrongFrameFormat;
    if (frame.dlc() != dlc) return CodecStatus::UnexpectedLength;
    if (!frame.has_data()) return CodecStatus::NullData;
    return CodecStatus::Ok;
}

inline CodecStatus validate_xor_frame(FrameView frame, std::uint32_t id) noexcept {
    const CodecStatus status = validate_frame(frame, id, false, 8);
    if (status != CodecStatus::Ok) return status;
    if (!profiles::verify_xor8_ff_v1(frame.data(), 7, frame[7]))
        return CodecStatus::ChecksumMismatch;
    return CodecStatus::Ok;
}

}  // namespace etrike::protocol::codecs::detail
