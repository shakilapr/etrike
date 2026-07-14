import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { test } from "node:test";

import DISCOVERY from "../../generated/discovery.json";
import {
  decode,
  decodeGenerated,
  decodeInto,
  encode,
  encodeSebCommand,
  encodeSesCommand,
  frame,
  isGenerated,
  PWT_DCDC_COMMAND_ID,
  SEB_STATUS_ID,
  SES_STATUS_ID,
} from "../../codecs/typescript";

interface Vector {
  id: string;
  message: string;
  bus: string;
  frame_format: "standard" | "extended";
  payload: string;
  values?: Record<string, number>;
  status: string;
}

const document = JSON.parse(
  readFileSync(resolve(__dirname, "../../../../vectors/payload-v1.json"), "utf8"),
) as { vectors: Vector[] };

test("shared payload-v1 vectors use selected TypeScript codecs", () => {
  for (const vector of document.vectors) {
    const metadata = DISCOVERY.messages.find((message) => message.canonical_key === vector.message);
    assert.ok(metadata, vector.id);
    const instance = metadata.instances.find((candidate) => candidate.bus === vector.bus);
    assert.ok(instance, vector.id);
    const input = frame(
      vector.bus,
      Number(instance.id),
      vector.frame_format,
      Uint8Array.from(Buffer.from(vector.payload, "hex")),
    );
    const [status, value] = decode(vector.message, input);
    assert.equal(status, vector.status, vector.id);
    if (status === "unsupported_semantics") {
      assert.deepEqual(value?.raw, input.data, vector.id);
    }
    if (status !== "ok" || vector.values === undefined) continue;
    for (const [key, expected] of Object.entries(vector.values)) {
      assert.equal(value?.[key], expected, `${vector.id}:${key}`);
    }
    const [encodeStatus, encoded] = encode(vector.message, vector.values, vector.bus);
    assert.equal(encodeStatus, "ok", vector.id);
    assert.deepEqual(encoded, input, vector.id);
  }
});

test("SES and SEB command codecs match canonical checksum bytes", () => {
  const [sesStatus, sesFrame] = encodeSesCommand({
    alignment_enable: true,
    control_enable: true,
    target_angle_raw: 30000,
    target_speed_raw: 125,
    rolling_counter: 5,
    vehicle_speed_raw: 5,
  });
  assert.equal(sesStatus, "ok");
  assert.equal(Buffer.from(sesFrame!.data).toString("hex"), "030030757d530592");

  const [sebStatus, sebFrame] = encodeSebCommand({
    control_enable: true,
    control_mode: 1,
    stroke_request_raw: 0,
    pressure_request_raw: 80,
    rolling_counter: 5,
  });
  assert.equal(sebStatus, "ok");
  assert.equal(Buffer.from(sebFrame!.data).toString("hex"), "06000050000053fa");
});

test("frame identity, DLC, checksum, constants, ranges, and output atomicity", () => {
  const payload = Uint8Array.from(Buffer.from("410030751000a348", "hex"));
  const cases = [
    [frame("high", SES_STATUS_ID, "standard", payload), "wrong_message_id"],
    [frame("low", SES_STATUS_ID + 1, "standard", payload), "wrong_message_id"],
    [frame("low", SES_STATUS_ID, "extended", payload), "wrong_frame_format"],
    [frame("low", SES_STATUS_ID, "standard", payload, 7), "unexpected_length"],
    [frame("low", SES_STATUS_ID, "standard", payload.subarray(0, 7)), "unexpected_length"],
    [frame("low", SES_STATUS_ID, "standard", [...payload.subarray(0, 7), 0x49]), "checksum_mismatch"],
  ] as const;
  for (const [input, expected] of cases) {
    const output: Record<string, unknown> = { sentinel: 99 };
    assert.equal(decodeInto("ses:ses_status", input, output), expected);
    assert.deepEqual(output, { sentinel: 99 });
  }

  assert.equal(encodeSesCommand({ target_speed_raw: 600 })[0], "value_out_of_range");
  const badPwt = frame(
    "powertrain",
    PWT_DCDC_COMMAND_ID,
    "extended",
    Uint8Array.from(Buffer.from("0100ffffffffff00", "hex")),
  );
  assert.equal(decode("pwt:pwt_dcdc_cmd", badPwt)[0], "constant_mismatch");
  const badSeb = frame("low", SEB_STATUS_ID, "standard", new Uint8Array(8));
  assert.equal(decode("seb:seb_status", badSeb)[0], "checksum_mismatch");
});

test("SES version exposes raw bytes with unsupported capability status", () => {
  const raw = Uint8Array.of(1, 2, 3, 4, 5, 6, 7, 8);
  const [status, value] = decode("ses:ses_version", frame("low", 0x203, "standard", raw));
  assert.equal(status, "unsupported_semantics");
  assert.deepEqual(value, { raw });
  const output: Record<string, unknown> = { sentinel: 1 };
  assert.equal(decodeInto("ses:ses_version", frame("low", 0x203, "standard", raw), output), status);
  assert.deepEqual(output, { sentinel: 1 });
});

test("generic codec cannot compete with custom codec selection", () => {
  assert.equal(isGenerated("ses:ses_status"), false);
  assert.throws(
    () => decodeGenerated("ses:ses_status", frame("low", SES_STATUS_ID, "standard", new Uint8Array(8))),
    /selects a custom codec/,
  );
});
