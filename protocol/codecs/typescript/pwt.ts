import { type CanFrame, type DecodeResult, type EncodeResult, frame, validateFrame } from "./types";

export const PWT_BUS = "powertrain";
export const PWT_DCDC_COMMAND_ID = 0x10262b27;
export const PWT_DLC = 8;

export function encodePwtDcdcCommand(values: Readonly<Record<string, unknown>>): EncodeResult {
  const control = values.control ?? 1;
  const reset = values.reset_control ?? 0;
  if (typeof control !== "number" || typeof reset !== "number" || ![0, 1].includes(control) || ![0, 1].includes(reset)) {
    return ["invalid_enum"];
  }
  return ["ok", frame(PWT_BUS, PWT_DCDC_COMMAND_ID, "extended", [control, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, reset])];
}

export function decodePwtDcdcCommand(input: CanFrame): DecodeResult<Record<string, number>> {
  const status = validateFrame(input, {
    bus: PWT_BUS,
    id: PWT_DCDC_COMMAND_ID,
    frameFormat: "extended",
    dlc: PWT_DLC,
  });
  if (status !== "ok") return [status];
  if (![0, 1].includes(input.data[0]) || ![0, 1].includes(input.data[7])) return ["invalid_enum"];
  if (input.data.subarray(1, 7).some((value) => value !== 0xff)) return ["constant_mismatch"];
  return ["ok", {
    control: input.data[0],
    reserved_1: 0xff,
    reserved_2: 0xff,
    reserved_3: 0xff,
    reserved_4: 0xff,
    reserved_5: 0xff,
    reserved_6: 0xff,
    reset_control: input.data[7],
  }];
}
