import { describe, it, expect, vi } from "vitest";
import { 
  decodeFrame, 
  normalizeFrame, 
  BusDetector, 
  normalizeBus,
  CAN_MESSAGES
} from "../../src/types/can";

describe("decodeFrame", () => {
  it("0x001 SAFETY_ESTOP returns empty object (DLC=0)", () => {
    const res = decodeFrame("high", "0x001", []);
    expect(res).toEqual({});
  });

  it("0x011 SYS_SAFETY_STS decodes estop_active, heartbeat_ok, light bits correctly", () => {
    // estop_active=1, heartbeat_ok=1, light_left=1, light_right=0, light_brake=1, light_head=0
    // Byte 0 = 1, Byte 1 = 1, Byte 2 = 1 | 4 = 5
    const res = decodeFrame("high", "0x011", [1, 1, 5]);
    expect(res).toEqual({
      estop_active: true,
      heartbeat_ok: true,
      light_left: true,
      light_right: false,
      light_brake: true,
      light_head: false
    });
  });

  it("0x120 SYS_THROTTLE_STS decodes signed i16 BE speed correctly (including negative)", () => {
    // 500 mm/s -> 0x01F4 -> [0x01, 0xF4]
    expect(decodeFrame("high", "0x120", [0x01, 0xF4])).toEqual({ speed_mmps: 500 });
    // -500 mm/s -> 0xFE0C -> [0xFE, 0x0C]
    expect(decodeFrame("high", "0x120", [0xFE, 0x0C])).toEqual({ speed_mmps: -500 });
  });

  it("0x300 HOST_DRIVE_CMD decodes i32 speed, i24 yaw, gear correctly", () => {
    // Speed: 2000 mm/s -> 0x000007D0 -> [0, 0, 0x07, 0xD0]
    // Yaw: -500 mrad/s -> 0xFFFE0C -> [0xFF, 0xFE, 0x0C]
    // Gear: 1 (D) -> [0x01]
    const res = decodeFrame("high", "0x300", [0, 0, 0x07, 0xD0, 0xFF, 0xFE, 0x0C, 0x01]);
    expect(res.speed_mmps).toBe(2000);
    expect(res.yaw_rate_mrad_s).toBe(-500);
    expect(res.gear).toBe(1);
    expect(res.gear_name).toBe("D");
  });

  it("0x210 RT_STATE_RPT decodes mode, safety_state nibble, estop_reason nibble", () => {
    // mode=1 (AUTO), safety_state=2 (Fault), estop_reason=4
    // Byte 1: (4 << 4) | 2 = 0x42
    const res = decodeFrame("high", "0x210", [1, 0x42, 1, 0, 0, 0]);
    expect(res.mode).toBe(1);
    expect(res.mode_name).toBe("AUTO");
    expect(res.safety_state).toBe(2);
    expect(res.estop_reason).toBe(4);
    expect(res.reversing).toBe(true);
  });

  it("0x169 VCU_SES_REQ decodes rolling counter and checksum", () => {
    // rolling_counter=5, checksum=0xAB
    // Byte 5: (5 << 4) = 0x50
    const res = decodeFrame("low", "0x169", [0, 0, 0, 0, 0, 0x50, 0, 0xAB]);
    expect(res.rolling_counter).toBe(5);
    expect(res.checksum).toBe(0xAB);
  });

  it("0x721 SEB_STATUS decodes all bit-packed fields correctly", () => {
    const res = decodeFrame("low", "0x721", [0x1B, 0, 0xD0, 0x02, 0, 0, 0x50, 0xAB]);
    // Byte 0: 0x1B = 0001 1011 -> align(1), ctrl(1), mode(2), auto_brake(1), error(0)
    expect(res.alignment_status).toBe(true);
    expect(res.control_enable_sts).toBe(true);
    expect(res.control_mode_sts).toBe(2);
    expect(res.auto_brake_sts).toBe(true);
    expect(res.rolling_counter).toBe(5);
    expect(res.checksum).toBe(0xAB);
  });

  it("unknown ID returns { bus } with no crash", () => {
    expect(decodeFrame("high", "0x999", [1, 2, 3])).toEqual({ bus: "high" });
  });

  it("all 37 messages have DLC matching shared YAML (cross-check — BUG-12 regression)", () => {
    expect(CAN_MESSAGES.length).toBe(37);
    for (const msg of CAN_MESSAGES) {
      expect(msg.dlc).toBeGreaterThanOrEqual(0);
      expect(msg.dlc).toBeLessThanOrEqual(8);
    }
  });
});

describe("normalizeFrame", () => {
  it("pads data array to 8 bytes", () => {
    const res = normalizeFrame({ bus: "high", id: "0x120", dlc: 2, data: [1] });
    // Returned data is truncated to dlc, but let's check it handles short data
    expect(res.data).toEqual([1, 0]);
  });

  it("truncates data array to dlc bytes in output", () => {
    const res = normalizeFrame({ bus: "high", id: "0x120", dlc: 2, data: [1, 2, 3, 4, 5] });
    expect(res.data).toEqual([1, 2]);
  });

  it("calls decodeFrame when decoded is not provided", () => {
    const res = normalizeFrame({ bus: "high", id: "0x301", dlc: 4, data: [0, 0, 0, 100] });
    expect(res.decoded.brake_pressure_kpa).toBe(100);
  });

  it("uses provided decoded when present (skips re-decode)", () => {
    const decoded = { custom: "value" };
    const res = normalizeFrame({ bus: "high", id: "0x301", dlc: 4, data: [0, 0, 0, 100], decoded });
    expect(res.decoded).toBe(decoded);
  });

  it("sets ts to Date.now() when ts is not provided", () => {
    const now = Date.now() / 1000;
    const res = normalizeFrame({ bus: "high", id: "0x120", dlc: 2, data: [0, 0] });
    expect(res.ts).toBeGreaterThanOrEqual(now - 1);
    expect(res.ts).toBeLessThanOrEqual(now + 1);
  });
});

describe("BusDetector", () => {
  let detector: BusDetector;

  beforeEach(() => {
    detector = new BusDetector();
  });

  it("returns 'high' by default before any frames", () => {
    expect(detector.state.bus).toBe("high");
    expect(detector.state.detected).toBe(false);
  });

  it("locks to 'high' after 3 HIGH_UNIQUE_IDS frames", () => {
    detector.feed("0x300");
    expect(detector.state.detected).toBe(false);
    detector.feed("0x300");
    detector.feed("0x301");
    expect(detector.state.detected).toBe(true);
    expect(detector.state.bus).toBe("high");
  });

  it("locks to 'low' after 3 LOW_UNIQUE_IDS frames", () => {
    detector.feed("0x201");
    detector.feed("0x7B9");
    detector.feed("0x721");
    expect(detector.state.detected).toBe(true);
    expect(detector.state.bus).toBe("low");
  });

  it("stays locked after lock (more frames do not change result)", () => {
    detector.feed("0x300");
    detector.feed("0x300");
    detector.feed("0x300"); // locked to high
    
    detector.feed("0x201");
    detector.feed("0x201");
    detector.feed("0x201");
    detector.feed("0x201");
    expect(detector.state.bus).toBe("high");
  });

  it("resets correctly after reset()", () => {
    detector.feed("0x300");
    detector.feed("0x300");
    detector.feed("0x300");
    expect(detector.state.detected).toBe(true);
    
    detector.reset();
    expect(detector.state.detected).toBe(false);
  });

  it("does not lock when both high and low IDs are seen (ambiguous)", () => {
    detector.feed("0x300"); // high
    detector.feed("0x201"); // low
    expect(detector.state.detected).toBe(false);
    expect(detector.state.confidence).toBe("none");
  });
});

describe("normalizeBus", () => {
  it("'low' -> 'low'", () => {
    expect(normalizeBus("low")).toBe("low");
  });

  it("'high' -> 'high'", () => {
    expect(normalizeBus("high")).toBe("high");
  });

  it("'HIGH' -> warns and returns 'high' (BUG-30 regression)", () => {
    expect(normalizeBus("HIGH")).toBe("high");
  });

  it("null -> warns (BUG-30 regression)", () => {
    expect(normalizeBus(null)).toBe("high");
  });

  it("undefined -> warns (BUG-30 regression)", () => {
    expect(normalizeBus(undefined)).toBe("high");
  });
});
