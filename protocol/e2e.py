"""AUTOSAR-profile CRC-8 (CRC8H2F style) for SYS_SAFETY_STS E2E protection.

Mirrors protocol/compat/e2e.hpp. Profile:
  Polynomial  0x2F (MSB-first; equivalent 0x1D LSB-first)
  Init        0xFF
  Final XOR   0xFF
  RefIn/Out   false
  Data-ID     16-bit, folded MSB-first (high byte then low) BEFORE the payload.
Protected payload for SYS_SAFETY_STS = bytes [0..3]
(estop_active, heartbeat_ok, four light bits, rolling_counter). The CRC field
(byte 4) is excluded from the input; bus identity is NOT mixed in.
"""

DATA_ID_SYS_SAFETY_STS = 0x3C11


def crc8_h2f(data: bytes, data_id: int = 0, init: int = 0xFF, final_xor: int = 0xFF) -> int:
    crc = init & 0xFF
    stream = bytes([(data_id >> 8) & 0xFF, data_id & 0xFF]) + bytes(data)
    for b in stream:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x2F) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return (crc ^ final_xor) & 0xFF


def sys_safety_sts_crc(payload5: bytes) -> int:
    return crc8_h2f(payload5[:4], DATA_ID_SYS_SAFETY_STS)
