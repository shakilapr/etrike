#include <cstdio>
#include "can/manual/vendor_protocol.h"
int main() {
    int fail = 0;
    can::Frame frame{};
    frame.id = can::gen::SebStatus::kId; frame.dlc = can::gen::SebStatus::kDlc;
    frame.data[0] = 0x45; frame.data[2] = 0x34; frame.data[3] = 0x12; frame.data[6] = 0xA0;
    frame.data[7] = can::manual::vendor_checksum(frame.data, 7);
    can::manual::SebStatusValue value{};
    fail += can::manual::decode_seb_status(frame, value) != can::gen::CodecStatus::Ok;
    fail += !value.alignment_status || value.control_mode != 1 || value.error_status != 1;
    fail += value.stroke_value != 0x1234 || value.pressure_value != 0x12 || value.rolling_counter != 0xA;
    auto original = value; frame.data[7] ^= 1;
    fail += can::manual::decode_seb_status(frame, value) != can::gen::CodecStatus::ChecksumMismatch;
    fail += value.stroke_value != original.stroke_value;
    frame.dlc = 7;
    fail += can::manual::decode_seb_status(frame, value) != can::gen::CodecStatus::UnexpectedLength;
    std::printf("vendor protocol: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
