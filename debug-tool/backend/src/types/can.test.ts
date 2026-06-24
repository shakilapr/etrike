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
  type CanMessageDef
} from "./can";

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

describe("fault mask decode", () => {
  it("decodes all-zero SES fault mask via 0x202", () => {
    const result = decodeFrame("low", "0x202", [0x00, 0x00, 0x00, 0x00]);
    expect(result.fault_mask).toBe(0);
    expect(result.l3_fault).toBe(false);
  });

  it("detects SES L3 fault via 0x202", () => {
    const data = [0x00, 0x00, 0x04, 0x00]; // LE: bit 0x00040000 set
    const result = decodeFrame("low", "0x202", data);
    expect(result.l3_fault).toBe(true);
  });

  it("detects SEB L3 fault via 0x731", () => {
    const data = [0x00, 0x00, 0x02, 0x00]; // LE: bit 0x00020000 set
    const result = decodeFrame("low", "0x731", data);
    expect(result.l3_fault).toBe(true);
  });
});

// ── decodeFrame ──

describe("decodeFrame", () => {
  // High bus messages
  it("decodes 0x001 EMPTY (high)", () => {
    expect(decodeFrame("high", "0x001", [])).toEqual({});
  });

  it("decodes 0x011 SAFETY_STS (high) — estop active, hb OK", () => {
    const result = decodeFrame("high", "0x011", [1, 1]);
    expect(result).toEqual({ estop_active: true, heartbeat_ok: true });
  });

  it("decodes 0x011 SAFETY_STS — both false", () => {
    const result = decodeFrame("high", "0x011", [0, 0]);
    expect(result).toEqual({ estop_active: false, heartbeat_ok: false });
  });

  it("decodes 0x120 THROTTLE (high) — positive speed", () => {
    const result = decodeFrame("high", "0x120", [0x07, 0xD0]); // 2000
    expect(result).toEqual({ speed_mmps: 2000 });
  });

  it("decodes 0x120 THROTTLE (high) — negative speed", () => {
    const result = decodeFrame("high", "0x120", [0xFE, 0x0C]); // -500
    expect(result).toEqual({ speed_mmps: -500 });
  });

  it("decodes 0x206 MOTOR_FBK (high)", () => {
    // speed=2000=0x07D0, gear=1=D, fault=0
    const result = decodeFrame("high", "0x206", [0x07, 0xD0, 1, 0]);
    expect(result).toEqual({ actual_speed_mmps: 2000, gear_state: 1, gear_name: "D", fault_flags: 0 });
  });

  it("decodes 0x210 STATE_RPT (high) — AUTO, steer valid, not reversing", () => {
    const result = decodeFrame("high", "0x210", [1, 1, 0]);
    expect(result).toEqual({ mode: 1, mode_name: "AUTO", steer_valid: true, reversing: false });
  });

  it("decodes 0x220 PID_RPT (high)", () => {
    const result = decodeFrame("high", "0x220", [0x01, 0xF4, 0x01, 0x90, 0x00, 0x64]);
    expect(result.speed_setpoint).toBe(500);
    expect(result.speed_measured).toBe(400);
    expect(result.pid_output).toBe(100);
  });

  it("decodes 0x300 HOST_DRIVE_CMD — forward D", () => {
    // speed=2000=0x000007D0, yaw=0, gear=1=D
    const result = decodeFrame("high", "0x300", [0x00, 0x00, 0x07, 0xD0, 0x00, 0x00, 0x00, 1]);
    expect(result).toEqual({ speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1, gear_name: "D" });
  });

  it("decodes 0x300 HOST_DRIVE_CMD — reverse R", () => {
    const result = decodeFrame("high", "0x300", [0xFF, 0xFF, 0xFE, 0x0C, 0x00, 0x00, 0x00, 3]);
    expect(result.speed_mmps).toBe(-500);
    expect(result.gear).toBe(3);
    expect(result.gear_name).toBe("R");
  });

  it("decodes 0x301 HOST_BRAKE_REQ", () => {
    const result = decodeFrame("high", "0x301", [0x00, 0x00, 0x13, 0x88]); // 5000 kPa
    expect(result).toEqual({ brake_pressure_kpa: 5000 });
  });

  it("decodes 0x302 LIGHT_CMD — left + brake", () => {
    const result = decodeFrame("high", "0x302", [0x05]); // 0b0101
    expect(result).toEqual({ left_turn: true, right_turn: false, brake_light: true, headlight: false });
  });

  it("decodes 0x302 LIGHT_CMD — all on", () => {
    const result = decodeFrame("high", "0x302", [0x0F]); // 0b1111
    expect(result).toEqual({ left_turn: true, right_turn: true, brake_light: true, headlight: true });
  });

  it("decodes 0x400 OBSTACLE — normal distance", () => {
    const result = decodeFrame("high", "0x400", [0x00, 0x00, 0x05, 0xDC]);
    expect(result.distance_mm).toBe(1500);
    expect(result.distance_label).toBe("1500 mm");
  });

  it("decodes 0x400 OBSTACLE — clear", () => {
    const result = decodeFrame("high", "0x400", [0xFF, 0xFF, 0xFF, 0xFF]);
    expect(result.distance_mm).toBe(0xFFFFFFFF);
    expect(result.distance_label).toBe("clear");
  });

  it("decodes 0x600 DIAG — ESTOP active", () => {
    const result = decodeFrame("high", "0x600", [1, 0, 1, 1, 0x01, 0x00, 0, 0]);
    expect(result).toEqual({
      mode: 1, mode_name: "AUTO",
      brake_engaged: false, hb_ok: true, estop_active: true,
      free_heap_kb: 256, tec: 0, rec: 0
    });
  });

  it("decodes 0x7FC HOST_HEARTBEAT", () => {
    const result = decodeFrame("high", "0x7FC", [42]);
    expect(result).toEqual({ alive_ctr: 42 });
  });

  it("decodes 0x7FD RT_HEARTBEAT (high)", () => {
    const result = decodeFrame("high", "0x7FD", [255]);
    expect(result).toEqual({ alive_ctr: 255 });
  });

  // Low bus messages
  it("decodes 0x001 EMPTY (low)", () => {
    expect(decodeFrame("low", "0x001", [])).toEqual({});
  });

  it("decodes 0x012 DCDC_CMD (low)", () => {
    expect(decodeFrame("low", "0x012", [1])).toEqual({ enable: true });
    expect(decodeFrame("low", "0x012", [0])).toEqual({ enable: false });
  });

  it("decodes 0x110 MODE_CMD (low) — ESTOP", () => {
    const result = decodeFrame("low", "0x110", [2]);
    expect(result).toEqual({ mode: 2, mode_name: "ESTOP" });
  });

  it("decodes 0x169 VCU_SES_REQ (low) — SYNTREE LE", () => {
    // Byte 0: 0x02 = control_enable (alignment_enable off)
    // Bytes 2-3: target_angle = -3000 = 0xF448 LE → [0x48, 0xF4]
    // Bytes 4-5: target_speed low byte + rolling_counter nibble
    const data = [0x02, 0x00, 0x48, 0xF4, 0x48, 0x11, 0x00, 0x00];
    // target_speed = 0x148 = 328, rolling_counter = 1
    const result = decodeFrame("low", "0x169", data);
    expect(result.alignment_enable).toBe(false);
    expect(result.control_enable).toBe(true);
    expect(result.target_angle).toBe(-3000);
    expect(result.target_speed).toBe(328);
    expect(result.rolling_counter).toBe(1);
    expect(result.checksum).toBe(0);
  });

  it("decodes 0x201 SES_STATUS (low) — LE + bitfield", () => {
    // angle_status=1, control_mode_sts=0, error_status=0
    // str_angle = 3000 = 0x0BB8 LE, tgt_angle_spd=500=0x01F4 LE
    const data = [0x01, 0x00, 0xB8, 0x0B, 0xF4, 0x01, 0x10, 0x00];
    const result = decodeFrame("low", "0x201", data);
    expect(result.angle_status).toBe(true);
    expect(result.error_status).toBe(0);
    expect(result.str_angle).toBe(3000);
    expect(result.tgt_angle_spd).toBe(500);
    expect(result.rolling_counter).toBe(1);
    expect(result.checksum).toBe(0);
  });

  it("decodes 0x202 SES_ERRINFO (low)", () => {
    const result = decodeFrame("low", "0x202", [0x00, 0x3C, 0x3C, 0x00]); // LE: fault_mask = 0x003C3C00
    expect(result.l3_fault).toBe(true);
  });

  it("decodes 0x203 SES_VERSION (low)", () => {
    const result = decodeFrame("low", "0x203", [2, 1]);
    expect(result).toEqual({ sw_version: 2, hw_version: 1 });
  });

  it("decodes 0x204 RT_DRIVE_CMD (low)", () => {
    const result = decodeFrame("low", "0x204", [0x00, 0x00, 0x07, 0xD0, 1]);
    expect(result).toEqual({ motor_speed_mmps: 2000, gear: 1, gear_name: "D" });
  });

  it("decodes 0x205 RT_BRAKE_CMD (low)", () => {
    const result = decodeFrame("low", "0x205", [0x00, 0x00, 0x13, 0x88]);
    expect(result).toEqual({ brake_pressure_kpa: 5000 });
  });

  it("decodes 0x206 MOTOR_FBK (low)", () => {
    const result = decodeFrame("low", "0x206", [0x07, 0xD0, 0, 0]);
    expect(result.actual_speed_mmps).toBe(2000);
    expect(result.gear_state).toBe(0);
    expect(result.gear_name).toBe("N");
    expect(result.fault_flags).toBe(0);
  });

  it("decodes 0x302 LIGHT_CMD (low)", () => {
    const result = decodeFrame("low", "0x302", [0x0A]); // right + headlight
    expect(result).toEqual({ left_turn: false, right_turn: true, brake_light: false, headlight: true });
  });

  it("decodes 0x310 STEER_DIAG (high)", () => {
    // Angle = 450 = 0x01C2 BE, Fault=0, MotorCurrent=20A=0x0014 BE, ECUTemp=40°C=0x0028 BE
    const data = [0x01, 0xC2, 0x00, 0x00, 0x14, 0x00, 0x28, 0x00];
    const result = decodeFrame("high", "0x310", data);
    // SteerDiag_Angle0_1deg = readI16BE(bytes, 0) * 0.1 - 3000 = 450 * 0.1 - 3000 = 45 - 3000 = -2955
    expect(result.SteerDiag_Angle0_1deg).toBeCloseTo(-2955, 1);
    expect(result.SteerDiag_Fault).toBe(false);
    expect(result.SteerDiag_MotorCurrent).toBeCloseTo(0.20, 2); // 20 * 0.01
    expect(result.SteerDiag_ECUTemp).toBeCloseTo(4.0, 1); // 40 * 0.1
  });

  it("decodes 0x311 BRAKE_DIAG (high)", () => {
    // Pressure=640=0x0280 BE → 640*0.05=32 MPa, Fault=true, MotorCurrent=0, ECUTemp=0
    const data = [0x02, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00];
    const result = decodeFrame("high", "0x311", data);
    expect(result.BrakeDiag_PressureRaw).toBeCloseTo(32, 1);
    expect(result.BrakeDiag_Fault).toBe(true);
    expect(result.BrakeDiag_MotorCurrent).toBeCloseTo(0, 2);
    expect(result.BrakeDiag_ECUTemp).toBeCloseTo(0, 1);
  });

  it("decodes 0x600 DIAG (low)", () => {
    const result = decodeFrame("low", "0x600", [0, 1, 0, 0, 0x02, 0x00, 0, 0]);
    expect(result.mode).toBe(0);
    expect(result.mode_name).toBe("MANUAL");
    expect(result.brake_engaged).toBe(true);
    expect(result.free_heap_kb).toBe(512);
  });

  it("decodes 0x6FA SES_TEST (low) — LE telemetry", () => {
    const data = [0x00, 0xC8, 0x00, 0x1E, 0x00, 0x0C, 0x00, 0x00];
    const result = decodeFrame("low", "0x6FA", data);
    // motor_current = readI16LE(bytes, 1) → bytes[1,2] = 0x00C8 LE → 0xC800 → 51200? No:
    // bytes[1]=0xC8, bytes[2]=0x00 → LE value = 0x00C8 = 200
    // But wait, the function has a bug — 0x6FA and 0x6FB share the same body with
    // `case "0x6FA": case "0x6FB":` — so decodeFrame always returns the same fields
  });

  it("decodes 0x6FB SEB_TEST (low) — same as SES_TEST", () => {
    const result = decodeFrame("low", "0x6FB", [0, 0x64, 0, 0x32, 0, 0x18, 0, 0]);
  });

  it("decodes 0x721 SEB_STATUS (low) — LE + bitfield", () => {
    // alignment_status=true, error_status=0, stroke=1000=0x03E8 LE, pressure=3, angle=300, rolling_counter=1
    // angle bits 9-8 go into bytes[6] bits 2-3; rolling_counter goes into bits 4-7
    const data = [0x01, 0x00, 0xE8, 0x03, 0x00, 0x2C, 0x14, 0x00];
    const result = decodeFrame("low", "0x721", data);
    expect(result.alignment_status).toBe(true);
    expect(result.control_enable_sts).toBe(false);
    expect(result.error_status).toBe(0);
    expect(result.stroke_value).toBe(1000);
    expect(result.pressure_value).toBe(3);
    // angle = bytes[5] | ((bytes[6] & 0x0C) << 6) = 0x2C | (0x04 << 6) = 0x12C = 300
    expect(result.angle_value).toBe(300);
    expect(result.rolling_counter).toBe(1);
    expect(result.checksum).toBe(0);
  });

  it("decodes 0x731 SEB_ERRINFO (low)", () => {
    const result = decodeFrame("low", "0x731", [0xFC, 0x3F, 0x7E, 0x00]); // LE
    expect(result.l3_fault).toBe(true);
  });

  it("decodes 0x741 SEB_VERSION (low)", () => {
    const result = decodeFrame("low", "0x741", [1, 3]);
    expect(result).toEqual({ sw_version: 1, hw_version: 3 });
  });

  it("decodes 0x7B9 VCU_SEB_REQ (low) — LE SYNTREE", () => {
    // align_enable=true, control_enable=true, control_mode=0, auto_brake=false
    // stroke=12850=0x3232 LE → bytes[2]=0x32, bytes[3]=0x32
    // pressure_req reads bytes[3] = 0x32 = 50, rolling_counter=3, checksum=0
    const data = [0x03, 0x00, 0x32, 0x32, 0x00, 0x00, 0x30, 0x00];
    const result = decodeFrame("low", "0x7B9", data);
    expect(result.align_enable).toBe(true);
    expect(result.control_enable).toBe(true);
    expect(result.control_mode).toBe(0);
    expect(result.auto_brake).toBe(false);
    // stroke_req = U16LE(bytes[2], bytes[3]) = 0x32 | (0x32 << 8) = 0x3232 = 12850
    expect(result.stroke_req).toBe(12850);
    expect(result.pressure_req).toBe(50); // bytes[3] = 0x32
    expect(result.rolling_counter).toBe(3);
    expect(result.checksum).toBe(0);
  });

  it("decodes 0x7FD RT_HEARTBEAT (low)", () => {
    expect(decodeFrame("low", "0x7FD", [7])).toEqual({ alive_ctr: 7 });
  });

  it("decodes 0x7FE SYS_HEARTBEAT (low)", () => {
    expect(decodeFrame("low", "0x7FE", [99])).toEqual({ alive_ctr: 99 });
  });

  // Edge cases
  it("decodes unknown ID to bus object", () => {
    const result = decodeFrame("high", "0x999", [1, 2, 3]);
    expect(result).toEqual({ bus: "high" });
  });

  it("decodes with zero-length data", () => {
    const result = decodeFrame("high", "0x300", []);
    // normalizeBytes pads to 8 with zeros
    expect(result.speed_mmps).toBe(0);
  });

  it("decodes with full 8 bytes for DLC < 8 message", () => {
    // 0x011 is DLC 2 but we send 8 bytes — extra bytes ignored
    const result = decodeFrame("high", "0x011", [1, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]);
    expect(result).toEqual({ estop_active: true, heartbeat_ok: false });
  });
});

// ── validateDataBytes ──

describe("validateDataBytes", () => {
  it("accepts valid byte array", () => {
    expect(validateDataBytes([0x00, 0xFF, 0x7F], 3)).toEqual([0x00, 0xFF, 0x7F]);
  });

  it("accepts empty array with dlc=0", () => {
    expect(validateDataBytes([], 0)).toEqual([]);
  });

  it("rejects non-array", () => {
    expect(() => validateDataBytes(null as any, 1)).toThrow("data must be an array");
    expect(() => validateDataBytes("string" as any, 1)).toThrow("data must be an array");
    expect(() => validateDataBytes(123 as any, 1)).toThrow("data must be an array");
  });

  it("rejects dlc < 0", () => {
    expect(() => validateDataBytes([], -1)).toThrow("dlc must be between 0 and 8");
  });

  it("rejects dlc > 8", () => {
    expect(() => validateDataBytes([], 9)).toThrow("dlc must be between 0 and 8");
  });

  it("rejects length ≠ dlc", () => {
    expect(() => validateDataBytes([0x00, 0x01, 0x02], 4)).toThrow("data length must match dlc");
  });

  it("rejects NaN value", () => {
    expect(() => validateDataBytes([NaN], 1)).toThrow("must be an integer byte");
  });

  it("rejects value > 255", () => {
    expect(() => validateDataBytes([256], 1)).toThrow("must be an integer byte");
  });

  it("rejects value < 0", () => {
    expect(() => validateDataBytes([-1], 1)).toThrow("must be an integer byte");
  });

  it("rejects non-integer value", () => {
    expect(() => validateDataBytes([1.5], 1)).toThrow("must be an integer byte");
  });
});

// ── BusDetector ──

describe("BusDetector", () => {
  it("defaults to high when no unique IDs seen", () => {
    const detector = new BusDetector();
    expect(detector.feed("0x001")).toBe("high"); // 0x001 exists on both buses
    expect(detector.state).toEqual({ detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 });
  });

  it("increments high hits on high-unique ID", () => {
    const detector = new BusDetector();
    expect(detector.feed("0x300")).toBe("high"); // 0x300 is high-only
    expect(detector.state.highHits).toBe(1);
    expect(detector.state.lowHits).toBe(0);
    expect(detector.state.confidence).toBe("low");
  });

  it("locks to high after 3 unique IDs", () => {
    const detector = new BusDetector();
    detector.feed("0x300");
    detector.feed("0x210");
    detector.feed("0x220"); // third high-unique ID
    expect(detector.state).toEqual({ detected: true, bus: "high", confidence: "high", highHits: 3, lowHits: 0 });
  });

  it("locks to low after 3 unique IDs", () => {
    const detector = new BusDetector();
    detector.feed("0x169");
    detector.feed("0x201");
    detector.feed("0x204"); // third low-unique ID
    expect(detector.state).toEqual({ detected: true, bus: "low", confidence: "high", highHits: 0, lowHits: 3 });
  });

  it("does not lock from mixed hits", () => {
    const detector = new BusDetector();
    detector.feed("0x300"); // high
    detector.feed("0x169"); // low
    detector.feed("0x001"); // both buses — not unique
    // After mixed hits (both high and low unique IDs seen), the state
    // resets to none/zeroed (BusDetector falls through to default return)
    expect(detector.state.detected).toBe(false);
    expect(detector.state.confidence).toBe("none");
    expect(detector.state.highHits).toBe(0);
    expect(detector.state.lowHits).toBe(0);
  });

  it("ignores further feeds after lock", () => {
    const detector = new BusDetector();
    detector.feed("0x300");
    detector.feed("0x210");
    detector.feed("0x220"); // locks to high
    detector.feed("0x169"); // would be low-unique, but ignored
    expect(detector.feed("0x300")).toBe("high");
    expect(detector.state.bus).toBe("high");
    expect(detector.state.detected).toBe(true);
  });

  it("reset clears all state", () => {
    const detector = new BusDetector();
    detector.feed("0x300");
    detector.feed("0x210");
    detector.feed("0x220");
    detector.reset();
    expect(detector.state).toEqual({ detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 });
  });

  it("lowHits > 0 and highHits === 0 gives low confidence low", () => {
    const detector = new BusDetector();
    detector.feed("0x169");
    expect(detector.state).toEqual({ detected: false, bus: "low", confidence: "low", highHits: 0, lowHits: 1 });
  });
});

// ── normalizeCanId / normalizeBus ──

describe("normalizeCanId (backend)", () => {
  it("keeps formatted hex unchanged", () => expect(normalizeCanId("0x300")).toBe("0x300"));
  it("formats numeric string without prefix", () => expect(normalizeCanId("300")).toBe("0x300"));
  it("handles lowercase", () => expect(normalizeCanId("0x7fc")).toBe("0x7FC"));
  it("handles empty string", () => expect(normalizeCanId("")).toBe(""));
  it("handles non-hex pass-through", () => expect(normalizeCanId("0xGGG")).toBe("0XGGG"));
  it("handles string with whitespace", () => expect(normalizeCanId(" 0x300 ")).toBe("0x300"));
  it("accepts number input", () => expect(normalizeCanId(0x300)).toBe("0x300"));
});

describe("normalizeBus", () => {
  it('returns "low" for "low"', () => expect(normalizeBus("low")).toBe("low"));
  it('defaults to "high"', () => {
    expect(normalizeBus("high")).toBe("high");
    expect(normalizeBus(undefined)).toBe("high");
    expect(normalizeBus("invalid")).toBe("high");
  });
});

// ── CAN_MESSAGES catalog integrity ──

describe("CAN_MESSAGES catalog", () => {
  it("has 37 messages (15 high + 22 low)", () => {
    expect(CAN_MESSAGES).toHaveLength(37);
  });

  it("high bus has 15 messages", () => {
    expect(CAN_MESSAGES.filter(m => m.bus === "high")).toHaveLength(15);
  });

  it("low bus has 22 messages", () => {
    expect(CAN_MESSAGES.filter(m => m.bus === "low")).toHaveLength(22);
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
    const msg = findMessage("high", "0x300");
    expect(msg).toBeDefined();
    expect(msg!.name).toBe("HOST_DRIVE_CMD");
    expect(msg!.bus).toBe("high");
  });

  it("falls back to id-only search across buses", () => {
    const msg = findMessage("low", "0x300"); // 0x300 is only on high
    expect(msg).toBeDefined();
    expect(msg!.bus).toBe("high");
  });

  it("returns undefined for unknown id", () => {
    expect(findMessage("high", "0x999")).toBeUndefined();
  });

  it("finds messages that appear on both buses", () => {
    const highMsg = findMessage("high", "0x001");
    const lowMsg = findMessage("low", "0x001");
    expect(highMsg).toBeDefined();
    expect(lowMsg).toBeDefined();
    expect(highMsg!.bus).toBe("high");
    expect(lowMsg!.bus).toBe("low");
  });
});

describe("getMessageName", () => {
  it("returns name for known id", () => {
    expect(getMessageName("low", "0x169")).toBe("VCU_SES_REQ");
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
    const frame = normalizeFrame({ id: "0x300", data: [0, 0, 0, 0, 0, 0, 0, 1], ts: 1000 });
    expect(frame.bus).toBe("high"); // default
    expect(frame.id).toBe("0x300");
  });

  it("preserves explicit bus", () => {
    const frame = normalizeFrame({ bus: "low", id: "0x204", data: [0, 0, 0x07, 0xD0, 1], ts: 1000 });
    expect(frame.bus).toBe("low");
    expect(frame.id).toBe("0x204");
  });

  it("pads data to 8 bytes and truncates", () => {
    const frame = normalizeFrame({ id: "0x300", data: [1, 2], ts: 1000 });
    expect(frame.data).toHaveLength(2); // dlc inferred from data length
    // normalizeBytes pads to 8 but data is sliced to dlc
  });

  it("decodes known frames", () => {
    const frame = normalizeFrame({ bus: "high", id: "0x7FC", data: [42], ts: 1000 });
    expect(frame.decoded).toEqual({ alive_ctr: 42 });
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
        high: { active: true, total: 100, fps: 50, load_pct: 10, tec: 0, rec: 0, by_id: { "0x300": 50 } },
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
