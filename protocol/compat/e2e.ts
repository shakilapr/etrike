// AUTOSAR-profile CRC-8 (CRC8H2F style) for SYS_SAFETY_STS E2E protection.
// Mirrors protocol/compat/e2e.hpp. See that header for the full profile note.

export const DATA_ID_SYS_SAFETY_STS = 0x3c11;

export function crc8H2F(
  data: Uint8Array,
  dataId = 0,
  init = 0xff,
  finalXor = 0xff,
): number {
  let crc = init & 0xff;
  const stream = new Uint8Array([(dataId >> 8) & 0xff, dataId & 0xff, ...data]);
  for (const b of stream) {
    crc ^= b;
    for (let i = 0; i < 8; i++) {
      if (crc & 0x80) {
        crc = ((crc << 1) ^ 0x2f) & 0xff;
      } else {
        crc = (crc << 1) & 0xff;
      }
    }
  }
  return (crc ^ finalXor) & 0xff;
}

export function sysSafetyStsCrc(payload5: Uint8Array): number {
  return crc8H2F(payload5.subarray(0, 4), DATA_ID_SYS_SAFETY_STS);
}
