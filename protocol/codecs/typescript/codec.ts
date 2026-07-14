import DISCOVERY from "../../generated/discovery.json";
import { decodeGenerated, encodeGenerated, hasMessage } from "./generated";
import { decodePwtDcdcCommand, encodePwtDcdcCommand, PWT_BUS } from "./pwt";
import {
  decodeSebCommand,
  decodeSebErrorInfo,
  decodeSebStatus,
  decodeSebTest,
  decodeSebVersion,
  encodeSebCommand,
  SEB_BUS,
} from "./seb";
import {
  decodeSesCommand,
  decodeSesErrorInfo,
  decodeSesStatus,
  decodeSesTest,
  decodeSesVersion,
  encodeSesCommand,
  SES_BUS,
} from "./ses";
import { type CanFrame, type CodecStatus, type DecodeResult, type EncodeResult } from "./types";

type Decoder = (input: CanFrame) => DecodeResult<Record<string, unknown>>;

const strategies = new Map(
  DISCOVERY.messages.map((message) => [message.canonical_key, message.codec.strategy]),
);

const customDecoders: Readonly<Record<string, Decoder>> = {
  "ses:vcu_ses_req": decodeSesCommand,
  "ses:ses_status": decodeSesStatus,
  "ses:ses_err_info": decodeSesErrorInfo,
  "ses:ses_version": decodeSesVersion,
  "ses:ses_test": decodeSesTest,
  "seb:vcu_seb_req": decodeSebCommand,
  "seb:seb_status": decodeSebStatus,
  "seb:seb_err_info": decodeSebErrorInfo,
  "seb:seb_version": decodeSebVersion,
  "seb:seb_test": decodeSebTest,
};

export function decode(message: string, input: CanFrame): DecodeResult<Record<string, unknown>> {
  const custom = customDecoders[message];
  if (custom !== undefined) return custom(input);
  if (message === "pwt:pwt_dcdc_cmd") return decodePwtDcdcCommand(input);
  if (!hasMessage(message)) return ["wrong_message_id"];
  return decodeGenerated(message, input);
}

export function decodeInto(message: string, input: CanFrame, output: Record<string, unknown>): CodecStatus {
  const [status, value] = decode(message, input);
  if (status === "ok" && value !== undefined) {
    for (const key of Object.keys(output)) delete output[key];
    Object.assign(output, value);
  }
  return status;
}

export function encode(
  message: string,
  values: Readonly<Record<string, unknown>>,
  bus: string,
): EncodeResult {
  if (message === "ses:vcu_ses_req") return bus === SES_BUS ? encodeSesCommand(values) : ["wrong_message_id"];
  if (message === "seb:vcu_seb_req") return bus === SEB_BUS ? encodeSebCommand(values) : ["wrong_message_id"];
  if (message === "pwt:pwt_dcdc_cmd") return bus === PWT_BUS ? encodePwtDcdcCommand(values) : ["wrong_message_id"];
  const strategy = strategies.get(message);
  if (strategy === undefined) return ["wrong_message_id"];
  if (strategy === "custom") throw new RangeError(`${message} has no dedicated encoder`);
  return encodeGenerated(message, values, bus);
}
