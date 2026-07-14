#pragma once

#include "protocol/compat/can_protocol.hpp"

namespace etrike {
namespace protocol {
namespace compat {

template <typename Message>
inline auto encode_frame(const Message& message, Frame& frame) noexcept
    -> decltype(generated::encode(message, frame)) {
    return generated::encode(message, frame);
}

template <typename Message>
inline auto decode_frame(FrameView frame, Message& message) noexcept
    -> decltype(generated::decode(frame, message)) {
    return generated::decode(frame, message);
}

inline CodecStatus encode_frame(const codecs::ses::Command& value, Frame& frame) noexcept {
    return codecs::ses::encode_command(value, frame);
}

inline CodecStatus decode_frame(FrameView frame, codecs::ses::Command& value) noexcept {
    return codecs::ses::decode_command(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::ses::Status& value) noexcept {
    return codecs::ses::decode_status(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::ses::ErrorInfo& value) noexcept {
    return codecs::ses::decode_error_info(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::ses::VersionRaw& value) noexcept {
    return codecs::ses::decode_version(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::ses::TestTelemetry& value) noexcept {
    return codecs::ses::decode_test(frame, value);
}

inline CodecStatus encode_frame(const codecs::seb::Command& value, Frame& frame) noexcept {
    return codecs::seb::encode_command(value, frame);
}

inline CodecStatus decode_frame(FrameView frame, codecs::seb::Command& value) noexcept {
    return codecs::seb::decode_command(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::seb::Status& value) noexcept {
    return codecs::seb::decode_status(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::seb::ErrorInfo& value) noexcept {
    return codecs::seb::decode_error_info(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::seb::Version& value) noexcept {
    return codecs::seb::decode_version(frame, value);
}

inline CodecStatus decode_frame(FrameView frame, codecs::seb::TestTelemetry& value) noexcept {
    return codecs::seb::decode_test(frame, value);
}

template <typename Message>
inline auto decode_frame(const Frame& frame, Message& message) noexcept
    -> decltype(decode_frame(frame.view(), message)) {
    return decode_frame(frame.view(), message);
}

}  // namespace compat
}  // namespace protocol
}  // namespace etrike

namespace can {
using ::etrike::protocol::compat::decode_frame;
using ::etrike::protocol::compat::encode_frame;
}  // namespace can
