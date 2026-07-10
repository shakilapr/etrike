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
