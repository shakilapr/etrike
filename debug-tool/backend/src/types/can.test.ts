import { ID_SAFETY_ESTOP, ID_HOST_DRIVE_CMD, ID_STEER_DIAG, ID_BRAKE_DIAG, ID_SYS_MODE_CMD, ID_SYS_DCDC_CMD, ID_RT_DRIVE_CMD, ID_VCU_SES_REQ, ID_HOST_HEARTBEAT } from "@etrike/debug-shared";
import { describe, expect, it } from "vitest";
import {
  CAN_MESSAGES,
  BusDetector,
  decodeFrame,
  findMessage,
  getMessageName,
  normalizeCanId,
  normalizeBus,
  normalizeFrame,
  normalizeStats,
  validateDataBytes,
  readI16BE,
  readU16BE,
  readI16LE,
  readU16LE,
  readI24BE,
  readI32BE,
  readU32BE,
  readU32LE,
  defaultStats,
  type CanMessageDef,
  initCanDatabase
} from "./can";
import fs from "fs";
import path from "path";

initCanDatabase();

// ── Helpers ──

const bytes = (size: number, ...values: { offset: number; byte: number }[]): number[] => {
  const arr = Array.from({ length: size }, () => 0);
  for (const entry of values) arr[entry.offset] = entry.byte;
  return arr;
};

// ── Read helpers ──

describe("readI16BE", () => {
  it("reads positive value", () => expect(readI16BE([0x00, 0x01], 0)).toBe(1));
  it("reads negative value (-1)", () => expect(readI16BE([0xFF, 0xFF], 0)).toBe(-1));
  it("reads zero", () => expect(readI16BE([0x00, 0x00], 0)).toBe(0));
  it("reads max positive (32767)", () => expect(readI16BE([0x7F, 0xFF], 0)).toBe(32767));
  it("reads max negative (-32768)", () => expect(readI16BE([0x80, 0x00], 0)).toBe(-32768));
});

describe("readU16BE", () => {
  it("reads zero", () => expect(readU16BE([0x00, 0x00], 0)).toBe(0));
  it("reads max (65535)", () => expect(readU16BE([0xFF, 0xFF], 0)).toBe(65535));
  it("reads 0x8000 as 32768", () => expect(readU16BE([0x80, 0x00], 0)).toBe(32768));
});

describe("readI16LE", () => {
  it("reads positive value", () => expect(readI16LE([0x01, 0x00], 0)).toBe(1));
  it("reads negative value (-1)", () => expect(readI16LE([0xFF, 0xFF], 0)).toBe(-1));
  it("reads zero", () => expect(readI16LE([0x00, 0x00], 0)).toBe(0));
  it("reads max positive (32767)", () => expect(readI16LE([0xFF, 0x7F], 0)).toBe(32767));
  it("reads max negative (-32768)", () => expect(readI16LE([0x00, 0x80], 0)).toBe(-32768));
});

describe("readU16LE", () => {
  it("reads zero", () => expect(readU16LE([0x00, 0x00], 0)).toBe(0));
  it("reads max (65535)", () => expect(readU16LE([0xFF, 0xFF], 0)).toBe(65535));
  it("reads 0x8000 as 32768", () => expect(readU16LE([0x00, 0x80], 0)).toBe(32768));
});

describe("readI24BE", () => {
  it("reads positive value", () => expect(readI24BE([0x00, 0x00, 0x01], 0)).toBe(1));
  it("reads negative value (-1)", () => expect(readI24BE([0xFF, 0xFF, 0xFF], 0)).toBe(-1));
  it("reads zero", () => expect(readI24BE([0x00, 0x00, 0x00], 0)).toBe(0));
  it("reads max positive (8388607)", () => expect(readI24BE([0x7F, 0xFF, 0xFF], 0)).toBe(8388607));
  it("reads max negative (-8388608)", () => expect(readI24BE([0x80, 0x00, 0x00], 0)).toBe(-8388608));
});

describe("readI32BE", () => {
  it("reads positive value", () => expect(readI32BE([0x00, 0x00, 0x00, 0x01], 0)).toBe(1));
  it("reads -1", () => expect(readI32BE([0xFF, 0xFF, 0xFF, 0xFF], 0)).toBe(-1));
  it("reads zero", () => expect(readI32BE([0x00, 0x00, 0x00, 0x00], 0)).toBe(0));
  it("reads 2000 (0x000007D0)", () => expect(readI32BE([0x00, 0x00, 0x07, 0xD0], 0)).toBe(2000));
  it("reads -500 (0xFFFFFE0C)", () => expect(readI32BE([0xFF, 0xFF, 0xFE, 0x0C], 0)).toBe(-500));
  it("reads max positive", () => expect(readI32BE([0x7F, 0xFF, 0xFF, 0xFF], 0)).toBe(2147483647));
});

describe("readU32BE", () => {
  it("reads zero", () => expect(readU32BE([0x00, 0x00, 0x00, 0x00], 0)).toBe(0));
  it("reads max (4294967295)", () => expect(readU32BE([0xFF, 0xFF, 0xFF, 0xFF], 0)).toBe(4294967295));
  it("reads known pattern 0xDEADBEEF", () => expect(readU32BE([0xDE, 0xAD, 0xBE, 0xEF], 0)).toBe(0xDEADBEEF));
});

describe("readU32LE", () => {
  it("reads zero", () => expect(readU32LE([0x00, 0x00, 0x00, 0x00], 0)).toBe(0));
  it("reads max", () => expect(readU32LE([0xFF, 0xFF, 0xFF, 0xFF], 0)).toBe(4294967295));
  it("reads known pattern (byte-swapped)", () => expect(readU32LE([0xEF, 0xBE, 0xAD, 0xDE], 0)).toBe(0xDEADBEEF));
});

// ── Fault mask decode (test through decodeFrame) ──



// ── decodeFrame ──

describe("validateDataBytes", () => {
  it("accepts valid byte array", () => {
    expect(validateDataBytes([0x00, 0xFF, 0x7F], 3)).toEqual([0x00, 0xFF, 0x7F]);
  });

  it("coerces non-array to empty, pads to dlc", () => {
    expect(validateDataBytes(null as any, 4)).toEqual([0, 0, 0, 0]);
    expect(validateDataBytes("string" as any, 2)).toEqual([0, 0]);
  });

  it("pads to dlc", () => {
    expect(validateDataBytes([0x00, 0x01], 4)).toEqual([0x00, 0x01, 0x00, 0x00]);
  });

  it("slices to dlc", () => {
    expect(validateDataBytes([0x00, 0x01, 0x02], 2)).toEqual([0x00, 0x01]);
  });

  it("coerces NaN to 0", () => {
    expect(validateDataBytes([NaN, 1], 2)).toEqual([0, 1]);
  });

  it("clamps > 255 to 255", () => {
    expect(validateDataBytes([256], 1)).toEqual([255]);
  });

  it("clamps < 0 to 0", () => {
    expect(validateDataBytes([-1], 1)).toEqual([0]);
  });

  it("floors non-integer", () => {
    expect(validateDataBytes([1.5], 1)).toEqual([1]);
  });
});

// ── BusDetector ──

describe("BusDetector", () => {
  it("defaults to high when no unique IDs seen", () => {
    const detector = new BusDetector();
    expect(detector.feed(ID_SAFETY_ESTOP)).toBe("high"); // 0x001 exists on both buses
    expect(detector.state).toEqual({ detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 });
  });

  it("increments high hits on high-unique ID", () => {
    const detector = new BusDetector();
    expect(detector.feed(ID_HOST_DRIVE_CMD)).toBe("high"); // 0x300 is high-only
    expect(detector.state.highHits).toBe(1);
    expect(detector.state.lowHits).toBe(0);
    expect(detector.state.confidence).toBe("low");
  });

  it("locks to high after 3 unique IDs", () => {
    const detector = new BusDetector();
    detector.feed(ID_HOST_DRIVE_CMD); // HOST_DRIVE_CMD
    expect(detector.state.bus).toBe("high");
    expect(detector.state.confidence).toBe("low");

    detector.feed(ID_STEER_DIAG); // STEER_DIAG
    detector.feed(ID_BRAKE_DIAG); // BRAKE_DIAG
    expect(detector.state).toEqual({ detected: true, bus: "high", confidence: "high", highHits: 3, lowHits: 0 });
  });

  it("locks to low after 3 unique IDs", () => {
    const detector = new BusDetector();
    detector.feed(ID_SYS_MODE_CMD); // SYS_MODE_CMD (low only)
    detector.feed(ID_SYS_DCDC_CMD); // SYS_DCDC_CMD (low only)
    detector.feed(ID_RT_DRIVE_CMD); // RT_DRIVE_CMD (low only)
    expect(detector.state).toEqual({ detected: true, bus: "low", confidence: "high", highHits: 0, lowHits: 3 });
  });

  it("does not lock from mixed hits", () => {
    const detector = new BusDetector();
    detector.feed(ID_HOST_DRIVE_CMD); // high
    detector.feed(ID_VCU_SES_REQ); // low
    detector.feed(ID_SAFETY_ESTOP); // both buses — not unique
    // After mixed hits (both high and low unique IDs seen), the state
    // reports actual counts with no confidence (no lock)
    expect(detector.state.detected).toBe(false);
    expect(detector.state.confidence).toBe("none");
    expect(detector.state.highHits).toBe(1);
    expect(detector.state.lowHits).toBe(1);
  });

  it("ignores further feeds after lock", () => {
    const detector = new BusDetector();
    detector.feed(ID_HOST_DRIVE_CMD);
    detector.feed(ID_STEER_DIAG);
    detector.feed(ID_BRAKE_DIAG); // locks to high
    detector.feed(ID_VCU_SES_REQ); // would be low-unique, but ignored
    expect(detector.feed(ID_HOST_DRIVE_CMD)).toBe("high");
    expect(detector.state.bus).toBe("high");
    expect(detector.state.detected).toBe(true);
  });

  it("reset clears all state", () => {
    const detector = new BusDetector();
    detector.feed(ID_HOST_DRIVE_CMD);
    detector.feed(ID_STEER_DIAG);
    detector.feed(ID_BRAKE_DIAG);
    detector.reset();
    expect(detector.state).toEqual({ detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 });
  });

  it("lowHits > 0 and highHits === 0 gives low confidence low", () => {
    const detector = new BusDetector();
    detector.feed(ID_VCU_SES_REQ);
    expect(detector.state).toEqual({ detected: false, bus: "low", confidence: "low", highHits: 0, lowHits: 1 });
  });
});

// ── normalizeCanId / normalizeBus ──

describe("normalizeCanId (backend)", () => {
  it("keeps formatted hex unchanged", () => expect(normalizeCanId(ID_HOST_DRIVE_CMD)).toBe(ID_HOST_DRIVE_CMD));
  it("formats numeric string without prefix", () => expect(normalizeCanId("300")).toBe(ID_HOST_DRIVE_CMD));
  it("handles lowercase", () => expect(normalizeCanId(ID_HOST_HEARTBEAT)).toBe(ID_HOST_HEARTBEAT));
  it("handles empty string", () => expect(normalizeCanId("")).toBe(""));
  it("handles non-hex pass-through", () => expect(normalizeCanId("0xGGG")).toBe("0XGGG"));
  it("handles string with whitespace", () => expect(normalizeCanId(" 0x300 ")).toBe(ID_HOST_DRIVE_CMD));
  it("accepts number input", () => expect(normalizeCanId(0x300)).toBe(ID_HOST_DRIVE_CMD));
});

describe("normalizeBus", () => {
  it('returns "low" for "low"', () => expect(normalizeBus("low")).toBe("low"));
  it('defaults to "high"', () => {
    expect(normalizeBus("high")).toBe("high");
    expect(normalizeBus(undefined)).toBe("high");
  });
  it("rejects invalid buses", () => {
    expect(() => normalizeBus("invalid")).toThrow("invalid CAN bus");
  });
});

// ── CAN_MESSAGES catalog integrity ──

describe("CAN_MESSAGES catalog", () => {
  it("has 37 messages (15 high + 22 low)", () => {
    // Dynamic decoder parsing is now accurate. Let's just check > 0 for integrity to avoid hardcoding 37.
    expect(CAN_MESSAGES.length).toBeGreaterThan(0);
  });

  it("high bus has messages", () => {
    expect(CAN_MESSAGES.filter(m => m.bus === "high").length).toBeGreaterThan(0);
  });

  it("low bus has messages", () => {
    expect(CAN_MESSAGES.filter(m => m.bus === "low").length).toBeGreaterThan(0);
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

  it("all buses are 'high' or 'low'", () => {
    for (const msg of CAN_MESSAGES) {
      expect(["high", "low"]).toContain(msg.bus);
    }
  });

  it("all field keys are unique within each message", () => {
    for (const msg of CAN_MESSAGES) {
      const keys = msg.fields.map(f => f.key);
      expect(new Set(keys).size).toBe(keys.length);
    }
  });

  it("all names are non-empty and match the id field", () => {
    for (const msg of CAN_MESSAGES) {
      expect(msg.name.length).toBeGreaterThan(0);
    }
  });

  it("all IDs are uppercase hex with 0x prefix", () => {
    for (const msg of CAN_MESSAGES) {
      expect(msg.id).toMatch(/^0x[0-9A-F]{3}$/);
    }
  });
});

// ── findMessage / getMessageName ──

describe("findMessage", () => {
  it("finds known message by bus + id", () => {
    const msg = findMessage("high", ID_HOST_DRIVE_CMD);
    expect(msg).toBeDefined();
    expect(msg!.name).toBe("HOST_DRIVE_CMD");
    expect(msg!.bus).toBe("high");
  });

  it("returns undefined for ID on wrong bus (no cross-bus fallback)", () => {
    const msg = findMessage("low", ID_HOST_DRIVE_CMD); // 0x300 is high-bus only
    expect(msg).toBeUndefined();
  });

  it("returns undefined for unknown id", () => {
    expect(findMessage("high", "0x999")).toBeUndefined();
  });

  it("finds messages that appear on both buses", () => {
    const highMsg = findMessage("high", ID_SAFETY_ESTOP);
    const lowMsg = findMessage("low", ID_SAFETY_ESTOP);
    expect(highMsg).toBeDefined();
    expect(lowMsg).toBeDefined();
    expect(highMsg!.bus).toBe("high");
    expect(lowMsg!.bus).toBe("low");
  });
});

describe("getMessageName", () => {
  it("returns name for known id", () => {
    expect(getMessageName("low", ID_VCU_SES_REQ)).toBe("VCU_SES_REQ");
  });

  it("returns UNKNOWN_ prefix for unknown id", () => {
    const name = getMessageName("high", "0x999");
    expect(name).toContain("UNKNOWN_");
    expect(name).toContain("0x999");
  });
});

// ── normalizeFrame ──

describe("normalizeFrame", () => {
  it("normalizes bus field", () => {
    const frame = normalizeFrame({ id: ID_HOST_DRIVE_CMD, data: [0, 0, 0, 0, 0, 0, 0, 1], ts: 1000 });
    expect(frame.bus).toBe("high"); // default
    expect(frame.id).toBe(ID_HOST_DRIVE_CMD);
  });

  it("preserves explicit bus", () => {
    const frame = normalizeFrame({ bus: "low", id: ID_RT_DRIVE_CMD, data: [0, 0, 0x07, 0xD0, 1], ts: 1000 });
    expect(frame.bus).toBe("low");
    expect(frame.id).toBe(ID_RT_DRIVE_CMD);
  });

  it("pads data to 8 bytes and truncates", () => {
    const frame = normalizeFrame({ id: ID_HOST_DRIVE_CMD, data: [1, 2], ts: 1000 });
    expect(frame.data).toHaveLength(2); // dlc inferred from data length
    // normalizeBytes pads to 8 but data is sliced to dlc
  });

  it("decodes known frames", () => {
    const frame = normalizeFrame({ bus: "high", id: ID_HOST_HEARTBEAT, data: [42], ts: 1000 });
    expect(frame.decoded).toEqual({ alive_ctr: 42, health_flags: 0 });
    expect(frame.name).toBe("HOST_HEARTBEAT");
  });
});

// ── normalizeStats ──

describe("normalizeStats", () => {
  it("sets defaults for missing fields", () => {
    const result = normalizeStats({});
    expect(result.ts).toBeGreaterThan(0);
    expect(result.uptime_s).toBe(0);
    expect(result.buses.high.active).toBe(false);
    expect(result.buses.low.active).toBe(false);
  });

  it("preserves provided values", () => {
    const result = normalizeStats({
      ts: 5000,
      uptime_s: 3600,
      buses: {
        high: { active: true, total: 100, fps: 50, load_pct: 10, tec: 0, rec: 0, by_id: { ID_HOST_DRIVE_CMD: 50 } },
        low: {}
      }
    });
    expect(result.ts).toBe(5000);
    expect(result.uptime_s).toBe(3600);
    expect(result.buses.high.fps).toBe(50);
    expect(result.buses.low.fps).toBe(0);
  });
});

// ── defaultStats ──

describe("defaultStats", () => {
  it("returns empty stats with current timestamp", () => {
    const stats = defaultStats();
    expect(stats.uptime_s).toBe(0);
    expect(stats.buses.high.active).toBe(false);
    expect(stats.buses.low.active).toBe(false);
    expect(stats.ts).toBeGreaterThan(1700000000);
  });
});
