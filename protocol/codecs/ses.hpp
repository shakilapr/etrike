#pragma once

#include <array>
#include <cstdint>

#include "protocol/codecs/detail.hpp"
#include "protocol/core/endian.hpp"

namespace etrike::protocol::codecs::ses {

inline constexpr std::uint32_t kCommandId = 0x169u;
inline constexpr std::uint32_t kStatusId = 0x201u;
inline constexpr std::uint32_t kErrorInfoId = 0x202u;
inline constexpr std::uint32_t kVersionId = 0x203u;
inline constexpr std::uint32_t kTestId = 0x6FAu;
inline constexpr std::uint8_t kDlc = 8;

struct Command {
    bool alignment_enable{false};
    bool control_enable{false};
    std::int16_t target_angle_raw{0};
    std::uint16_t target_speed_raw{328};
    std::uint8_t rolling_counter{0};
    std::uint8_t vehicle_speed_raw{0};
};

inline CodecStatus encode_command(const Command& value, Frame& out) noexcept {
    if (value.target_speed_raw < 125 || value.target_speed_raw > 525 ||
        value.rolling_counter > 15)
        return CodecStatus::ValueOutOfRange;

    Frame frame = Frame::standard(kCommandId, kDlc);
    frame.data[0] = static_cast<std::uint8_t>((value.alignment_enable ? 0x01u : 0u) |
                                              (value.control_enable ? 0x02u : 0u));
    // Bytes 1 and all unused bits remain zero from value initialization.
    write_le_i16(&frame.data[2], value.target_angle_raw);
    frame.data[4] = static_cast<std::uint8_t>(value.target_speed_raw);
    frame.data[5] = static_cast<std::uint8_t>(0x03u |
                                              ((value.target_speed_raw >> 6u) & 0x0Cu) |
                                              (value.rolling_counter << 4u));
    frame.data[6] = value.vehicle_speed_raw;
    frame.data[7] = profiles::xor8_ff_v1(frame.data.data(), 7);
    out = frame;
    return CodecStatus::Ok;
}

inline CodecStatus decode_command(FrameView frame, Command& out) noexcept {
    CodecStatus status = detail::validate_xor_frame(frame, kCommandId);
    if (status != CodecStatus::Ok) return status;
    if ((frame[5] & 0x03u) != 0x03u) return CodecStatus::ConstantMismatch;

    Command value{};
    value.alignment_enable = (frame[0] & 0x01u) != 0;
    value.control_enable = (frame[0] & 0x02u) != 0;
    value.target_angle_raw = read_le_i16(frame.data() + 2);
    value.target_speed_raw = static_cast<std::uint16_t>(frame[4]) |
                             static_cast<std::uint16_t>((frame[5] & 0x0Cu) << 6u);
    value.rolling_counter = static_cast<std::uint8_t>(frame[5] >> 4u);
    value.vehicle_speed_raw = frame[6];
    if (value.target_speed_raw < 125 || value.target_speed_raw > 525)
        return CodecStatus::ValueOutOfRange;
    out = value;
    return CodecStatus::Ok;
}

struct Status {
    bool angle_aligned{false};
    std::uint8_t control_mode{0};
    std::uint8_t error_status{0};
    std::uint16_t steering_angle_raw{0};
    std::int16_t target_angle_speed_raw{0};
    // This is the same wire byte as the high byte of target_angle_speed_raw.
    std::uint8_t steering_torque_raw{0};
    bool rolling_counter_enabled{false};
    bool checksum_enabled{false};
    std::uint8_t rolling_counter{0};
};

inline CodecStatus decode_status(FrameView frame, Status& out) noexcept {
    const CodecStatus status = detail::validate_xor_frame(frame, kStatusId);
    if (status != CodecStatus::Ok) return status;
    Status value{};
    value.angle_aligned = (frame[0] & 0x01u) != 0;
    value.control_mode = static_cast<std::uint8_t>((frame[0] >> 1u) & 0x03u);
    value.error_status = static_cast<std::uint8_t>((frame[0] >> 6u) & 0x03u);
    value.steering_angle_raw = read_le_u16(frame.data() + 2);
    value.target_angle_speed_raw = read_le_i16(frame.data() + 4);
    value.steering_torque_raw = frame[5];
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

// The repository records the SES version byte interpretation as vendor-unconfirmed.
struct VersionRaw {
    std::array<std::uint8_t, 8> raw{};
};

inline CodecStatus decode_version(FrameView frame, VersionRaw& out) noexcept {
    const CodecStatus status = detail::validate_frame(frame, kVersionId, false, kDlc);
    if (status != CodecStatus::Ok) return status;
    VersionRaw value{};
    for (std::size_t index = 0; index < value.raw.size(); ++index) value.raw[index] = frame[index];
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

}  // namespace etrike::protocol::codecs::ses
