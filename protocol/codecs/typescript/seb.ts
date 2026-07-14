import { compute, verify } from "../../profiles/xor8_ff_v1";
import { type CanFrame, type CodecStatus, type DecodeResult, type EncodeResult, frame, validateFrame } from "./types";

export const SEB_BUS = "low";
export const SEB_DLC = 8;
export const SEB_COMMAND_ID = 0x7b9;
export const SEB_STATUS_ID = 0x721;
export const SEB_ERROR_INFO_ID = 0x731;
export const SEB_VERSION_ID = 0x741;
export const SEB_TEST_ID = 0x6fb;

function validate(input: CanFrame, id: number, checksum = false): CodecStatus {
  const status = validateFrame(input, { bus: SEB_BUS, id, frameFormat: "standard", dlc: SEB_DLC });
  if (status !== "ok") return status;
  if (checksum && !verify(input.data.subarray(0, 7), input.data[7])) return "checksum_mismatch";
  return "ok";
}

function integer(value: unknown, fallback: number): number | undefined {
  const selected = value ?? fallback;
  return typeof selected === "number" && Number.isInteger(selected) ? selected : undefined;
}

export function encodeSebCommand(values: Readonly<Record<string, unknown>>): EncodeResult {
  if (
    (values.alignment_enable !== undefined && typeof values.alignment_enable !== "boolean") ||
    (values.control_enable !== undefined && typeof values.control_enable !== "boolean") ||
    (values.auto_brake !== undefined && typeof values.auto_brake !== "boolean")
  ) return ["value_out_of_range"];
  const mode = integer(values.control_mode, 0);
  const stroke = integer(values.stroke_request_raw, 600);
  const pressure = integer(values.pressure_request_raw, 0);
  const counter = integer(values.rolling_counter, 0);
  if (mode === undefined || ![0, 1].includes(mode)) return ["invalid_enum"];
  if (stroke === undefined || pressure === undefined || counter === undefined) return ["value_out_of_range"];
  if (stroke < 0 || stroke > 0xffff || pressure < 0 || pressure > 100 || counter < 0 || counter > 15) {
    return ["value_out_of_range"];
  }
  const payload = new Uint8Array(SEB_DLC);
  payload[0] =
    (values.alignment_enable === true ? 1 : 0) |
    (values.control_enable === true ? 2 : 0) |
    (mode << 2) |
    (values.auto_brake === true ? 8 : 0);
  payload[2] = stroke & 0xff;
  payload[3] = mode === 0 ? stroke >> 8 : pressure;
  payload[6] = 0x03 | (counter << 4);
  payload[7] = compute(payload.subarray(0, 7));
  return ["ok", frame(SEB_BUS, SEB_COMMAND_ID, "standard", payload)];
}

export function decodeSebCommand(input: CanFrame): DecodeResult<Record<string, unknown>> {
  const status = validate(input, SEB_COMMAND_ID, true);
  if (status !== "ok") return [status];
  if ((input.data[6] & 0x03) !== 0x03) return ["constant_mismatch"];
  const mode = (input.data[0] & 4) !== 0 ? 1 : 0;
  const pressure = mode === 1 ? input.data[3] : 0;
  if (pressure > 100) return ["value_out_of_range"];
  return ["ok", {
    alignment_enable: (input.data[0] & 1) !== 0,
    control_enable: (input.data[0] & 2) !== 0,
    control_mode: mode,
    auto_brake: (input.data[0] & 8) !== 0,
    stroke_request_raw: mode === 0 ? input.data[2] | (input.data[3] << 8) : input.data[2],
    pressure_request_raw: pressure,
    rolling_counter: input.data[6] >> 4,
  }];
}

export function decodeSebStatus(input: CanFrame): DecodeResult<Record<string, unknown>> {
  const status = validate(input, SEB_STATUS_ID, true);
  if (status !== "ok") return [status];
  const view = new DataView(input.data.buffer, input.data.byteOffset, input.data.byteLength);
  return ["ok", {
    status_byte: input.data[0],
    alignment_status: (input.data[0] & 1) !== 0,
    control_enabled: (input.data[0] & 2) !== 0,
    control_mode: (input.data[0] >> 2) & 3,
    auto_brake_status: (input.data[0] & 0x10) !== 0,
    error_status: (input.data[0] >> 6) & 3,
    stroke_value_raw: view.getUint16(2, true),
    pressure_value_raw: input.data[3],
    angle_value_raw: view.getInt16(5, true),
    rolling_counter_enabled: (input.data[6] & 1) !== 0,
    checksum_enabled: (input.data[6] & 2) !== 0,
    rolling_counter: input.data[6] >> 4,
  }];
}

export function decodeSebErrorInfo(input: CanFrame): DecodeResult<{ raw: Uint8Array }> {
  const status = validate(input, SEB_ERROR_INFO_ID);
  return status === "ok" ? ["ok", { raw: input.data.slice() }] : [status];
}

export function decodeSebVersion(input: CanFrame): DecodeResult<Record<string, unknown>> {
  const status = validate(input, SEB_VERSION_ID);
  return status === "ok"
    ? ["ok", { software_raw: input.data[0], hardware_raw: input.data[1], raw: input.data.slice() }]
    : [status];
}

export function decodeSebTest(input: CanFrame): DecodeResult<Record<string, number>> {
  const status = validate(input, SEB_TEST_ID);
  if (status !== "ok") return [status];
  const view = new DataView(input.data.buffer, input.data.byteOffset, input.data.byteLength);
  return ["ok", {
    motor_current_raw: view.getInt16(1, true),
    ecu_temperature_raw: view.getUint16(3, true),
    supply_voltage_raw: view.getUint16(5, true),
  }];
}
