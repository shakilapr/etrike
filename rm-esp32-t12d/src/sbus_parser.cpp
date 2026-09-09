#include "sbus_parser.h"

namespace rm {

void SbusParser::reset() {
    idx_ = 0;
}

bool SbusParser::parse_byte(uint8_t byte, SbusFrame& out_frame) {
    if (idx_ == 0) {
        if (byte == kHeaderByte) {
            buffer_[0] = byte;
            idx_ = 1;
        }
        return false;
    }

    buffer_[idx_++] = byte;

    if (idx_ == kFrameSize) {
        idx_ = 0;
        return decode_buffer(buffer_, out_frame);
    }

    return false;
}

bool SbusParser::decode_buffer(const uint8_t buf[kFrameSize], SbusFrame& out_frame) {
    if (buf[0] != kHeaderByte) {
        return false;
    }

    // SBUS standard footer is 0x00. Some telemetry extensions use 0x04, 0x14, 0x24, 0x34, or 0x08.
    const uint8_t footer = buf[24];
    if (footer != 0x00 && footer != 0x04 && (footer & 0x0F) != 0x04 && footer != 0x08) {
        return false;
    }

    out_frame.channels[0]  = static_cast<uint16_t>((buf[1]       | (buf[2] << 8))                      & 0x07FF);
    out_frame.channels[1]  = static_cast<uint16_t>(((buf[2] >> 3)| (buf[3] << 5))                      & 0x07FF);
    out_frame.channels[2]  = static_cast<uint16_t>(((buf[3] >> 6)| (buf[4] << 2) | (buf[5] << 10))     & 0x07FF);
    out_frame.channels[3]  = static_cast<uint16_t>(((buf[5] >> 1)| (buf[6] << 7))                      & 0x07FF);
    out_frame.channels[4]  = static_cast<uint16_t>(((buf[6] >> 4)| (buf[7] << 4))                      & 0x07FF);
    out_frame.channels[5]  = static_cast<uint16_t>(((buf[7] >> 7)| (buf[8] << 1) | (buf[9] << 9))      & 0x07FF);
    out_frame.channels[6]  = static_cast<uint16_t>(((buf[9] >> 2)| (buf[10] << 6))                     & 0x07FF);
    out_frame.channels[7]  = static_cast<uint16_t>(((buf[10] >> 5)|(buf[11] << 3))                     & 0x07FF);
    out_frame.channels[8]  = static_cast<uint16_t>((buf[12]      | (buf[13] << 8))                     & 0x07FF);
    out_frame.channels[9]  = static_cast<uint16_t>(((buf[13] >> 3)|(buf[14] << 5))                     & 0x07FF);
    out_frame.channels[10] = static_cast<uint16_t>(((buf[14] >> 6)|(buf[15] << 2) | (buf[16] << 10))   & 0x07FF);
    out_frame.channels[11] = static_cast<uint16_t>(((buf[16] >> 1)|(buf[17] << 7))                     & 0x07FF);
    out_frame.channels[12] = static_cast<uint16_t>(((buf[17] >> 4)|(buf[18] << 4))                     & 0x07FF);
    out_frame.channels[13] = static_cast<uint16_t>(((buf[18] >> 7)|(buf[19] << 1) | (buf[20] << 9))    & 0x07FF);
    out_frame.channels[14] = static_cast<uint16_t>(((buf[20] >> 2)|(buf[21] << 6))                     & 0x07FF);
    out_frame.channels[15] = static_cast<uint16_t>(((buf[21] >> 5)|(buf[22] << 3))                     & 0x07FF);

    out_frame.ch17       = (buf[23] & 0x01) != 0;
    out_frame.ch18       = (buf[23] & 0x02) != 0;
    out_frame.frame_lost = (buf[23] & 0x04) != 0;
    out_frame.failsafe   = (buf[23] & 0x08) != 0;

    return true;
}

}  // namespace rm
