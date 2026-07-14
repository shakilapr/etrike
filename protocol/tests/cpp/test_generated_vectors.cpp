#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "protocol/generated/cpp/etrike_protocol.hpp"

namespace {

int failures = 0;

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);              \
            ++failures;                                                                      \
        }                                                                                    \
    } while (false)

namespace generated = etrike::protocol::generated;
using etrike::protocol::CodecStatus;
using etrike::protocol::Frame;

template <typename Message, std::size_t Size>
void check_vector(const Message& value, const std::array<std::uint8_t, Size>& expected) {
    static_assert(Size == Message::kDlc);
    Frame frame = Frame::extended_frame(0x55u, 3);
    frame.data.fill(0xCCu);
    CHECK(generated::encode(value, frame) == CodecStatus::Ok);
    CHECK(frame.id == Message::kId);
    CHECK(frame.extended == Message::kExtended);
    CHECK(frame.dlc == Message::kDlc);
    for (std::size_t index = 0; index < Size; ++index) CHECK(frame.data[index] == expected[index]);
    std::printf("VECTOR %.*s ", static_cast<int>(Message::kKey.size()), Message::kKey.data());
    if constexpr (Size == 0) std::printf("-");
    for (std::size_t index = 0; index < Size; ++index) std::printf("%02x", frame.data[index]);
    std::printf("\n");

    Message decoded{};
    CHECK(generated::decode(frame.view(), decoded) == CodecStatus::Ok);
    Frame round_trip{};
    CHECK(generated::encode(decoded, round_trip) == CodecStatus::Ok);
    CHECK(round_trip.id == frame.id && round_trip.extended == frame.extended &&
          round_trip.dlc == frame.dlc && round_trip.data == frame.data);
}

void test_success_vectors() {
    check_vector(generated::SafetyEstop{}, std::array<std::uint8_t, 0>{});

    generated::SysSafetySts safety{};
    safety.estop_active = true;
    safety.heartbeat_ok = true;
    safety.light_left = true;
    safety.light_brake = true;
    safety.light_head = true;
    check_vector(safety, std::array<std::uint8_t, 3>{0x01, 0x01, 0x0D});

    generated::HmiModeReq hmi_mode{};
    hmi_mode.req_mode = true;
    hmi_mode.rolling_counter = 42;
    check_vector(hmi_mode, std::array<std::uint8_t, 2>{0x01, 0x2A});

    generated::HmiPwrReq hmi_power{};
    hmi_power.req_start = true;
    hmi_power.rolling_counter = 7;
    check_vector(hmi_power, std::array<std::uint8_t, 2>{0x01, 0x07});

    generated::SysThrottleSts throttle{};
    throttle.speed_mmps = -2;
    check_vector(throttle, std::array<std::uint8_t, 2>{0xFF, 0xFE});

    generated::MtrMotorFbk motor{};
    motor.actual_speed_mmps = -2;
    motor.gear_state = 3;
    motor.fault_flags = 17;
    check_vector(motor, std::array<std::uint8_t, 4>{0xFF, 0xFE, 0x03, 0x11});

    generated::HostDriveCmd host_drive{};
    host_drive.speed_mmps = 1000;
    host_drive.yaw_rate_mrad_s = -2;
    host_drive.gear = 3;
    check_vector(host_drive, std::array<std::uint8_t, 8>{0x00, 0x00, 0x03, 0xE8,
                                                         0xFF, 0xFF, 0xFE, 0x03});

    generated::HostBrakeReq host_brake{};
    host_brake.brake_pressure_kpa = 1234;
    check_vector(host_brake, std::array<std::uint8_t, 4>{0x00, 0x00, 0x04, 0xD2});

    generated::HostLightCmd lights{};
    lights.left_turn = true;
    lights.right_turn = true;
    lights.headlight = true;
    check_vector(lights, std::array<std::uint8_t, 1>{0x0B});

    generated::HostObstacleDist obstacle{};
    obstacle.distance_mm = generated::HostObstacleDist::kDistanceMmClear;
    check_vector(obstacle, std::array<std::uint8_t, 4>{0xFF, 0xFF, 0xFF, 0xFF});

    generated::HostHeartbeat host_heartbeat{};
    host_heartbeat.alive_ctr = 7;
    host_heartbeat.health_flags = 13;
    check_vector(host_heartbeat, std::array<std::uint8_t, 2>{0x07, 0x0D});

    generated::RtDriveCmd rt_drive{};
    rt_drive.motor_speed_mmps = -500;
    rt_drive.gear = 3;
    check_vector(rt_drive, std::array<std::uint8_t, 5>{0xFF, 0xFF, 0xFE, 0x0C, 0x03});

    generated::RtBrakeCmd rt_brake{};
    rt_brake.brake_pressure_kpa = 5000;
    check_vector(rt_brake, std::array<std::uint8_t, 4>{0x00, 0x00, 0x13, 0x88});

    generated::RtStateRpt state{};
    state.mode = 1;
    state.safety_state = 1;
    state.estop_reason = 2;
    state.reversing = true;
    state.rx_overflow = 7;
    state.task_health = 15;
    state.steer_state = 5;
    check_vector(state, std::array<std::uint8_t, 6>{0x01, 0x21, 0x01, 0x07, 0x0F, 0x05});

    generated::RtPidRpt pid{};
    pid.speed_setpoint = 1;
    pid.speed_measured = -2;
    pid.pid_output = 300;
    check_vector(pid, std::array<std::uint8_t, 6>{0x00, 0x01, 0xFF, 0xFE, 0x01, 0x2C});

    generated::SteerDiag steer{};
    steer.angle_0_1deg = 0.0;
    steer.fault = true;
    steer.motor_current = 1.0;
    steer.ecu_temp = 50.0;
    check_vector(steer, std::array<std::uint8_t, 8>{0x75, 0x30, 0x01, 0x00,
                                                    0x64, 0x01, 0xF4, 0x00});

    generated::BrakeDiag brake_diag{};
    brake_diag.pressure_raw = 5.0;
    brake_diag.fault = true;
    brake_diag.motor_current = 2.0;
    brake_diag.ecu_temp = 30.0;
    check_vector(brake_diag, std::array<std::uint8_t, 8>{0x00, 0x64, 0x01, 0x00,
                                                         0xC8, 0x01, 0x2C, 0x00});

    generated::RtHeartbeat rt_heartbeat{};
    rt_heartbeat.alive_ctr = 255;
    rt_heartbeat.health_flags = 15;
    check_vector(rt_heartbeat, std::array<std::uint8_t, 2>{0xFF, 0x0F});

    generated::SysModeCmd mode{};
    mode.mode = 2;
    check_vector(mode, std::array<std::uint8_t, 1>{0x02});

    generated::SysDiagRpt diagnostic{};
    diagnostic.mode = 2;
    diagnostic.brake_engaged = true;
    diagnostic.brake_fault = true;
    diagnostic.heartbeat_ok = true;
    diagnostic.rx_overflow = 8;
    diagnostic.estop_active = true;
    diagnostic.free_heap_kb = 0x1234;
    diagnostic.tec = 0xAA;
    diagnostic.rec = 0x55;
    check_vector(diagnostic, std::array<std::uint8_t, 8>{0x02, 0x03, 0x11, 0x01,
                                                         0x12, 0x34, 0xAA, 0x55});

    generated::SysHeartbeat sys_heartbeat{};
    sys_heartbeat.alive_ctr = 255;
    sys_heartbeat.heartbeat_ok = true;
    sys_heartbeat.estop_active = true;
    sys_heartbeat.mode_auto = true;
    sys_heartbeat.can_ok = true;
    sys_heartbeat.task_safety_ok = true;
    sys_heartbeat.task_brake_ok = true;
    sys_heartbeat.task_dispatch_ok = true;
    sys_heartbeat.task_can_tx_ok = true;
    check_vector(sys_heartbeat, std::array<std::uint8_t, 2>{0xFF, 0xFF});

    generated::PwtDcdcCmd dcdc{};
    dcdc.control = true;
    check_vector(dcdc, std::array<std::uint8_t, 8>{0x01, 0xFF, 0xFF, 0xFF,
                                                   0xFF, 0xFF, 0xFF, 0x00});
}

void test_validation_and_unchanged_outputs() {
    generated::HostDriveCmd value{};
    value.speed_mmps = 123;
    value.yaw_rate_mrad_s = 45;
    value.gear = 1;
    const generated::HostDriveCmd unchanged = value;

    Frame frame = Frame::standard(generated::HostDriveCmd::kId + 1u, 8);
    CHECK(generated::decode(frame.view(), value) == CodecStatus::WrongMessageId);
    CHECK(value.speed_mmps == unchanged.speed_mmps && value.gear == unchanged.gear);
    frame.id = generated::HostDriveCmd::kId;
    frame.extended = true;
    CHECK(generated::decode(frame.view(), value) == CodecStatus::WrongFrameFormat);
    frame.extended = false;
    frame.dlc = 7;
    CHECK(generated::decode(frame.view(), value) == CodecStatus::UnexpectedLength);

    const Frame preserved = Frame::extended_frame(0x44u, 2);
    Frame output = preserved;
    generated::HostDriveCmd invalid_range{};
    invalid_range.speed_mmps = 3001;
    CHECK(generated::encode(invalid_range, output) == CodecStatus::ValueOutOfRange);
    CHECK(output.id == preserved.id && output.extended == preserved.extended &&
          output.dlc == preserved.dlc && output.data == preserved.data);

    std::array<std::uint8_t, 8> payload{};
    payload.fill(0xA5u);
    CHECK(invalid_range.pack(payload.data(), payload.size()) == CodecStatus::ValueOutOfRange);
    CHECK(payload == (std::array<std::uint8_t, 8>{0xA5, 0xA5, 0xA5, 0xA5,
                                                  0xA5, 0xA5, 0xA5, 0xA5}));
    CHECK(unchanged.pack(payload.data(), 7) == CodecStatus::UnexpectedLength);

    generated::HostObstacleDist distance{};
    distance.distance_mm = generated::HostObstacleDist::kDistanceMmClear;
    Frame valid_zero = Frame::standard(generated::HostObstacleDist::kId, 4);
    CHECK(generated::decode(valid_zero.view(), distance) == CodecStatus::Ok);
    CHECK(distance.distance_mm == 0);

    generated::HmiModeReq request{};
    request.req_mode = true;
    Frame invalid_boolean = Frame::standard(generated::HmiModeReq::kId, 2);
    invalid_boolean.data[0] = 2;
    CHECK(generated::decode(invalid_boolean.view(), request) == CodecStatus::ValueOutOfRange);
    CHECK(request.req_mode);

    generated::PwtDcdcCmd dcdc{};
    dcdc.control = true;
    Frame invalid_constant = Frame::extended_frame(generated::PwtDcdcCmd::kId, 8);
    invalid_constant.data = {0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    const generated::PwtDcdcCmd dcdc_unchanged = dcdc;
    CHECK(generated::decode(invalid_constant.view(), dcdc) == CodecStatus::ConstantMismatch);
    CHECK(dcdc.control == dcdc_unchanged.control && dcdc.reserved_1 == dcdc_unchanged.reserved_1);
    dcdc.reserved_1 = 0;
    CHECK(generated::encode(dcdc, output) == CodecStatus::ConstantMismatch);
    CHECK(output.id == preserved.id && output.data == preserved.data);
}

void test_metadata_and_compatibility() {
    static_assert(std::is_same_v<can::generated::HostDriveCmd, generated::HostDriveCmd>);
    static_assert(generated::PwtDcdcCmd::kExtended);
    static_assert(generated::HostLightCmd::kHighId == generated::HostLightCmd::kLowId);
    CHECK(etrike::protocol::kMessages.size() == 42);
    CHECK(etrike::protocol::kRoutes.size() == 9);
    CHECK(etrike::protocol::kRoutes[0].message == "safety:safety_estop");
    CHECK(etrike::protocol::kRoutes[0].semantics == etrike::protocol::RouteSemantics::SameFrame);
}

}  // namespace

int main() {
    test_success_vectors();
    test_validation_and_unchanged_outputs();
    test_metadata_and_compatibility();
    std::printf("generated protocol vector tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
