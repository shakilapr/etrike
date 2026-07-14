#pragma once

#include <cstdint>

namespace etrike::protocol {

enum class CodecStatus : std::uint8_t {
    Ok = 0,
    NullData,
    WrongMessageId,
    WrongFrameFormat,
    UnexpectedLength,
    ChecksumMismatch,
    ValueOutOfRange,
    InvalidEnum,
    ConstantMismatch,
};

constexpr bool succeeded(CodecStatus status) noexcept {
    return status == CodecStatus::Ok;
}

}  // namespace etrike::protocol
