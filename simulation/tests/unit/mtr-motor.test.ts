import { describe, it, expect, beforeEach } from "vitest";
import { MtrMotorController } from "../../src/controllers/mtr-motor.js";
import type { SimFrame } from "../../src/core/types.js";

function makeFrame(canId: string, data: number[]): SimFrame {
  return {
    simTimeMs: 0, bus: "low", canId, name: canId, dlc: data.length, data, sender: "rt",
  };
}

describe("MtrMotorController", () => {
  let mtr: MtrMotorController;

  beforeEach(() => {
    mtr = new MtrMotorController();
  });

  it("starts with DAC=0, gear=N, no faults", () => {
    const s = mtr.getState();
    expect(s.dacValue).toBe(0);
    expect(s.gear).toBe(0);
    expect(s.faultFlags).toBe(0);
  });

  it("processes 0x204 drive command", () => {
    // speed=2000=0x000007D0 BE, gear=1(D)
    const frames = mtr.tick(0, [makeFrame("0x204", [0, 0, 0x07, 0xD0, 1])], 0, false);
    const s = mtr.getState();
    expect(s.dacValue).toBeGreaterThan(0);
    expect(s.gear).toBe(1);
  });

  it("sets DAC to zero on ESTOP", () => {
    mtr.tick(0, [makeFrame("0x204", [0, 0, 0x07, 0xD0, 1])], 0, false);
    expect(mtr.getState().dacValue).toBeGreaterThan(0);

    const frames = mtr.tick(1, [], 0, true);
    expect(mtr.getState().dacValue).toBe(0);
    expect(mtr.getState().faultFlags & 1).toBe(1); // ESTOP bit
  });

  it("sets CMD_TIMEOUT fault after 500ms without 0x204", () => {
    mtr.tick(0, [makeFrame("0x204", [0, 0, 0x07, 0xD0, 1])], 0, false);

    // 501ms later, no commands
    mtr.tick(501, [], 0, false);
    expect(mtr.getState().faultFlags & 2).toBe(2); // CMD_TIMEOUT bit
    expect(mtr.getState().dacValue).toBe(0);
  });

  it("produces 0x120 at 100Hz (every 10ms)", () => {
    const frames = mtr.tick(10, [], 500, false); // speed=500mm/s
    const f120 = frames.find(f => f.canId === "0x120");
    expect(f120).toBeDefined();
    expect(f120!.dlc).toBe(2);
  });

  it("produces 0x206 at 50Hz (every 20ms)", () => {
    const frames = mtr.tick(20, [], 500, false);
    const f206 = frames.find(f => f.canId === "0x206");
    expect(f206).toBeDefined();
    expect(f206!.dlc).toBe(4);
  });

  it("does not produce frames on non-periodic ticks", () => {
    const frames = mtr.tick(3, [], 0, false);
    expect(frames).toHaveLength(0);
  });

  it("gap #16: 0x204 staleness does not trigger during startup grace period", () => {
    // Within first 3 seconds, no 0x204 received — should NOT trigger CMD_TIMEOUT
    mtr.tick(1000, [], 0, false); // t=1s, no setpoint
    const state1 = mtr.getState();
    expect(state1.faultFlags & 0x02).toBe(0); // CMD_TIMEOUT not set

    // After 3 seconds, should trigger
    mtr.tick(3500, [], 0, false); // t=3.5s, still no setpoint
    const state2 = mtr.getState();
    expect(state2.faultFlags & 0x02).not.toBe(0); // CMD_TIMEOUT now set
  });
});
