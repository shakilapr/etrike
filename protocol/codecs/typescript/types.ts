export type CodecStatus =
  | "ok"
  | "wrong_message_id"
  | "wrong_frame_format"
  | "unexpected_length"
  | "value_out_of_range"
  | "invalid_enum"
  | "constant_mismatch"
  | "checksum_mismatch"
  | "unsupported_semantics";

export type FrameFormat = "standard" | "extended";

export interface CanFrame {
  readonly bus: string;
  readonly id: number;
  readonly frameFormat: FrameFormat;
  readonly data: Uint8Array;
  readonly dlc: number;
}

export type DecodeResult<T> = readonly [CodecStatus, T?];
export type EncodeResult = readonly [CodecStatus, CanFrame?];

export function frame(
  bus: string,
  id: number,
  frameFormat: FrameFormat,
  data: ArrayLike<number>,
  dlc = data.length,
): CanFrame {
  return { bus, id, frameFormat, data: Uint8Array.from(data), dlc };
}

export function validateFrame(
  value: CanFrame,
  expected: { bus: string; id: number; frameFormat: FrameFormat; dlc: number },
): CodecStatus {
  if (value.bus !== expected.bus || value.id !== expected.id) return "wrong_message_id";
  if (value.frameFormat !== expected.frameFormat) return "wrong_frame_format";
  if (value.dlc !== expected.dlc || value.data.length !== expected.dlc) return "unexpected_length";
  return "ok";
}
