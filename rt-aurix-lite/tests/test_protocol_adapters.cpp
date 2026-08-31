// Protocol adapter tests — CAN bytes <-> typed domain I/O.
// Exercises the adapters against the generated subset codecs and the
// vendor SES/SEB codecs (round-trip and known-vector checks).

#include <cstdio>
#include <cstdlib>

#include "protocol/adapters.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::Frame;
using rta::FrameView;
using rta::CodecStatus;
namespace gen = etrike::protocol::generated;
namespace ses = etrike::protocol::codecs::ses;
namespace seb = etrike::protocol::codecs::seb;

void test_host_drive_roundtrip() {
    gen::HostDriveCmd cmd{};
    cmd.speed_mmps = 1000;
    cmd.yaw_rate_mrad_s = -200;
    cmd.gear = 1;

    Frame frame;
    CHECK(etrike::protocol::succeeded(gen::encode(cmd, frame)));

    rta::DriveDemand demand;
    CHECK(rta::decode_host_drive(frame.view(), demand));
    CHECK(demand.speed_mmps == 1000);
    CHECK(demand.yaw_rate_mrad_s == -200);
    CHECK(demand.gear == 1);
}

void test_host_brake() {
    gen::HostBrakeReq req{};
    req.brake_pressure_kpa = 2500;
    Frame frame;
    CHECK(etrike::protocol::succeeded(gen::encode(req, frame)));

    auto out = rta::decode_host_brake(frame.view());
    CHECK(out.valid);
    CHECK(out.brake_kpa == 2500);
}

void test_hmi_mode() {
    gen::HmiModeReq req{};
    req.req_mode = 1;  // AUTO
    req.rolling_counter = 3;
    Frame frame;
    CHECK(etrike::protocol::succeeded(gen::encode(req, frame)));

    rta::ModeRequest mr;
    CHECK(rta::decode_hmi_mode(frame.view(), mr));
    CHECK(mr.valid);
    CHECK(mr.mode == rta::Mode::Auto);
}

void test_mtr_motor() {
    gen::MtrMotorFbk fbk{};
    fbk.actual_speed_mmps = -50;
    fbk.gear_state = 2;
    fbk.fault_flags = 0x03;
    Frame frame;
    CHECK(etrike::protocol::succeeded(gen::encode(fbk, frame)));

    rta::MotorFeedback out;
    CHECK(rta::decode_mtr_motor(frame.view(), out));
    CHECK(out.actual_speed_mmps == -50);
    CHECK(out.gear_state == 2);
    CHECK(out.fault_flags == 0x03);
}

void test_ses_status_decode() {
    // Build a valid 0x201 frame via the SES codec from a known vector shape.
    ses::Status st{};
    st.angle_aligned = true;
    st.control_mode = 1;
    st.error_status = 0;
    st.steering_angle_raw = 1500;  // 150.0°
    st.rolling_counter = 5;

    // ses::decode_status needs a raw frame; build one manually with a valid
    // XOR8 checksum (bytes 0-6 ^ 0xFF at byte 7).
    Frame frame = Frame::standard(0x201u, 8u);
    frame.data[0] = 0x01u;                       // aligned
    frame.data[2] = 1500u & 0xFFu;               // angle LE
    frame.data[3] = (1500u >> 8u) & 0xFFu;
    frame.data[6] = 0x03u | (5u << 4u);          // integrity + rolling counter
    std::uint8_t cks = 0;
    for (int i = 0; i < 7; ++i) cks ^= frame.data[i];
    frame.data[7] = cks ^ 0xFFu;

    rta::SteeringFeedback out;
    CHECK(rta::decode_ses_status(frame.view(), out));
    CHECK(out.valid);
    CHECK(out.angle_aligned);
    CHECK(out.angle_0_1deg == 1500);
    CHECK(out.rolling_counter == 5);
}

void test_seb_status_decode() {
    Frame frame = Frame::standard(0x721u, 8u);
    frame.data[0] = 0x01u;                       // alignment status
    frame.data[2] = 700u & 0xFFu;                // stroke LE
    frame.data[3] = (700u >> 8u) & 0xFFu;
    frame.data[6] = 0x03u | (7u << 4u);
    std::uint8_t cks = 0;
    for (int i = 0; i < 7; ++i) cks ^= frame.data[i];
    frame.data[7] = cks ^ 0xFFu;

    rta::BrakeFeedback out;
    CHECK(rta::decode_seb_status(frame.view(), out));
    CHECK(out.valid);
    CHECK(out.alignment);
    CHECK(out.stroke_raw == 700);
    CHECK(out.rolling_counter == 7);
}

void test_encode_drive_roundtrip() {
    rta::DriveCommand in;
    in.motor_speed_mmps = 1200;
    in.gear = 2;
    Frame frame;
    CHECK(rta::encode_drive_cmd(in, frame));
    CHECK(frame.id == 0x204u);
    CHECK(frame.dlc == 5u);

    // decode back
    gen::RtaDriveCmd cmd{};
    CHECK(etrike::protocol::succeeded(gen::decode(frame.view(), cmd)));
    CHECK(cmd.motor_speed_mmps == 1200);
    CHECK(cmd.gear == 2);
}

void test_encode_steer_roundtrip() {
    rta::SteeringCommand in;
    in.valid = true;
    in.angle_0_1deg = 123;   // 12.3°
    in.speed_raw = 300;
    in.rolling_counter = 4;
    in.vehicle_speed_raw = 20;
    Frame frame;
    CHECK(rta::encode_steer_cmd(in, frame));
    CHECK(frame.id == 0x169u);
    CHECK(frame.dlc == 8u);

    // decode via SES codec
    ses::Command cmd{};
    CHECK(etrike::protocol::succeeded(ses::decode_command(frame.view(), cmd)));
    CHECK(cmd.rolling_counter == 4);
    CHECK(cmd.target_angle_raw == 123 + rta::kSbwAngleOffset);
}

void test_encode_brake_roundtrip() {
    rta::BrakeCommand in;
    in.valid = true;
    in.stroke_mode = true;
    in.stroke_raw = 900;
    in.rolling_counter = 6;
    Frame frame;
    CHECK(rta::encode_brake_cmd(in, frame));
    CHECK(frame.id == 0x7B9u);

    seb::Command cmd{};
    CHECK(etrike::protocol::succeeded(seb::decode_command(frame.view(), cmd)));
    CHECK(cmd.control_mode == seb::ControlMode::Stroke);
    CHECK(cmd.stroke_request_raw == 900);
    CHECK(cmd.rolling_counter == 6);
}

void test_encode_heartbeat() {
    Frame frame;
    CHECK(rta::encode_heartbeat(7, 0x0F, frame));
    CHECK(frame.id == 0x7FDu);
    gen::RtaHeartbeat hb{};
    CHECK(etrike::protocol::succeeded(gen::decode(frame.view(), hb)));
    CHECK(hb.alive_ctr == 7);
    CHECK(hb.health_flags == 0x0F);
}

}  // namespace

int main() {
    test_host_drive_roundtrip();
    test_host_brake();
    test_hmi_mode();
    test_mtr_motor();
    test_ses_status_decode();
    test_seb_status_decode();
    test_encode_drive_roundtrip();
    test_encode_steer_roundtrip();
    test_encode_brake_roundtrip();
    test_encode_heartbeat();
    if (g_failures) {
        std::printf("protocol_adapters: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("protocol_adapters: all tests passed\n");
    return 0;
}
