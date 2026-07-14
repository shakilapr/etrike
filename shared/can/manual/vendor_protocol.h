#pragma once
// CAN-MAPPING: CAN-MANUAL-SES-* / CAN-MANUAL-SEB-*
// SOURCE: shared/can/manual-mappings.yaml

#include "can/codec_transport.h"

namespace can::manual {
inline uint8_t vendor_checksum(const uint8_t* data, size_t count) noexcept {
    uint8_t value = 0;
    for (size_t i = 0; i < count; ++i) value ^= data[i];
    return static_cast<uint8_t>(value ^ 0xFFu);
}

template <typename Message>
inline gen::CodecStatus validate(const Frame& frame, bool checksum) noexcept {
    if (frame.id != Message::kId) return gen::CodecStatus::WrongMessageId;
    if (frame.extended != Message::kExtended) return gen::CodecStatus::WrongFrameFormat;
    if (frame.dlc != Message::kDlc) return gen::CodecStatus::UnexpectedLength;
    if (checksum && vendor_checksum(frame.data, Message::kDlc - 1u) != frame.data[Message::kDlc - 1u])
        return gen::CodecStatus::ChecksumMismatch;
    return gen::CodecStatus::Ok;
}

struct SesStatusValue { bool angle_status{}; uint8_t error_status{}; uint16_t steering_angle{}; uint8_t rolling_counter{}; };
inline gen::CodecStatus decode_ses_status(const Frame& frame, SesStatusValue& out) noexcept {
    auto status = validate<gen::SesStatus>(frame, true);
    if (status != gen::CodecStatus::Ok) return status;
    SesStatusValue value{};
    value.angle_status = (frame.data[0] & 1u) != 0;
    value.error_status = uint8_t((frame.data[0] >> 6u) & 3u);
    value.steering_angle = uint16_t(frame.data[2] | (uint16_t(frame.data[3]) << 8u));
    value.rolling_counter = uint8_t((frame.data[6] >> 4u) & 0x0Fu);
    out = value;
    return gen::CodecStatus::Ok;
}

struct SebStatusValue { uint8_t status_byte{}; bool alignment_status{}; uint8_t control_mode{}; uint8_t error_status{}; uint16_t stroke_value{}; uint8_t pressure_value{}; uint8_t rolling_counter{}; };
inline gen::CodecStatus decode_seb_status(const Frame& frame, SebStatusValue& out) noexcept {
    auto status = validate<gen::SebStatus>(frame, true);
    if (status != gen::CodecStatus::Ok) return status;
    SebStatusValue value{};
    value.status_byte = frame.data[0];
    value.alignment_status = (frame.data[0] & 1u) != 0;
    value.control_mode = uint8_t((frame.data[0] >> 2u) & 3u);
    value.error_status = uint8_t((frame.data[0] >> 6u) & 3u);
    value.stroke_value = uint16_t(frame.data[2] | (uint16_t(frame.data[3]) << 8u));
    value.pressure_value = frame.data[3];
    value.rolling_counter = uint8_t((frame.data[6] >> 4u) & 0x0Fu);
    out = value;
    return gen::CodecStatus::Ok;
}

struct TestTelemetry { int16_t motor_current{}; uint16_t ecu_temperature{}; uint16_t supply_voltage{}; };
template <typename Message>
inline gen::CodecStatus decode_test(const Frame& frame, TestTelemetry& out) noexcept {
    auto status = validate<Message>(frame, false);
    if (status != gen::CodecStatus::Ok) return status;
    TestTelemetry value{};
    value.motor_current = int16_t(frame.data[1] | (uint16_t(frame.data[2]) << 8u));
    value.ecu_temperature = uint16_t(frame.data[3] | (uint16_t(frame.data[4]) << 8u));
    value.supply_voltage = uint16_t(frame.data[5] | (uint16_t(frame.data[6]) << 8u));
    out = value;
    return gen::CodecStatus::Ok;
}

inline gen::CodecStatus encode(const VcuSesReq& value, Frame& frame) noexcept { value.to_frame(frame); return validate<gen::VcuSesReq>(frame, true); }
inline gen::CodecStatus encode(const VcuSebReq& value, Frame& frame) noexcept { value.to_frame(frame); return validate<gen::VcuSebReq>(frame, true); }
} // namespace can::manual
