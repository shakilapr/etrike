import { afterEach, describe, expect, it, vi } from "vitest";
import {
  BUSES,
  CAN_MESSAGES,
  decodeFrame,
  encodePayload,
  findMessage,
  formatBytes,
  formatDecoded,
  frameTime,
  frameAge,
  getMessageName,
  normalizeBus,
  normalizeCanId,
  numberValue,
  writeI16BE,
  writeI24BE,
  writeI32BE,
  writeU32BE,
  writeI16LE,
  writeU16LE,
  type Bus,
  type CanMessageDef
} from "./can-decoder";

const makeBytes = (size: number): number[] => Array.from({ length: size }, () => 0);

afterEach(() => {
  vi.useRealTimers();
});

// ── normalizeCanId ──

describe("normalizeCanId", () => {
  it("formats numeric hex strings", () => {
    expect(normalizeCanId("0x300")).toBe("0x300");
    expect(normalizeCanId("300")).toBe("0x300");
    expect(normalizeCanId("0x001")).toBe("0x001");
  });

  it("handles lowercase input", () => {
    expect(normalizeCanId("0x7fc")).toBe("0x7FC");
  });

  it("handles non-hex pass-through", () => {
    expect(normalizeCanId("abc")).toBe("0xABC");
  });

  it("handles empty string", () => {
    expect(normalizeCanId("")).toBe("");
  });

  it("handles just 0x prefix", () => {
    // "0x" → parseInt("", 16) = NaN → returns "0X" (uppercased pass-through)
    expect(normalizeCanId("0x")).toBe("0X");
  });

  it("handles invalid hex pass-through", () => {
    expect(normalizeCanId("0xGGG")).toBe("0XGGG");
  });
});

// ── normalizeBus ──

describe("normalizeBus", () => {
  it('returns "low" for "low"', () => {
    expect(normalizeBus("low")).toBe("low");
  });

  it('defaults to "high"', () => {
    expect(normalizeBus("high")).toBe("high");
    expect(normalizeBus(undefined)).toBe("high");
  });

  it("rejects invalid buses", () => {
    expect(() => normalizeBus("invalid")).toThrow("invalid CAN bus");
  });
});

// ── numberValue edge cases ──

describe("numberValue", () => {
  it("returns 0 for NaN", () => expect(numberValue(NaN)).toBe(0));
  it("returns 0 for Infinity", () => expect(numberValue(Infinity)).toBe(0));
  it("returns 0 for -Infinity", () => expect(numberValue(-Infinity)).toBe(0));
  it("returns 0 for undefined", () => expect(numberValue(undefined)).toBe(0));
  it("returns 0 for null", () => expect(numberValue(null)).toBe(0));
  it("coerces numeric strings", () => expect(numberValue("42")).toBe(42));
  it("returns 1 for true", () => expect(numberValue(true)).toBe(1));
  it("returns 0 for false", () => expect(numberValue(false)).toBe(0));
  it("returns the number for valid numbers", () => expect(numberValue(-42)).toBe(-42));
});

// ── Write helpers ──

describe("writeI16BE", () => {
  it("writes positive value", () => {
    const bytes = makeBytes(4);
    writeI16BE(bytes, 1, 0x1234);
    expect(bytes).toEqual([0, 0x12, 0x34, 0]);
  });

  it("masks to 16 bits", () => {
    const bytes = makeBytes(2);
    writeI16BE(bytes, 0, 0x1ABCD);
    expect(bytes).toEqual([0xAB, 0xCD]);
  });
});

describe("writeI16LE", () => {
  it("writes little-endian", () => {
    const bytes = makeBytes(4);
    writeI16LE(bytes, 1, 0x1234);
    expect(bytes).toEqual([0, 0x34, 0x12, 0]);
  });
});

describe("writeU16LE", () => {
  it("writes little-endian unsigned", () => {
    const bytes = makeBytes(2);
    writeU16LE(bytes, 0, 0xABCD);
    expect(bytes).toEqual([0xCD, 0xAB]);
  });
});

describe("writeI24BE", () => {
  it("writes 24-bit big-endian", () => {
    const bytes = makeBytes(4);
    writeI24BE(bytes, 0, 0x123456);
    expect(bytes).toEqual([0x12, 0x34, 0x56, 0]);
  });

  it("masks to 24 bits", () => {
    const bytes = makeBytes(3);
    writeI24BE(bytes, 0, 0xABCDEF12);
    expect(bytes).toEqual([0xCD, 0xEF, 0x12]);
  });
});

describe("writeI32BE", () => {
  it("writes 32-bit big-endian", () => {
    const bytes = makeBytes(4);
    writeI32BE(bytes, 0, 0x12345678);
    expect(bytes).toEqual([0x12, 0x34, 0x56, 0x78]);
  });

  it("handles negative values (two's complement)", () => {
    const bytes = makeBytes(4);
    writeI32BE(bytes, 0, -1);
    expect(bytes).toEqual([0xFF, 0xFF, 0xFF, 0xFF]);
  });
});

describe("writeU32BE", () => {
  it("writes unsigned 32-bit", () => {
    const bytes = makeBytes(4);
    writeU32BE(bytes, 0, 0xDEADBEEF);
    expect(bytes).toEqual([0xDE, 0xAD, 0xBE, 0xEF]);
  });

  it("writes max unsigned (4294967295)", () => {
    const bytes = makeBytes(4);
    writeU32BE(bytes, 0, 0xFFFFFFFF);
    expect(bytes).toEqual([0xFF, 0xFF, 0xFF, 0xFF]);
  });
});

// ── CAN_MESSAGES catalog ──

describe("CAN_MESSAGES catalog", () => {
  it("has 37 messages (15 high + 22 low)", () => {
    expect(CAN_MESSAGES).toHaveLength(37);
  });

  it("every message has a valid bus field", () => {
    for (const msg of CAN_MESSAGES) {
      expect(BUSES).toContain(msg.bus);
    }
  });

  it("high bus has 15 messages", () => {
    expect(CAN_MESSAGES.filter((m) => m.bus === "high")).toHaveLength(15);
  });

  it("low bus has 22 messages", () => {
    expect(CAN_MESSAGES.filter((m) => m.bus === "low")).toHaveLength(22);
  });

  it("has no duplicate (bus, id) pairs", () => {
    const keys = CAN_MESSAGES.map(m => `${m.bus}:${m.id}`);
    expect(new Set(keys).size).toBe(keys.length);
  });

  it("all DLCs are between 0 and 8", () => {
    for (const msg of CAN_MESSAGES) {
      expect(msg.dlc).toBeGreaterThanOrEqual(0);
      expect(msg.dlc).toBeLessThanOrEqual(8);
    }
  });

  it("all field keys are unique within each message", () => {
    for (const msg of CAN_MESSAGES) {
      const keys = msg.fields.map(f => f.key);
      expect(new Set(keys).size).toBe(keys.length);
    }
  });

  it("all IDs are uppercase hex with 0x prefix", () => {
    for (const msg of CAN_MESSAGES) {
      expect(msg.id).toMatch(/^0x[0-9A-F]{3}$/);
    }
  });
});

// ── findMessage ──

describe("findMessage", () => {
  it("finds a message by bus + id", () => {
    const msg = findMessage("high", "0x300");
    expect(msg).toBeDefined();
    expect(msg!.name).toBe("HOST_DRIVE_CMD");
    expect(msg!.bus).toBe("high");
  });

  it("does NOT fall back to id-only search across buses", () => {
    const msg = findMessage("low", "0x300");
    expect(msg).toBeUndefined();
  });

  it("returns undefined for unknown IDs", () => {
    expect(findMessage("high", "0x999")).toBeUndefined();
  });
});

// ── getMessageName ──

describe("getMessageName", () => {
  it("returns the name for known IDs", () => {
    expect(getMessageName("low", "0x169")).toBe("VCU_SES_REQ");
  });

  it("returns UNKNOWN_ prefix for unknown IDs", () => {
    const name = getMessageName("high", "0x999");
    expect(name).toContain("UNKNOWN_");
  });
});

// ── encodePayload ──

describe("encodePayload", () => {
  it("encodes 0x300 drive command (high bus)", () => {
    const result = encodePayload("high", "0x300", { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 });
    expect(result.dlc).toBe(8);
    expect(result.data).toHaveLength(8);
    expect(result.data).toEqual([0x00, 0x00, 0x07, 0xD0, 0x00, 0x00, 0x00, 0x01]);
  });

  it("encodes 0x001 ESTOP (empty payload)", () => {
    const result = encodePayload("high", "0x001", {});
    expect(result.dlc).toBe(0);
    expect(result.data).toHaveLength(0);
  });

  it("encodes 0x011 safety status (high bus)", () => {
    const result = encodePayload("high", "0x011", { estop_active: true, heartbeat_ok: false, light_left: false, light_right: false, light_brake: false, light_head: false });
    expect(result.dlc).toBe(3);
    expect(result.data).toEqual([1, 0, 0]);
  });

  it("encodes 0x120 throttle (high bus)", () => {
    const result = encodePayload("high", "0x120", { speed_mmps: -500 });
    expect(result.dlc).toBe(2);
    expect(result.data).toEqual([0xFE, 0x0C]);
  });

  it("encodes 0x301 brake request", () => {
    const result = encodePayload("high", "0x301", { brake_pressure_kpa: 5000 });
    expect(result.dlc).toBe(4);
    expect(result.data).toEqual([0x00, 0x00, 0x13, 0x88]);
  });

  it("encodes 0x302 light command", () => {
    const result = encodePayload("high", "0x302", { left_turn: true, right_turn: false, brake_light: true, headlight: false });
    expect(result.dlc).toBe(1);
    expect(result.data).toEqual([0x05]);
  });

  it("encodes 0x204 RT drive (low bus)", () => {
    const result = encodePayload("low", "0x204", { motor_speed_mmps: 2000, gear: 1 });
    expect(result.dlc).toBe(5);
    expect(result.data).toEqual([0x00, 0x00, 0x07, 0xD0, 0x01]);
  });

  it("encodes 0x169 steer request (low bus)", () => {
    const result = encodePayload("low", "0x169", { control_enable: true, alignment_enable: false, target_angle: -3000, target_speed: 328, rolling_counter: 1, checksum: 0 });
    expect(result.dlc).toBe(8);
    expect(result.data[0]).toBe(0x02);
  });

  it("encodes 0x206 motor feedback (high bus)", () => {
    const result = encodePayload("high", "0x206", { actual_speed_mmps: 2000, gear_state: 1, fault_flags: 0 });
    expect(result.dlc).toBe(4);
    expect(result.data).toEqual([0x07, 0xD0, 0x01, 0x00]);
  });

  it("encodes 0x210 RT state (high bus)", () => {
    const result = encodePayload("high", "0x210", { mode: 1, safety_state: 1, estop_reason: 0, reversing: false, rx_overflow: 0, task_health: 15, steer_state: 5 });
    expect(result.dlc).toBe(6);
    expect(result.data).toEqual([0x01, 0x01, 0x00, 0x00, 0x0F, 0x05]);
  });

  it("encodes 0x400 obstacle distance (clear)", () => {
    const result = encodePayload("high", "0x400", { distance_mm: 0xFFFFFFFF });
    expect(result.dlc).toBe(4);
    expect(result.data).toEqual([0xFF, 0xFF, 0xFF, 0xFF]);
  });

  it("encodes 0x7FC Host heartbeat (high bus)", () => {
    const result = encodePayload("high", "0x7FC", { alive_ctr: 42, health_flags: 0 });
    expect(result.dlc).toBe(2);
    expect(result.data).toEqual([42, 0]);
  });

  it("encodes 0x110 mode command (low bus)", () => {
    const result = encodePayload("low", "0x110", { mode: 2 });
    expect(result.dlc).toBe(1);
    expect(result.data).toEqual([2]);
  });

  it("encodes 0x310 steer diag (high bus)", () => {
    const result = encodePayload("high", "0x310", { SteerDiag_Angle0_1deg: 0, SteerDiag_Fault: false, SteerDiag_MotorCurrent: 0, SteerDiag_ECUTemp: 0 });
    expect(result.dlc).toBe(8);
    expect(result.data).toHaveLength(8);
  });

  it("encodes 0x311 brake diag (high bus)", () => {
    const result = encodePayload("high", "0x311", { BrakeDiag_PressureRaw: 640, BrakeDiag_Fault: true, BrakeDiag_MotorCurrent: 20, BrakeDiag_ECUTemp: 40 });
    expect(result.dlc).toBe(8);
    // Pressure=640=0x0280, Fault=1, Current=20=0x0014, Temp=40=0x0028
    expect(result.data).toEqual([0x02, 0x80, 0x01, 0x00, 0x14, 0x00, 0x28, 0x00]);
  });

  it("encodes 0x205 brake command (low bus)", () => {
    const result = encodePayload("low", "0x205", { brake_pressure_kpa: 5000 });
    expect(result.dlc).toBe(4);
    expect(result.data).toEqual([0x00, 0x00, 0x13, 0x88]);
  });

  it("returns empty for unknown IDs", () => {
    const result = encodePayload("high", "0x999", {});
    expect(result.dlc).toBe(0);
    expect(result.data).toEqual([]);
  });
});

// ── decodeFrame (mirrors backend tests) ──

describe("decodeFrame", () => {
  it("decodes 0x001 EMPTY", () => {
    expect(decodeFrame("high", "0x001", [])).toEqual({});
  });

  it("decodes 0x011 SAFETY_STS", () => {
    expect(decodeFrame("high", "0x011", [1, 0, 0])).toEqual({ estop_active: true, heartbeat_ok: false, light_left: false, light_right: false, light_brake: false, light_head: false });
  });

  it("decodes 0x120 THROTTLE", () => {
    expect(decodeFrame("high", "0x120", [0x07, 0xD0])).toEqual({ speed_mmps: 2000 });
  });

  it("decodes 0x300 HOST_DRIVE_CMD", () => {
    const result = decodeFrame("high", "0x300", [0x00, 0x00, 0x07, 0xD0, 0x00, 0x00, 0x00, 1]);
    expect(result).toEqual({ speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1, gear_name: "D" });
  });

  it("decodes 0x301 BRAKE_REQ", () => {
    expect(decodeFrame("high", "0x301", [0x00, 0x00, 0x13, 0x88])).toEqual({ brake_pressure_kpa: 5000 });
  });

  it("decodes 0x302 LIGHT_CMD", () => {
    expect(decodeFrame("high", "0x302", [0x05])).toEqual({ left_turn: true, right_turn: false, brake_light: true, headlight: false });
  });

  it("decodes 0x400 OBSTACLE — clear", () => {
    const result = decodeFrame("high", "0x400", [0xFF, 0xFF, 0xFF, 0xFF]);
    expect(result.distance_mm).toBe(0xFFFFFFFF);
    expect(result.distance_label).toBe("clear");
  });

  it("decodes 0x310 STEER_DIAG", () => {
    const result = decodeFrame("high", "0x310", [0x01, 0xC2, 0x00, 0x00, 0x14, 0x00, 0x28, 0x00]);
    expect(result.SteerDiag_Fault).toBe(false);
    expect(result.SteerDiag_MotorCurrent).toBeCloseTo(0.20, 2);
    expect(result.SteerDiag_ECUTemp).toBeCloseTo(4.0, 1);
  });

  it("decodes 0x311 BRAKE_DIAG", () => {
    const result = decodeFrame("high", "0x311", [0x02, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]);
    expect(result.BrakeDiag_PressureRaw).toBeCloseTo(32, 1);
    expect(result.BrakeDiag_Fault).toBe(true);
  });

  it("decodes 0x169 VCU_SES_REQ (steer-by-wire LE)", () => {
    const result = decodeFrame("low", "0x169", [0x02, 0x00, 0x48, 0xF4, 0x48, 0x11, 0x00, 0x00]);
    expect(result.alignment_enable).toBe(false);
    expect(result.control_enable).toBe(true);
    expect(result.target_angle).toBe(-3000);
    expect(result.target_speed).toBe(72);  // byte4=0x48, byte5 bits2-3=0 (RollCntEnable=1)
    expect(result.rolling_counter).toBe(1);
  });

  it("decodes 0x204 RT_DRIVE_CMD (low)", () => {
    const result = decodeFrame("low", "0x204", [0x00, 0x00, 0x07, 0xD0, 1]);
    expect(result).toEqual({ motor_speed_mmps: 2000, gear: 1, gear_name: "D" });
  });

  it("decodes 0x7B9 VCU_SEB_REQ (low)", () => {
    const data = [0x03, 0x00, 0x32, 0x32, 0x00, 0x00, 0x30, 0x00];
    const result = decodeFrame("low", "0x7B9", data);
    expect(result.align_enable).toBe(true);
    expect(result.control_enable).toBe(true);
    expect(result.control_mode).toBe(0);
    expect(result.auto_brake).toBe(false);
    expect(result.stroke_req).toBe(12850);
    expect(result.pressure_req).toBe(50);
    expect(result.rolling_counter).toBe(3);
  });

  it("decodes 0x721 SEB_STATUS (low)", () => {
    // Byte 6: bits 0-1=sec echo(01), bits 2-3=angle[9:8], bits 4-7=RollCnt(0001)
    // angle=0x12C (300): byte5=0x2C, byte6 bits2-3=01 → byte6=0x15
    const data = [0x01, 0x00, 0xE8, 0x03, 0x00, 0x2C, 0x15, 0x00];
    const result = decodeFrame("low", "0x721", data);
    expect(result.alignment_status).toBe(true);
    expect(result.stroke_value).toBe(1000);
    expect(result.pressure_value).toBe(3);
    expect(result.angle_value).toBe(300);
    expect(result.rolling_counter).toBe(1);
  });

  it("decodes unknown ID to bus object", () => {
    expect(decodeFrame("high", "0x999", [1, 2, 3])).toEqual({ bus: "high" });
  });
});

// ── Round-trip: encodePayload → decodeFrame ──

describe("round-trip encode→decode", () => {
  const cases: Array<[Bus, string, Record<string, number | boolean>]> = [
    ["high", "0x011", { estop_active: true, heartbeat_ok: false, light_left: false, light_right: false, light_brake: false, light_head: false }],
    ["high", "0x120", { speed_mmps: 2000 }],
    ["high", "0x206", { actual_speed_mmps: 2000, gear_state: 1, fault_flags: 0 }],
    ["high", "0x210", { mode: 1, safety_state: 1, estop_reason: 0, reversing: false, rx_overflow: 0, task_health: 15, steer_state: 5 }],
    ["high", "0x300", { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 }],
    ["high", "0x301", { brake_pressure_kpa: 5000 }],
    ["high", "0x302", { left_turn: true, right_turn: false, brake_light: true, headlight: false }],
    ["high", "0x400", { distance_mm: 1500 }],
    ["high", "0x7FC", { alive_ctr: 42, health_flags: 0 }],
    ["low", "0x011", { estop_active: false, heartbeat_ok: true, light_left: false, light_right: false, light_brake: false, light_head: false }],
    ["low", "0x110", { mode: 2 }],
    ["low", "0x120", { speed_mmps: -500 }],
    ["low", "0x204", { motor_speed_mmps: 2000, gear: 1 }],
    ["low", "0x205", { brake_pressure_kpa: 5000 }],
    ["low", "0x206", { actual_speed_mmps: 1500, gear_state: 2, fault_flags: 0 }],
    ["low", "0x302", { left_turn: false, right_turn: true, brake_light: false, headlight: true }],
  ];

  it.each(cases)("round-trip: %s %s", (bus, id, values) => {
    const encoded = encodePayload(bus, id, values);
    const decoded = decodeFrame(bus, id, encoded.data);
    for (const [key, val] of Object.entries(values)) {
      expect(decoded[key]).toEqual(val);
    }
  });

  // steer-by-wire messages have rolling_counter and checksum that aren't perfectly round-tripped
  it("round-trip: low:0x169 (steer-by-wire steering)", () => {
    const values: Record<string, number | boolean> = { control_enable: true, alignment_enable: false, target_angle: -3000, target_speed: 328, rolling_counter: 1, checksum: 0 };
    const { data } = encodePayload("low", "0x169", values);
    const decoded = decodeFrame("low", "0x169", data);
    expect(decoded.control_enable).toBe(true);
    expect(decoded.alignment_enable).toBe(false);
    expect(decoded.target_angle).toBe(-3000);
    expect(decoded.target_speed).toBe(328);
    expect(decoded.rolling_counter).toBe(1);
    expect(decoded.checksum).toBe(0);
  });

  it("round-trip: low:0x7B9 (steer-by-wire brake)", () => {
    const values: Record<string, number | boolean> = { align_enable: true, control_enable: false, control_mode: 0, auto_brake: false, stroke_req: 12850, pressure_req: 50, rolling_counter: 3, checksum: 0 };
    const { data } = encodePayload("low", "0x7B9", values);
    const decoded = decodeFrame("low", "0x7B9", data);
    expect(decoded.align_enable).toBe(true);
    expect(decoded.control_enable).toBe(false);
    expect(decoded.control_mode).toBe(0);
    expect(decoded.auto_brake).toBe(false);
    expect(decoded.stroke_req).toBe(12850);
    expect(decoded.pressure_req).toBe(50);
    expect(decoded.rolling_counter).toBe(3);
    expect(decoded.checksum).toBe(0);
  });

  it("round-trip: low:0x721 (SEB status)", () => {
    // byte 6: bits 0-1=sec echo, bits 2-3=angle[9:8], bits 4-7=RollCntStatus.
    // Both encode and decode now use the proper 12-bit angle extraction.
    const values: Record<string, number | boolean> = { alignment_status: true, control_enable_sts: false, control_mode_sts: 0, error_status: 0, stroke_value: 1000, pressure_value: 3, angle_value: 300, rolling_counter: 1, checksum: 0 };
    const { data } = encodePayload("low", "0x721", values);
    const decoded = decodeFrame("low", "0x721", data);
    expect(decoded.alignment_status).toBe(true);
    expect(decoded.stroke_value).toBe(1000);
    expect(decoded.pressure_value).toBe(3);
    expect(decoded.angle_value).toBe(300);
    expect(decoded.rolling_counter).toBe(1);
    expect(decoded.checksum).toBe(0);
  });

  it("round-trip: low:0x201 (SES status)", () => {
    const values: Record<string, number | boolean> = { angle_status: true, error_status: 0, str_angle: 3000, tgt_angle_spd: 500, rolling_counter: 1, checksum: 0 };
    const { data } = encodePayload("low", "0x201", values);
    const decoded = decodeFrame("low", "0x201", data);
    expect(decoded.angle_status).toBe(true);
    expect(decoded.str_angle).toBe(3000);
    expect(decoded.tgt_angle_spd).toBe(500);
    expect(decoded.rolling_counter).toBe(1);
    expect(decoded.checksum).toBe(0);
  });
});

// ── formatBytes ──

describe("formatBytes", () => {
  it("formats bytes as hex", () => {
    expect(formatBytes([0x00, 0x07, 0xD0])).toBe("00 07 D0");
  });

  it("returns '--' for empty array", () => {
    expect(formatBytes([])).toBe("--");
  });
});

// ── formatDecoded ──

describe("formatDecoded", () => {
  it("formats decoded fields", () => {
    const result = formatDecoded({ speed_mmps: 2000, gear: 1 });
    expect(result).toContain("speed=2000");
    expect(result).toContain("gear=1");
  });

  it('returns "event" for empty decoded', () => {
    expect(formatDecoded({})).toBe("event");
  });

  it("filters out _name and _label suffixes", () => {
    const result = formatDecoded({ speed_mmps: 2000, gear_name: "D", mode_label: "AUTO" });
    expect(result).toContain("speed=2000");
    expect(result).not.toContain("gear_name");
    expect(result).not.toContain("mode_label");
  });
});

// ── frameTime ──

describe("frameTime", () => {
  it("formats a valid timestamp", () => {
    const frame = { ts: 1700000000, bus: "high" as const, id: "0x300", name: "TEST", dlc: 8, data: [], decoded: {}, ts_real: 1700000000 };
    const time = frameTime(frame);
    expect(time).toMatch(/^\d{2}:\d{2}:\d{2}\.\d{3}$/);
  });

  it("formats millisecond timestamps", () => {
    const frame = { ts: 1700000000000, bus: "high" as const, id: "0x300", name: "TEST", dlc: 8, data: [], decoded: {} };
    const time = frameTime(frame);
    expect(time).toMatch(/^\d{2}:\d{2}:\d{2}\.\d{3}$/);
  });
});

describe("frameAge", () => {
  it("normalizes millisecond timestamps", () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-07-06T00:00:00Z"));
    const now = Date.now();
    const age = frameAge({ ts: now - 500 });
    expect(age).toBe("500 ms");
  });
});
