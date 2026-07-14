import { compute, verify } from "../../profiles/xor8_ff_v1";
import { type CanFrame, type CodecStatus, type DecodeResult, type EncodeResult, frame, validateFrame } from "./types";

export const SES_BUS = "low";
export const SES_DLC = 8;
export const SES_COMMAND_ID = 0x169;
export const SES_STATUS_ID = 0x201;
export const SES_ERROR_INFO_ID = 0x202;
export const SES_VERSION_ID = 0x203;
export const SES_TEST_ID = 0x6fa;

function validate(input: CanFrame, id: number, checksum = false): CodecStatus {
  const status = validateFrame(input, { bus: SES_BUS, id, frameFormat: "standard", dlc: SES_DLC });
  if (status !== "ok") return status;
  if (checksum && !verify(input.data.subarray(0, 7), input.data[7])) return "checksum_mismatch";
  return "ok";
}

function integer(value: unknown, fallback: number): number | undefined {
  const selected = value ?? fallback;
  return typeof selected === "number" && Number.isInteger(selected) ? selected : undefined;
}

export function encodeSesCommand(values: Readonly<Record<string, unknown>>): EncodeResult {
  if (
    (values.alignment_enable !== undefined && typeof values.alignment_enable !== "boolean") ||
    (values.control_enable !== undefined && typeof values.control_enable !== "boolean")
  ) return ["value_out_of_range"];
  const targetAngle = integer(values.target_angle_raw, 0);
  const targetSpeed = integer(values.target_speed_raw, 328);
  const counter = integer(values.rolling_counter, 0);
  const vehicleSpeed = integer(values.vehicle_speed_raw, 0);
  if (targetAngle === undefined || targetSpeed === undefined || counter === undefined || vehicleSpeed === undefined) {
    return ["value_out_of_range"];
  }
  if (targetAngle < -0x8000 || targetAngle > 0x7fff || targetSpeed < 125 || targetSpeed > 525) {
    return ["value_out_of_range"];
  }
  if (counter < 0 || counter > 15 || vehicleSpeed < 0 || vehicleSpeed > 0xff) return ["value_out_of_range"];
  const payload = new Uint8Array(SES_DLC);
  payload[0] = (values.alignment_enable === true ? 1 : 0) | (values.control_enable === true ? 2 : 0);
  new DataView(payload.buffer).setInt16(2, targetAngle, true);
  payload[4] = targetSpeed & 0xff;
  payload[5] = 0x03 | ((targetSpeed >> 6) & 0x0c) | (counter << 4);
  payload[6] = vehicleSpeed;
  payload[7] = compute(payload.subarray(0, 7));
  return ["ok", frame(SES_BUS, SES_COMMAND_ID, "standard", payload)];
}

export function decodeSesCommand(input: CanFrame): DecodeResult<Record<string, unknown>> {
  const status = validate(input, SES_COMMAND_ID, true);
  if (status !== "ok") return [status];
  if ((input.data[5] & 0x03) !== 0x03) return ["constant_mismatch"];
  const targetSpeed = input.data[4] | ((input.data[5] & 0x0c) << 6);
  if (targetSpeed < 125 || targetSpeed > 525) return ["value_out_of_range"];
  return ["ok", {
    alignment_enable: (input.data[0] & 1) !== 0,
    control_enable: (input.data[0] & 2) !== 0,
    target_angle_raw: new DataView(input.data.buffer, input.data.byteOffset, input.data.byteLength).getInt16(2, true),
    target_speed_raw: targetSpeed,
    rolling_counter: input.data[5] >> 4,
    vehicle_speed_raw: input.data[6],
  }];
}

export function decodeSesStatus(input: CanFrame): DecodeResult<Record<string, unknown>> {
  const status = validate(input, SES_STATUS_ID, true);
  if (status !== "ok") return [status];
  const view = new DataView(input.data.buffer, input.data.byteOffset, input.data.byteLength);
  return ["ok", {
    angle_aligned: (input.data[0] & 1) !== 0,
    control_mode: (input.data[0] >> 1) & 3,
    error_status: (input.data[0] >> 6) & 3,
    steering_angle_raw: view.getUint16(2, true),
    target_angle_speed_raw: view.getInt16(4, true),
    steering_torque_raw: input.data[5],
    rolling_counter_enabled: (input.data[6] & 1) !== 0,
    checksum_enabled: (input.data[6] & 2) !== 0,
    rolling_counter: input.data[6] >> 4,
  }];
}

export function decodeSesErrorInfo(input: CanFrame): DecodeResult<{ raw: Uint8Array }> {
  const status = validate(input, SES_ERROR_INFO_ID);
  return status === "ok" ? ["ok", { raw: input.data.slice() }] : [status];
}

export function decodeSesVersion(input: CanFrame): DecodeResult<{ raw: Uint8Array }> {
  const status = validate(input, SES_VERSION_ID);
  return status === "ok" ? ["unsupported_semantics", { raw: input.data.slice() }] : [status];
}

export const decodeSesVersionRaw = decodeSesVersion;

export function decodeSesTest(input: CanFrame): DecodeResult<Record<string, number>> {
  const status = validate(input, SES_TEST_ID);
  if (status !== "ok") return [status];
  const view = new DataView(input.data.buffer, input.data.byteOffset, input.data.byteLength);
  return ["ok", {
    motor_current_raw: view.getInt16(1, true),
    ecu_temperature_raw: view.getUint16(3, true),
    supply_voltage_raw: view.getUint16(5, true),
  }];
}
