import { describe, expect, it, beforeAll } from "vitest";
import { decoder, initCanDatabase } from "../src/can";
import fs from "fs";
import path from "path";

beforeAll(() => {
  const high = fs.readFileSync(path.join(__dirname, "../../../shared/can/can_high.yaml"), "utf8");
  const low = fs.readFileSync(path.join(__dirname, "../../../shared/can/can_low.yaml"), "utf8");
  initCanDatabase(high, low);
});

describe("Golden Vectors (Signed 32-bit, Enums, Endianness)", () => {
  it("32-bit signed maximum (2147483647) - 0x300 HOST_DRIVE_CMD", () => {
    const def = (decoder as any).messages.get("high:0x300");
    const speedField = def.fields.find((f: any) => f.key === "speed_mmps");
    const oldMin = speedField.min, oldMax = speedField.max;
    delete speedField.min; delete speedField.max;

    // 2147483647 = 0x7FFFFFFF
    // byte_order = motorola (Big Endian)
    const result = decoder.encode("high", "0x300", { speed_mmps: 2147483647, yaw_rate_mrad_s: 0, gear: 1 });
    expect(result.data).toEqual([0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01]);
    const decoded = decoder.decode("high", "0x300", result.data);
    expect(decoded.speed_mmps).toBe(2147483647);
    speedField.min = oldMin; speedField.max = oldMax;
  });

  it("32-bit signed minimum (-2147483648) - 0x300 HOST_DRIVE_CMD", () => {
    const def = (decoder as any).messages.get("high:0x300");
    const speedField = def.fields.find((f: any) => f.key === "speed_mmps");
    const oldMin = speedField.min, oldMax = speedField.max;
    delete speedField.min; delete speedField.max;

    // -2147483648 = 0x80000000
    const result = decoder.encode("high", "0x300", { speed_mmps: -2147483648, yaw_rate_mrad_s: 0, gear: 1 });
    expect(result.data).toEqual([0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]);
    const decoded = decoder.decode("high", "0x300", result.data);
    expect(decoded.speed_mmps).toBe(-2147483648);
    speedField.min = oldMin; speedField.max = oldMax;
  });

  it("32-bit signed -1 - 0x300 HOST_DRIVE_CMD", () => {
    const def = (decoder as any).messages.get("high:0x300");
    const speedField = def.fields.find((f: any) => f.key === "speed_mmps");
    const oldMin = speedField.min, oldMax = speedField.max;
    delete speedField.min; delete speedField.max;

    // -1 = 0xFFFFFFFF
    const result = decoder.encode("high", "0x300", { speed_mmps: -1, yaw_rate_mrad_s: 0, gear: 1 });
    expect(result.data).toEqual([0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01]);
    const decoded = decoder.decode("high", "0x300", result.data);
    expect(decoded.speed_mmps).toBe(-1);
    speedField.min = oldMin; speedField.max = oldMax;
  });

  it("32-bit signed zero - 0x300 HOST_DRIVE_CMD", () => {
    const result = decoder.encode("high", "0x300", { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 });
    expect(result.data).toEqual([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]);
    const decoded = decoder.decode("high", "0x300", result.data);
    expect(decoded.speed_mmps).toBe(0);
  });

  it("Intel (Little Endian) 16-bit signed -1 raw value - 0x169 VCU_SES_REQ", () => {
    const def = (decoder as any).messages.get("low:0x169");
    const angleField = def.fields.find((f: any) => f.key === "target_angle");
    const oldMin = angleField.min, oldMax = angleField.max;
    delete angleField.min; delete angleField.max;

    // target_angle is byte: 2, bit_offset: 0, size: 16, type: signed, factor: 0.1, offset: -3000
    // To encode a raw value of -1 (0xFFFF), we need physical value = -1 * 0.1 - 3000 = -3000.1
    // Little endian of 0xFFFF is byte 2 = 0xFF, byte 3 = 0xFF
    const result = decoder.encode("low", "0x169", { control_enable: true, target_angle: -3000.1, target_speed: 200 });
    expect(result.data[2]).toBe(0xFF);
    expect(result.data[3]).toBe(0xFF);
    const decoded = decoder.decode("low", "0x169", result.data);
    expect(decoded.target_angle).toBe(-3000.1);
    angleField.min = oldMin; angleField.max = oldMax;
  });

  it("Motorola enum option mapping - 0x210 STATE_RPT", () => {
    // mode is byte: 0, bit_offset: 0, size: 8 (mode 2 is ESTOP)
    // safety_state is byte: 1, bit_offset: 0, size: 2 (safety_state 2 is Fault)
    const result = decoder.encode("high", "0x210", { mode: 2, safety_state: 2 });
    const decoded = decoder.decode("high", "0x210", result.data);
    expect(decoded.mode).toBe(2);
    expect(decoded.mode_name).toBe("ESTOP");
    expect(decoded.safety_state).toBe(2);
    expect(decoded.safety_state_name).toBe("Fault");
  });
});
