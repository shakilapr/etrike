#pragma once
// Big-endian serialization helpers for CAN frames.
// All CAN multi-byte fields are transmitted MSB-first per convention.

#include <cstdint>

namespace os {

inline void write_be32(uint8_t* buf, int32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >>  8) & 0xFF;
    buf[3] =  val        & 0xFF;
}

inline void write_be16(uint8_t* buf, int16_t val) {
    buf[0] = (val >>  8) & 0xFF;
    buf[1] =  val        & 0xFF;
}

inline int32_t read_be32(const uint8_t* buf) {
    return (int32_t(buf[0]) << 24) | (int32_t(buf[1]) << 16)
         | (int32_t(buf[2]) <<  8) |  int32_t(buf[3]);
}

inline int16_t read_be16(const uint8_t* buf) {
    return (int16_t(buf[0]) << 8) | int16_t(buf[1]);
}

} // namespace os
