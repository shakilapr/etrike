#include "can/codec_transport.h"

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; \
} } while (0)

int main() {
    using can::gen::CodecStatus;

    // Independently reviewed 0x011 vector: two byte-wide booleans and four light bits.
    const uint8_t safety_raw[] = {1, 1, 0x0D};
    can::gen::SysSafetySts safety{};
    CHECK(can::gen::SysSafetySts::unpack(safety_raw, sizeof(safety_raw), safety) == CodecStatus::Ok);
    CHECK(safety.estop_active && safety.heartbeat_ok);
    CHECK(safety.light_left && !safety.light_right && safety.light_brake && safety.light_head);
    uint8_t safety_roundtrip[3]{};
    CHECK(safety.pack(safety_roundtrip, sizeof(safety_roundtrip)) == CodecStatus::Ok);
    CHECK(std::memcmp(safety_raw, safety_roundtrip, sizeof(safety_raw)) == 0);

    // A symmetric but corrupt boolean representation must not decode as true.
    const uint8_t corrupt_bool[] = {2, 1, 0};
    auto previous = safety;
    CHECK(can::gen::SysSafetySts::unpack(corrupt_bool, sizeof(corrupt_bool), safety) == CodecStatus::ValueOutOfRange);
    CHECK(safety.estop_active == previous.estop_active); // output is unchanged on error

    // Reviewed 0x300 big-endian i32 + signed i24 + enum vector.
    const uint8_t drive_raw[] = {0x00, 0x00, 0x05, 0xDC, 0xFF, 0xFC, 0x18, 0x01};
    can::gen::HostDriveCmd drive{};
    CHECK(can::gen::HostDriveCmd::unpack(drive_raw, sizeof(drive_raw), drive) == CodecStatus::Ok);
    CHECK(drive.speed_mmps == 1500 && drive.yaw_rate_mrad_s == -1000 && drive.gear == 1);
    uint8_t drive_roundtrip[8]{};
    CHECK(drive.pack(drive_roundtrip, sizeof(drive_roundtrip)) == CodecStatus::Ok);
    CHECK(std::memcmp(drive_raw, drive_roundtrip, sizeof(drive_raw)) == 0);

    can::Frame frame{};
    CHECK(can::encode_frame(drive, frame) == CodecStatus::Ok);
    CHECK(frame.id == can::gen::HostDriveCmd::kId && frame.dlc == 8 && !frame.extended);
    frame.dlc = 7;
    CHECK(can::decode_frame(frame, drive) == CodecStatus::UnexpectedLength);

    can::CodecErrorMonitor monitor;
    can::CodecError error{CodecStatus::UnexpectedLength, can::Bus::High, 0x300, false, 7, 8};
    monitor.record(error, 100);
    monitor.record(error, 200);
    CHECK(monitor.entries()[0].consecutive == 2 && monitor.entries()[0].total == 2);
    CHECK(can::CodecErrorMonitor::should_log(1, 100, 0));
    CHECK(!can::CodecErrorMonitor::should_log(2, 1000, 100));
    CHECK(monitor.record_valid(can::Bus::High, 0x300, 300));
    CHECK(monitor.entries()[0].consecutive == 0 && monitor.entries()[0].total == 2);

    can::gen::SafetyEstop estop{};
    CHECK(estop.pack(nullptr, 0) == CodecStatus::Ok);
    CHECK(can::gen::SafetyEstop::unpack(nullptr, 0, estop) == CodecStatus::Ok);

    std::printf("generated codec checks: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
