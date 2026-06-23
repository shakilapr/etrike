import { describe, expect, it } from "vitest";
import {
  BUSES,
  CAN_MESSAGES,
  encodePayload,
  findMessage,
  formatBytes,
  formatDecoded,
  frameTime,
  getMessageName,
  normalizeBus,
  normalizeCanId,
  type CanMessageDef
} from "./can-decoder";

describe("normalizeCanId", () => {
  it("formats numeric hex strings", () => {
    expect(normalizeCanId("0x300")).toBe("0x300");
    expect(normalizeCanId("300")).toBe("0x300");
    expect(normalizeCanId("0x001")).toBe("0x001");
  });

  it("handles lowercase input", () => {
    expect(normalizeCanId("0x7fc")).toBe("0x7FC");
  });

  it("handles non-hex strings", () => {
    // "abc" is valid hex, so it normalizes to "0xABC"
    expect(normalizeCanId("abc")).toBe("0xABC");
  });
});

describe("normalizeBus", () => {
  it('returns "low" for "low"', () => {
    expect(normalizeBus("low")).toBe("low");
  });

  it('defaults to "high"', () => {
    expect(normalizeBus("high")).toBe("high");
    expect(normalizeBus(undefined)).toBe("high");
    expect(normalizeBus("invalid")).toBe("high");
  });
});

describe("CAN_MESSAGES catalog", () => {
  it("has 35 messages (13 high + 22 low)", () => {
    expect(CAN_MESSAGES).toHaveLength(35);
  });

  it("every message has a bus field", () => {
    for (const msg of CAN_MESSAGES) {
      expect(BUSES).toContain(msg.bus);
    }
  });

  it("high bus has 13 messages", () => {
    expect(CAN_MESSAGES.filter((m) => m.bus === "high")).toHaveLength(13);
  });

  it("low bus has 22 messages", () => {
    expect(CAN_MESSAGES.filter((m) => m.bus === "low")).toHaveLength(22);
  });
});

describe("findMessage", () => {
  it("finds a message by bus + id", () => {
    const msg = findMessage("high", "0x300");
    expect(msg).toBeDefined();
    expect(msg!.name).toBe("HOST_DRIVE_CMD");
    expect(msg!.bus).toBe("high");
  });

  it("falls back to id-only search across buses", () => {
    // 0x300 is only on high bus, but findMessage should find it even if we pass "low"
    const msg = findMessage("low", "0x300");
    expect(msg).toBeDefined();
  });

  it("returns undefined for unknown IDs", () => {
    expect(findMessage("high", "0x999")).toBeUndefined();
  });
});

describe("getMessageName", () => {
  it("returns the name for known IDs", () => {
    expect(getMessageName("low", "0x169")).toBe("VCU_SES_REQ");
  });

  it("returns UNKNOWN_ prefix for unknown IDs", () => {
    const name = getMessageName("high", "0x999");
    expect(name).toContain("UNKNOWN_");
  });
});

describe("encodePayload", () => {
  it("encodes 0x300 drive command (high bus)", () => {
    const result = encodePayload("high", "0x300", {
      speed_mmps: 2000,
      yaw_rate_mrad_s: 0,
      gear: 1
    });
    expect(result.dlc).toBe(8);
    expect(result.data).toHaveLength(8);
    // speed_mmps = 2000 = 0x000007D0 big-endian
    expect(result.data[0]).toBe(0x00);
    expect(result.data[1]).toBe(0x00);
    expect(result.data[2]).toBe(0x07);
    expect(result.data[3]).toBe(0xD0);
    // gear = 1 in byte 7
    expect(result.data[7]).toBe(1);
  });

  it("encodes 0x001 ESTOP (empty payload)", () => {
    const result = encodePayload("high", "0x001", {});
    expect(result.dlc).toBe(0);
    expect(result.data).toHaveLength(0);
  });

  it("encodes 0x011 safety status (high bus)", () => {
    const result = encodePayload("high", "0x011", {
      estop_active: true,
      heartbeat_ok: false
    });
    expect(result.dlc).toBe(2);
    expect(result.data[0]).toBe(1); // estop_active
    expect(result.data[1]).toBe(0); // heartbeat_ok
  });

  it("encodes 0x120 throttle (high bus)", () => {
    const result = encodePayload("high", "0x120", {
      speed_mmps: -500
    });
    expect(result.dlc).toBe(2);
    // -500 as i16 BE = 0xFE0C
    expect(result.data[0]).toBe(0xFE);
    expect(result.data[1]).toBe(0x0C);
  });

  it("encodes 0x301 brake request", () => {
    const result = encodePayload("high", "0x301", {
      brake_pressure_kpa: 5000
    });
    expect(result.dlc).toBe(4);
    // 5000 = 0x00001388
    expect(result.data[0]).toBe(0x00);
    expect(result.data[1]).toBe(0x00);
    expect(result.data[2]).toBe(0x13);
    expect(result.data[3]).toBe(0x88);
  });

  it("encodes 0x302 light command", () => {
    const result = encodePayload("high", "0x302", {
      left_turn: true,
      right_turn: false,
      brake_light: true,
      headlight: false
    });
    expect(result.dlc).toBe(1);
    expect(result.data[0]).toBe(0x05); // 0b0101
  });

  it("encodes 0x204 RT drive (low bus)", () => {
    const result = encodePayload("low", "0x204", {
      motor_speed_mmps: 2000,
      gear: 1
    });
    expect(result.dlc).toBe(5);
    // speed 2000 = 0x000007D0 big-endian
    expect(result.data[0]).toBe(0x00);
    expect(result.data[1]).toBe(0x00);
    expect(result.data[2]).toBe(0x07);
    expect(result.data[3]).toBe(0xD0);
    expect(result.data[4]).toBe(1); // gear
  });

  it("encodes 0x169 steer request (low bus)", () => {
    const result = encodePayload("low", "0x169", {
      control_enable: true,
      alignment_enable: false,
      target_angle: -3000,
      target_speed: 328,
      rolling_counter: 1,
      checksum: 0
    });
    expect(result.dlc).toBe(8);
    expect(result.data[0]).toBe(0x02); // control_enable
  });

  it("returns empty for unknown IDs", () => {
    const result = encodePayload("high", "0x999", {});
    expect(result.dlc).toBe(0);
  });
});

describe("formatBytes", () => {
  it("formats bytes as hex", () => {
    expect(formatBytes([0x00, 0x07, 0xD0])).toBe("00 07 D0");
  });

  it("returns '--' for empty array", () => {
    expect(formatBytes([])).toBe("--");
  });
});

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
    const result = formatDecoded({
      speed_mmps: 2000,
      gear_name: "D",
      mode_label: "AUTO"
    });
    expect(result).toContain("speed=2000");
    expect(result).not.toContain("gear_name");
    expect(result).not.toContain("mode_label");
  });
});

describe("frameTime", () => {
  it("formats a valid timestamp", () => {
    const frame = { ts: 1700000000, bus: "high" as const, id: "0x300", name: "TEST", dlc: 8, data: [], decoded: {}, ts_real: 1700000000 };
    const time = frameTime(frame);
    expect(time).toMatch(/^\d{2}:\d{2}:\d{2}\.\d{3}$/);
  });
});
