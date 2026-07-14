#pragma once

#include <array>
#include <cstdint>

#include "protocol/codecs/detail.hpp"
#include "protocol/core/endian.hpp"

namespace etrike::protocol::codecs::seb {

inline constexpr std::uint32_t kCommandId = 0x7B9u;
inline constexpr std::uint32_t kStatusId = 0x721u;
inline constexpr std::uint32_t kErrorInfoId = 0x731u;
inline constexpr std::uint32_t kVersionId = 0x741u;
inline constexpr std::uint32_t kTestId = 0x6FBu;
inline constexpr std::uint8_t kDlc = 8;

enum class ControlMode : std::uint8_t { Stroke = 0, Pressure = 1 };

struct Command {
    bool alignment_enable{false};
    bool control_enable{false};
    ControlMode control_mode{ControlMode::Stroke};
    bool auto_brake{false};
    std::uint16_t stroke_request_raw{600};
    std::uint8_t pressure_request_raw{0};
    std::uint8_t rolling_counter{0};
};

inline CodecStatus encode_command(const Command& value, Frame& out) noexcept {
    const auto mode = static_cast<std::uint8_t>(value.control_mode);
    if (mode > 1) return CodecStatus::InvalidEnum;
    if (value.pressure_request_raw > 100 || value.rolling_counter > 15)
        return CodecStatus::ValueOutOfRange;

    Frame frame = Frame::standard(kCommandId, kDlc);
    frame.data[0] = static_cast<std::uint8_t>((value.alignment_enable ? 0x01u : 0u) |
                                              (value.control_enable ? 0x02u : 0u) |
                                              (mode << 2u) |
                                              (value.auto_brake ? 0x08u : 0u));
    frame.data[2] = static_cast<std::uint8_t>(value.stroke_request_raw);
    frame.data[3] = mode == 0 ? static_cast<std::uint8_t>(value.stroke_request_raw >> 8u)
                              : value.pressure_request_raw;
    // Reserved bytes 1, 4, 5 and reserved bits are always zero.
    frame.data[6] = static_cast<std::uint8_t>(0x03u | (value.rolling_counter << 4u));
    frame.data[7] = profiles::xor8_ff_v1(frame.data.data(), 7);
    out = frame;
    return CodecStatus::Ok;
}

inline CodecStatus decode_command(FrameView frame, Command& out) noexcept {
    CodecStatus status = detail::validate_xor_frame(frame, kCommandId);
    if (status != CodecStatus::Ok) return status;
    if ((frame[6] & 0x03u) != 0x03u) return CodecStatus::ConstantMismatch;

    Command value{};
    value.alignment_enable = (frame[0] & 0x01u) != 0;
    value.control_enable = (frame[0] & 0x02u) != 0;
    value.control_mode = (frame[0] & 0x04u) != 0 ? ControlMode::Pressure : ControlMode::Stroke;
    value.auto_brake = (frame[0] & 0x08u) != 0;
    value.rolling_counter = static_cast<std::uint8_t>(frame[6] >> 4u);
    if (value.control_mode == ControlMode::Stroke) {
        value.stroke_request_raw = read_le_u16(frame.data() + 2);
        value.pressure_request_raw = 0;
    } else {
        value.stroke_request_raw = frame[2];
        value.pressure_request_raw = frame[3];
        if (value.pressure_request_raw > 100) return CodecStatus::ValueOutOfRange;
    }
    out = value;
    return CodecStatus::Ok;
}

struct Status {
    std::uint8_t status_byte{0};
    bool alignment_status{false};
    bool control_enabled{false};
    std::uint8_t control_mode{0};
    bool auto_brake_status{false};
    std::uint8_t error_status{0};
    std::uint16_t stroke_value_raw{0};
    // This is the same wire byte as the high byte of stroke_value_raw.
    std::uint8_t pressure_value_raw{0};
    // Byte 6 is shared with integrity status, so this retains the exact overlap.
    std::int16_t angle_value_raw{0};
    bool rolling_counter_enabled{false};
    bool checksum_enabled{false};
    std::uint8_t rolling_counter{0};
};

inline CodecStatus decode_status(FrameView frame, Status& out) noexcept {
    const CodecStatus status = detail::validate_xor_frame(frame, kStatusId);
    if (status != CodecStatus::Ok) return status;
    Status value{};
    value.status_byte = frame[0];
    value.alignment_status = (frame[0] & 0x01u) != 0;
    value.control_enabled = (frame[0] & 0x02u) != 0;
    value.control_mode = static_cast<std::uint8_t>((frame[0] >> 2u) & 0x03u);
    value.auto_brake_status = (frame[0] & 0x10u) != 0;
    value.error_status = static_cast<std::uint8_t>((frame[0] >> 6u) & 0x03u);
    value.stroke_value_raw = read_le_u16(frame.data() + 2);
    value.pressure_value_raw = frame[3];
    value.angle_value_raw = read_le_i16(frame.data() + 5);
    value.rolling_counter_enabled = (frame[6] & 0x01u) != 0;
    value.checksum_enabled = (frame[6] & 0x02u) != 0;
    value.rolling_counter = static_cast<std::uint8_t>(frame[6] >> 4u);
    out = value;
    return CodecStatus::Ok;
}

struct ErrorInfo {
    std::array<std::uint8_t, 8> raw{};
};

inline CodecStatus decode_error_info(FrameView frame, ErrorInfo& out) noexcept {
    const CodecStatus status = detail::validate_frame(frame, kErrorInfoId, false, kDlc);
    if (status != CodecStatus::Ok) return status;
    ErrorInfo value{};
    for (std::size_t index = 0; index < value.raw.size(); ++index) value.raw[index] = frame[index];
    out = value;
    return CodecStatus::Ok;
}

struct Version {
    std::uint8_t software_raw{0};
    std::uint8_t hardware_raw{0};
    std::array<std::uint8_t, 8> raw{};
};

inline CodecStatus decode_version(FrameView frame, Version& out) noexcept {
    const CodecStatus status = detail::validate_frame(frame, kVersionId, false, kDlc);
    if (status != CodecStatus::Ok) return status;
    Version value{};
    for (std::size_t index = 0; index < value.raw.size(); ++index) value.raw[index] = frame[index];
    value.software_raw = frame[0];
    value.hardware_raw = frame[1];
    out = value;
    return CodecStatus::Ok;
}

struct TestTelemetry {
    std::int16_t motor_current_raw{0};
    std::uint16_t ecu_temperature_raw{0};
    std::uint16_t supply_voltage_raw{0};
};

inline CodecStatus decode_test(FrameView frame, TestTelemetry& out) noexcept {
    const CodecStatus status = detail::validate_frame(frame, kTestId, false, kDlc);
    if (status != CodecStatus::Ok) return status;
    TestTelemetry value{};
    value.motor_current_raw = read_le_i16(frame.data() + 1);
    value.ecu_temperature_raw = read_le_u16(frame.data() + 3);
    value.supply_voltage_raw = read_le_u16(frame.data() + 5);
    out = value;
    return CodecStatus::Ok;
}

}  // namespace etrike::protocol::codecs::seb
