#include <array>
#include <cstdint>
#include <cstdio>

#include "protocol/codecs/pwt.hpp"
#include "protocol/codecs/seb.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/core/bits.hpp"
#include "protocol/core/endian.hpp"
#include "protocol/core/supervision.hpp"

namespace {

int failures = 0;

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);              \
            ++failures;                                                                      \
        }                                                                                    \
    } while (false)

using etrike::protocol::CodecStatus;
using etrike::protocol::CounterEvent;
using etrike::protocol::Frame;
using etrike::protocol::FreshnessEvent;
using etrike::protocol::TrackerKey;

void test_core() {
    std::array<std::uint8_t, 4> bytes{};
    etrike::protocol::write_le_u16(bytes.data(), 0xABCDu);
    CHECK(bytes[0] == 0xCD && bytes[1] == 0xAB);
    CHECK(etrike::protocol::read_le_u16(bytes.data()) == 0xABCDu);
    etrike::protocol::write_be_u32(bytes.data(), 0x12345678u);
    CHECK(etrike::protocol::read_be_u32(bytes.data()) == 0x12345678u);
    CHECK(etrike::protocol::extract_bits(0xB4u, 2, 3) == 5u);
    CHECK(etrike::protocol::insert_bits(0u, 5u, 2, 3) == 0x14u);

    const Frame frame = Frame::standard(0x123, 2);
    const auto view = frame.view();
    CHECK(view.id() == 0x123 && !view.extended() && view.dlc() == 2);
}

void test_profile() {
    constexpr std::uint8_t bytes[] = {0x03, 0x00, 0x85, 0xFF, 0x48, 0xA7, 0x12};
    static_assert(etrike::protocol::profiles::xor8_ff_v1(bytes, 7) == 0x7B);
    CHECK(etrike::protocol::profiles::verify_xor8_ff_v1(bytes, 7, 0x7B));
}

void test_ses() {
    namespace ses = etrike::protocol::codecs::ses;
    ses::Command command{};
    command.alignment_enable = true;
    command.control_enable = true;
    command.target_angle_raw = 30000;
    command.target_speed_raw = 125;
    command.rolling_counter = 5;
    command.vehicle_speed_raw = 5;

    Frame frame{};
    CHECK(ses::encode_command(command, frame) == CodecStatus::Ok);
    const std::array<std::uint8_t, 8> expected{0x03, 0x00, 0x30, 0x75,
                                                0x7D, 0x53, 0x05, 0x92};
    CHECK(frame.id == ses::kCommandId && !frame.extended && frame.dlc == 8);
    CHECK(frame.data == expected);

    ses::Command decoded{};
    CHECK(ses::decode_command(frame.view(), decoded) == CodecStatus::Ok);
    CHECK(decoded.target_angle_raw == 30000 && decoded.target_speed_raw == 125);
    CHECK(decoded.rolling_counter == 5 && decoded.vehicle_speed_raw == 5);

    const ses::Command unchanged = decoded;
    frame.data[7] ^= 1;
    CHECK(ses::decode_command(frame.view(), decoded) == CodecStatus::ChecksumMismatch);
    CHECK(decoded.target_angle_raw == unchanged.target_angle_raw);
    frame.data[7] ^= 1;
    frame.extended = true;
    CHECK(ses::decode_command(frame.view(), decoded) == CodecStatus::WrongFrameFormat);
    frame.extended = false;
    frame.dlc = 7;
    CHECK(ses::decode_command(frame.view(), decoded) == CodecStatus::UnexpectedLength);

    Frame preserved = Frame::extended_frame(0x55, 3);
    preserved.data.fill(0xCC);
    Frame output = preserved;
    command.target_speed_raw = 600;
    CHECK(ses::encode_command(command, output) == CodecStatus::ValueOutOfRange);
    CHECK(output.id == preserved.id && output.data == preserved.data);

    Frame status = Frame::standard(ses::kStatusId, 8);
    status.data = {0x41, 0x00, 0x30, 0x75, 0x10, 0x00, 0xA3, 0x48};
    ses::Status status_value{};
    CHECK(ses::decode_status(status.view(), status_value) == CodecStatus::Ok);
    CHECK(status_value.angle_aligned && status_value.error_status == 1);
    CHECK(status_value.steering_angle_raw == 30000);
    CHECK(status_value.target_angle_speed_raw == 0x0010);
    CHECK(status_value.steering_torque_raw == 0 && status_value.rolling_counter == 10);

    Frame error = Frame::standard(ses::kErrorInfoId, 8);
    error.data = {0x01, 0x80, 0x04, 0x01, 0x00, 0x00, 0x00, 0x2A};
    ses::ErrorInfo error_value{};
    CHECK(ses::decode_error_info(error.view(), error_value) == CodecStatus::Ok);
    CHECK(error_value.raw == error.data);

    Frame telemetry = Frame::standard(ses::kTestId, 8);
    telemetry.data = {0x00, 0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A, 0x00};
    ses::TestTelemetry telemetry_value{};
    CHECK(ses::decode_test(telemetry.view(), telemetry_value) == CodecStatus::Ok);
    CHECK(telemetry_value.motor_current_raw == 0x1234);
    CHECK(telemetry_value.ecu_temperature_raw == 0x5678);
    CHECK(telemetry_value.supply_voltage_raw == 0x9ABC);

    Frame version = Frame::standard(ses::kVersionId, 8);
    version.data = {0x64, 0x0D, 1, 2, 3, 4, 5, 6};
    ses::VersionRaw raw_version{};
    CHECK(ses::decode_version(version.view(), raw_version) == CodecStatus::Ok);
    CHECK(raw_version.raw == version.data);
}

void test_seb() {
    namespace seb = etrike::protocol::codecs::seb;
    seb::Command command{};
    command.control_enable = true;
    command.control_mode = seb::ControlMode::Pressure;
    command.stroke_request_raw = 0;
    command.pressure_request_raw = 80;
    command.rolling_counter = 5;

    Frame frame{};
    CHECK(seb::encode_command(command, frame) == CodecStatus::Ok);
    const std::array<std::uint8_t, 8> expected{0x06, 0x00, 0x00, 0x50,
                                                0x00, 0x00, 0x53, 0xFA};
    CHECK(frame.data == expected);

    seb::Command decoded{};
    CHECK(seb::decode_command(frame.view(), decoded) == CodecStatus::Ok);
    CHECK(decoded.control_mode == seb::ControlMode::Pressure);
    CHECK(decoded.stroke_request_raw == 0 && decoded.pressure_request_raw == 80);

    Frame status = Frame::standard(seb::kStatusId, 8);
    status.data = {0x55, 0x00, 0x34, 0x12, 0x00, 0x78, 0xA3, 0x00};
    status.data[7] = etrike::protocol::profiles::xor8_ff_v1(status.data.data(), 7);
    seb::Status value{};
    CHECK(seb::decode_status(status.view(), value) == CodecStatus::Ok);
    CHECK(value.status_byte == 0x55 && value.alignment_status && value.control_mode == 1);
    CHECK(value.error_status == 1 && value.stroke_value_raw == 0x1234);
    CHECK(value.pressure_value_raw == 0x12 && value.angle_value_raw ==
                                                     static_cast<std::int16_t>(0xA378));
    CHECK(value.rolling_counter == 10 && value.rolling_counter_enabled);

    const seb::Status unchanged = value;
    status.id = 0x720;
    CHECK(seb::decode_status(status.view(), value) == CodecStatus::WrongMessageId);
    CHECK(value.stroke_value_raw == unchanged.stroke_value_raw);

    Frame error = Frame::standard(seb::kErrorInfoId, 8);
    error.data = {0x84, 0xA6, 0x2C, 0x03, 0x00, 0x00, 0x00, 0x00};
    seb::ErrorInfo error_value{};
    CHECK(seb::decode_error_info(error.view(), error_value) == CodecStatus::Ok);
    CHECK(error_value.raw == error.data);

    Frame version = Frame::standard(seb::kVersionId, 8);
    version.data = {0xC8, 0x0D, 0, 0, 0, 0, 0, 0};
    seb::Version version_value{};
    CHECK(seb::decode_version(version.view(), version_value) == CodecStatus::Ok);
    CHECK(version_value.software_raw == 0xC8 && version_value.hardware_raw == 0x0D);

    Frame telemetry = Frame::standard(seb::kTestId, 8);
    telemetry.data = {0x00, 0xFE, 0xFF, 0x50, 0x00, 0x00, 0x18, 0x00};
    seb::TestTelemetry telemetry_value{};
    CHECK(seb::decode_test(telemetry.view(), telemetry_value) == CodecStatus::Ok);
    CHECK(telemetry_value.motor_current_raw == -2);
    CHECK(telemetry_value.ecu_temperature_raw == 0x0050);
    CHECK(telemetry_value.supply_voltage_raw == 0x1800);
}

void test_pwt() {
    namespace pwt = etrike::protocol::codecs::pwt;
    pwt::DcdcCommand command{true, false};
    Frame frame{};
    CHECK(pwt::encode_dcdc_command(command, frame) == CodecStatus::Ok);
    CHECK(frame.id == pwt::kDcdcCommandId && frame.extended && frame.dlc == 8);
    CHECK(frame.data == (std::array<std::uint8_t, 8>{1, 0xFF, 0xFF, 0xFF,
                                                      0xFF, 0xFF, 0xFF, 0}));
    pwt::DcdcCommand decoded{};
    CHECK(pwt::decode_dcdc_command(frame.view(), decoded) == CodecStatus::Ok);
    CHECK(decoded.enabled && !decoded.reset);
    const pwt::DcdcCommand unchanged = decoded;
    frame.data[3] = 0;
    CHECK(pwt::decode_dcdc_command(frame.view(), decoded) == CodecStatus::ConstantMismatch);
    CHECK(decoded.enabled == unchanged.enabled && decoded.reset == unchanged.reset);
    frame.data[3] = 0xFF;
    frame.extended = false;
    CHECK(pwt::decode_dcdc_command(frame.view(), decoded) == CodecStatus::WrongFrameFormat);
}

void test_counter_tracker() {
    etrike::protocol::CounterTracker tracker({16, 4});
    const TrackerKey key{1, 0x721, 20, 100};
    CHECK(tracker.observe(key, 14).event == CounterEvent::First);
    CHECK(tracker.observe(key, 15).event == CounterEvent::Increment);
    const auto wrap = tracker.observe(key, 0);
    CHECK(wrap.event == CounterEvent::Wrap && wrap.wrapped);
    CHECK(tracker.observe(key, 0).event == CounterEvent::Duplicate);
    CHECK(tracker.observe(key, 0).event == CounterEvent::Frozen);
    CHECK(tracker.observe(key, 1).event == CounterEvent::Recovery);
    const auto gap = tracker.observe(key, 4);
    CHECK(gap.event == CounterEvent::Gap && gap.missed == 2);
    CHECK(tracker.observe(key, 5).event == CounterEvent::Recovery);
    CHECK(tracker.observe(key, 4).event == CounterEvent::Reorder);
    CHECK(tracker.observe(key, 6).event == CounterEvent::Recovery);
    tracker.reset(key);
    CHECK(tracker.observe(key, 9).event == CounterEvent::Reset);

    TrackerKey next_session = key;
    next_session.session_epoch++;
    CHECK(tracker.observe(next_session, 9).event == CounterEvent::First);
    TrackerKey next_bus = key;
    next_bus.bus++;
    CHECK(tracker.observe(next_bus, 1).event == CounterEvent::First);
}

void test_freshness_tracker() {
    etrike::protocol::FreshnessTracker tracker(50);
    const TrackerKey key{1, 0x201, 10, 7};
    CHECK(tracker.check(key, 0).event == FreshnessEvent::NeverSeen);
    CHECK(tracker.observe(key, 100).event == FreshnessEvent::First);
    CHECK(tracker.check(key, 150).event == FreshnessEvent::Fresh);
    CHECK(tracker.check(key, 151).event == FreshnessEvent::Expired);
    CHECK(tracker.check(key, 160).event == FreshnessEvent::Stale);
    CHECK(tracker.observe(key, 161).event == FreshnessEvent::Recovery);
    CHECK(tracker.observe(key, 160).event == FreshnessEvent::TimeReorder);
    CHECK(tracker.observe(key, 162).event == FreshnessEvent::Recovery);
    tracker.reset(key);
    CHECK(tracker.observe(key, 200).event == FreshnessEvent::Reset);

    TrackerKey new_epoch = key;
    new_epoch.session_epoch++;
    CHECK(tracker.observe(new_epoch, 1).event == FreshnessEvent::First);
}

}  // namespace

int main() {
    test_core();
    test_profile();
    test_ses();
    test_seb();
    test_pwt();
    test_counter_tracker();
    test_freshness_tracker();
    std::printf("protocol tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
