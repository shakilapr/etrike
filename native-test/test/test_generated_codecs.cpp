#include "protocol/generated/cpp/etrike_protocol.hpp"

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; \
} } while (0)

int main() {
    namespace generated = etrike::protocol::generated;
    using etrike::protocol::CodecStatus;
    using etrike::protocol::Frame;

    // Independently reviewed 0x011 vector: two byte-wide booleans, four light bits, roll, crc.
    const uint8_t safety_raw[] = {1, 1, 0x0D, 0, 0};
    generated::SysSafetySts safety{};
    CHECK(generated::SysSafetySts::unpack(safety_raw, sizeof(safety_raw), safety) == CodecStatus::Ok);
    CHECK(safety.estop_active && safety.heartbeat_ok);
    CHECK(safety.light_left && !safety.light_right && safety.light_brake && safety.light_head);
    uint8_t safety_roundtrip[5]{};
    CHECK(safety.pack(safety_roundtrip, sizeof(safety_roundtrip)) == CodecStatus::Ok);
    CHECK(std::memcmp(safety_raw, safety_roundtrip, sizeof(safety_raw)) == 0);

    // A symmetric but corrupt boolean representation must not decode as true.
    const uint8_t corrupt_bool[] = {2, 1, 0, 0, 0};
    auto previous = safety;
    CHECK(generated::SysSafetySts::unpack(corrupt_bool, sizeof(corrupt_bool), safety) == CodecStatus::ValueOutOfRange);
    CHECK(safety.estop_active == previous.estop_active); // output is unchanged on error

    // Reviewed 0x300 big-endian i32 + signed i24 + enum vector.
    const uint8_t drive_raw[] = {0x00, 0x00, 0x05, 0xDC, 0xFF, 0xFC, 0x18, 0x01};
    generated::HostDriveCmd drive{};
    CHECK(generated::HostDriveCmd::unpack(drive_raw, sizeof(drive_raw), drive) == CodecStatus::Ok);
    CHECK(drive.speed_mmps == 1500 && drive.yaw_rate_mrad_s == -1000 && drive.gear == 1);
    uint8_t drive_roundtrip[8]{};
    CHECK(drive.pack(drive_roundtrip, sizeof(drive_roundtrip)) == CodecStatus::Ok);
    CHECK(std::memcmp(drive_raw, drive_roundtrip, sizeof(drive_raw)) == 0);

    Frame frame{};
    CHECK(generated::encode(drive, frame) == CodecStatus::Ok);
    CHECK(frame.id == generated::HostDriveCmd::kId && frame.dlc == 8 && !frame.extended);
    frame.dlc = 7;
    CHECK(generated::decode(frame.view(), drive) == CodecStatus::UnexpectedLength);

    generated::SafetyEstop estop{};
    CHECK(estop.pack(nullptr, 0) == CodecStatus::Ok);
    CHECK(generated::SafetyEstop::unpack(nullptr, 0, estop) == CodecStatus::Ok);

    // RT_STATE_RPT estop_reason widening check: 0..15 must all pack and unpack identically.
    for (uint8_t reason = 0; reason <= 15; ++reason) {
        generated::RtStateRpt rpt{};
        rpt.mode = generated::RtStateRpt::kModeEstop;
        rpt.safety_state = 1;
        rpt.estop_reason = reason;
        rpt.task_health = 0x0F;
        rpt.steer_state = 2;

        uint8_t rpt_raw[6]{};
        CHECK(rpt.pack(rpt_raw, sizeof(rpt_raw)) == CodecStatus::Ok);

        generated::RtStateRpt unpacked{};
        CHECK(generated::RtStateRpt::unpack(rpt_raw, sizeof(rpt_raw), unpacked) == CodecStatus::Ok);
        CHECK(unpacked.mode == generated::RtStateRpt::kModeEstop);
        CHECK(unpacked.safety_state == 1);
        CHECK(unpacked.estop_reason == reason);
        CHECK(unpacked.task_health == 0x0F);
        CHECK(unpacked.steer_state == 2);
    }

    std::printf("generated codec checks: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
