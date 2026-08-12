#include <cstdint>
#include <cstdio>
#include <cstring>

#include "protocol/compat/can.hpp"

namespace {

int failures = 0;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

void test_constants_and_names() {
    static_assert(can::kIdSafetyEstop == can::gen::SafetyEstop::kId);
    static_assert(can::kIdHostDriveCmd == can::gen::HostDriveCmd::kId);
    static_assert(can::kIdHostSteerCmd == can::gen::HostSteerCmd::kId);
    static_assert(can::kIdRtMotionRpt == can::gen::RtMotionRpt::kId);
    static_assert(can::kIdPwtDcdcCmd == can::gen::PwtDcdcCmd::kId);
    static_assert(can::kIdVcuSesReq == can::custom::ses::kCommandId);
    static_assert(can::kIdSebStatus == can::custom::seb::kStatusId);
    static_assert(can::kIdSbwStatus == can::kIdSesStatus);
    CHECK(std::strcmp(can::mode_name(can::Mode::Auto), "AUTO") == 0);
    CHECK(std::strcmp(can::gear_name(can::Gear::R), "R") == 0);
    CHECK(std::strcmp(can::mode_name(static_cast<can::Mode>(99)), "?") == 0);
}

void test_generated_routes() {
    CHECK(can::is_forwarded_low_to_high(can::kIdSafetyEstop));
    CHECK(can::is_forwarded_low_to_high(can::kIdSysSafetySts));
    CHECK(can::is_forwarded_low_to_high(can::kIdMtrMotorFbk));
    CHECK(can::is_forwarded_high_to_low(can::kIdHmiModeReq));
    CHECK(can::is_forwarded_high_to_low(can::kIdHostLightCmd));
    CHECK(!can::is_forwarded_low_to_high(can::kIdRtDriveCmd));
    CHECK(!can::is_forwarded(can::kIdPwtDcdcCmd, true, can::Bus::Powertrain,
                             can::Bus::Low));
    CHECK(can::is_known_frame_on_bus(
        can::kIdSysHeartbeat, false, can::gen::SysHeartbeat::kDlc, can::Bus::Low));
    CHECK(!can::is_known_frame_on_bus(
        can::kIdSysHeartbeat, false, can::gen::SysHeartbeat::kDlc + 1, can::Bus::Low));
    CHECK(!can::is_known_frame_on_bus(0x7FFu, false, 0, can::Bus::Low));
}

void test_generated_adapters() {
    can::gen::HostDriveCmd command{};
    command.speed_mmps = 1000;
    command.yaw_rate_mrad_s = -2;
    command.gear = static_cast<std::uint8_t>(can::Gear::D);
    can::Frame frame;
    CHECK(can::encode_frame(command, frame) == can::gen::CodecStatus::Ok);
    CHECK(frame.id == can::kIdHostDriveCmd && frame.dlc == 8u);

    can::gen::HostDriveCmd decoded{};
    CHECK(can::decode_frame(frame, decoded) == can::gen::CodecStatus::Ok);
    CHECK(decoded.speed_mmps == 1000 && decoded.yaw_rate_mrad_s == -2 &&
          decoded.gear == static_cast<std::uint8_t>(can::Gear::D));

    frame.dlc = 7;
    CHECK(can::decode_frame(frame, decoded) == can::gen::CodecStatus::UnexpectedLength);
}

void test_custom_adapters() {
    can::custom::ses::Command command{};
    command.control_enable = true;
    command.target_angle_raw = 30000;
    command.target_speed_raw = 125;
    command.rolling_counter = 5;
    can::Frame frame;
    CHECK(can::encode_frame(command, frame) == can::gen::CodecStatus::Ok);
    CHECK(frame.id == can::kIdVcuSesReq && frame.data[7] == 0x96u);

    can::custom::ses::Command decoded{};
    CHECK(can::decode_frame(frame, decoded) == can::gen::CodecStatus::Ok);
    CHECK(decoded.target_angle_raw == 30000 && decoded.rolling_counter == 5);
    frame.data[7] ^= 1u;
    CHECK(can::decode_frame(frame, decoded) == can::gen::CodecStatus::ChecksumMismatch);

    can::custom::seb::Command brake{};
    brake.control_enable = true;
    brake.control_mode = can::custom::seb::ControlMode::Pressure;
    brake.pressure_request_raw = 80;
    CHECK(can::encode_frame(brake, frame) == can::gen::CodecStatus::Ok);
    can::custom::seb::Command decoded_brake{};
    CHECK(can::decode_frame(frame.view(), decoded_brake) == can::gen::CodecStatus::Ok);
    CHECK(decoded_brake.pressure_request_raw == 80);
}

}  // namespace

int main() {
    test_constants_and_names();
    test_generated_routes();
    test_generated_adapters();
    test_custom_adapters();
    std::printf("compat tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
