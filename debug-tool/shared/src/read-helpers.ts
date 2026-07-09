export function readI16BE(bytes: number[], offset: number): number {
  const value = ((bytes[offset] ?? 0) << 8) | (bytes[offset + 1] ?? 0);
  return value & 0x8000 ? value - 0x10000 : value;
}
export function readU16BE(bytes: number[], offset: number): number {
  return (((bytes[offset] ?? 0) << 8) | (bytes[offset + 1] ?? 0)) >>> 0;
}
export function readI16LE(bytes: number[], offset: number): number {
  const value = (bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8);
  return value & 0x8000 ? value - 0x10000 : value;
}
export function readU16LE(bytes: number[], offset: number): number {
  return ((bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8)) >>> 0;
}
export function readI24BE(bytes: number[], offset: number): number {
  const value = ((bytes[offset] ?? 0) << 16) | ((bytes[offset + 1] ?? 0) << 8) | (bytes[offset + 2] ?? 0);
  return value & 0x800000 ? value - 0x1000000 : value;
}
export function readI32BE(bytes: number[], offset: number): number {
  return ((bytes[offset] ?? 0) << 24) | ((bytes[offset + 1] ?? 0) << 16) | ((bytes[offset + 2] ?? 0) << 8) | (bytes[offset + 3] ?? 0);
}
export function readU32BE(bytes: number[], offset: number): number {
  return (((bytes[offset] ?? 0) * 0x1000000) + ((bytes[offset + 1] ?? 0) << 16) + ((bytes[offset + 2] ?? 0) << 8) + (bytes[offset + 3] ?? 0)) >>> 0;
}
export function readU32LE(bytes: number[], offset: number): number {
  return ((bytes[offset] ?? 0) + ((bytes[offset + 1] ?? 0) << 8) + ((bytes[offset + 2] ?? 0) << 16) + ((bytes[offset + 3] ?? 0) * 0x1000000)) >>> 0;
}

export function writeI16BE(bytes: number[], offset: number, value: number): void {
  bytes[offset] = (value >> 8) & 0xff;
  bytes[offset + 1] = value & 0xff;
}
export function writeI16LE(bytes: number[], offset: number, value: number): void {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = (value >> 8) & 0xff;
}
export function writeI24BE(bytes: number[], offset: number, value: number): void {
  bytes[offset] = (value >> 16) & 0xff;
  bytes[offset + 1] = (value >> 8) & 0xff;
  bytes[offset + 2] = value & 0xff;
}
export function writeI32BE(bytes: number[], offset: number, value: number): void {
  bytes[offset] = (value >> 24) & 0xff;
  bytes[offset + 1] = (value >> 16) & 0xff;
  bytes[offset + 2] = (value >> 8) & 0xff;
  bytes[offset + 3] = value & 0xff;
}
export function writeU32BE(bytes: number[], offset: number, value: number): void {
  const v = value >>> 0;
  bytes[offset] = (v >>> 24) & 0xff;
  bytes[offset + 1] = (v >>> 16) & 0xff;
  bytes[offset + 2] = (v >>> 8) & 0xff;
  bytes[offset + 3] = v & 0xff;
}
export function writeU16LE(bytes: number[], offset: number, value: number): void {
  const v = value & 0xffff;
  bytes[offset] = v & 0xff;
  bytes[offset + 1] = (v >>> 8) & 0xff;
}

/** Coerce any value to a finite number; returns 0 for NaN / Infinity / null / undefined. */
export function numberValue(v: unknown): number {
  const n = Number(v);
  return isFinite(n) ? n : 0;
}
