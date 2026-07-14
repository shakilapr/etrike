/** Versioned XOR(bytes) ^ 0xFF integrity profile. */
export const PROFILE_ID = "xor8_ff_v1" as const;

export function compute(data: ArrayLike<number>): number {
  let value = 0;
  for (let index = 0; index < data.length; index += 1) {
    const byte = data[index];
    if (!Number.isInteger(byte) || byte < 0 || byte > 0xff) {
      throw new RangeError("profile input bytes must be integers in range 0..255");
    }
    value ^= byte;
  }
  return (value ^ 0xff) & 0xff;
}

export function verify(data: ArrayLike<number>, checksum: number): boolean {
  return Number.isInteger(checksum) && checksum >= 0 && checksum <= 0xff && compute(data) === checksum;
}
