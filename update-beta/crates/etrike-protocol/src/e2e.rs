//! End-to-end protection (AUTOSAR CRC-8H2F style), ported from
//! `protocol/compat/e2e.hpp`.
//!
//! Polynomial 0x2F (MSB-first), init 0xFF, final XOR 0xFF. An optional 16-bit
//! Data-ID is folded in MSB-first before the protected payload.

/// Data-ID for `SYS_SAFETY_STS` (`0x011`).
pub const DATA_ID_SYS_SAFETY_STS: u16 = 0x3C11;

/// CRC-8H2F over `data`, folding in a 16-bit `data_id` first.
pub fn crc8_h2f(data: &[u8], data_id: u16, init: u8, final_xor: u8) -> u8 {
    fn step(crc: &mut u8, byte: u8) {
        *crc ^= byte;
        for _ in 0..8 {
            *crc = if *crc & 0x80 != 0 {
                (*crc << 1) ^ 0x2F
            } else {
                *crc << 1
            };
        }
    }
    let mut crc = init;
    step(&mut crc, (data_id >> 8) as u8);
    step(&mut crc, (data_id & 0xFF) as u8);
    for &byte in data {
        step(&mut crc, byte);
    }
    crc ^ final_xor
}

/// CRC over the protected `SYS_SAFETY_STS` payload (the four bytes preceding the
/// CRC field).
pub fn sys_safety_sts_crc(payload: &[u8]) -> u8 {
    crc8_h2f(&payload[..4], DATA_ID_SYS_SAFETY_STS, 0xFF, 0xFF)
}

/// CRC over the protected node-status payload (`0x500`/`0x501`/`0x502`): the
/// seven bytes preceding the CRC field, no Data-ID.
pub fn node_status_crc(payload: &[u8]) -> u8 {
    crc8_h2f(&payload[..7], 0u16, 0xFF, 0xFF)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_answers() {
        // Golden value from protocol/vectors/payload-v1.json (sys-safety-packed).
        assert_eq!(
            crc8_h2f(
                &[0x01, 0x01, 0x0d, 0x00],
                DATA_ID_SYS_SAFETY_STS,
                0xFF,
                0xFF
            ),
            0x32
        );
        assert_eq!(sys_safety_sts_crc(&[0, 0, 0, 0, 0]), 0x30);
        assert_eq!(sys_safety_sts_crc(&[2, 0, 0, 0, 0]), 0x75);
    }

    #[test]
    fn crc_round_trips() {
        let mut payload = [0u8; 5];
        payload[0] = 0x02;
        let crc = sys_safety_sts_crc(&payload);
        payload[4] = crc;
        assert_eq!(sys_safety_sts_crc(&payload), crc);
    }
}
