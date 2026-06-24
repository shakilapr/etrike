import { describe, it, expect, beforeEach } from "vitest";
import { SyntreeEpsc } from "../../src/ecus/epsc.js";
import type { SimFrame } from "../../src/core/types.js";
import type { SimulationContext } from "../../src/ecus/base.js";

function ctx(): SimulationContext {
  return { nowMs: 0, ticks: 0, mode: "auto", estopActive: false, brakeLeverPressed: false };
}

function make0x169(nowMs: number): SimFrame {
  return {
    simTimeMs: nowMs, bus: "low", canId: "0x169", name: "VCU_SES_REQ",
    dlc: 8, data: [3, 0, 0, 0, 0, 0, 0, 0], sender: "rt",
  };
}

describe("SyntreeEpsc", () => {
  let epsc: SyntreeEpsc;

  beforeEach(() => {
    epsc = new SyntreeEpsc();
    epsc.init();
    epsc.setActualAngle(0);
  });

  it("reports aligned on init", () => {
    const frames = epsc.tick(10, [], [make0x169(0)], ctx());
    const f201 = frames.find(f => f.canId === "0x201");
    expect(f201).toBeDefined();
    // status byte bit 0 = aligned
    expect(f201!.data[0] & 1).toBe(1);
  });

  it("produces 0x201 at 100Hz", () => {
    const frames = epsc.tick(10, [], [make0x169(0)], ctx());
    expect(frames.some(f => f.canId === "0x201")).toBe(true);
    expect(frames.some(f => f.canId === "0x6FA")).toBe(true);
  });

  it("produces 0x202 at 10Hz", () => {
    const frames = epsc.tick(100, [], [make0x169(0)], ctx());
    expect(frames.some(f => f.canId === "0x202")).toBe(true);
  });

  it("produces 0x203 at 1Hz", () => {
    const frames = epsc.tick(1000, [], [make0x169(0)], ctx());
    const f203 = frames.find(f => f.canId === "0x203");
    expect(f203).toBeDefined();
    expect(f203!.dlc).toBe(8);
  });

  it("sets L3 error after 20ms without 0x169", () => {
    // First receive a command
    epsc.tick(0, [], [make0x169(0)], ctx());
    // Then 25ms later with no command
    const frames = epsc.tick(25, [], [], ctx());
    const f201 = frames.find(f => f.canId === "0x201");
    if (f201) {
      // bits 6-7 should be 3 for L3
      expect((f201.data[0] >> 6) & 3).toBe(3);
    }
  });
});
