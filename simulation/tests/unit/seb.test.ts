import { describe, it, expect, beforeEach } from "vitest";
import { Bbw } from "../../src/ecus/seb.js";
import type { SimFrame } from "../../src/core/types.js";
import type { SimulationContext } from "../../src/ecus/base.js";

function ctx(): SimulationContext {
  return { nowMs: 0, ticks: 0, mode: "auto", estopActive: false, brakeLeverPressed: false };
}

function make0x7B9(nowMs: number): SimFrame {
  return {
    simTimeMs: nowMs, bus: "low", canId: "0x7B9", name: "VCU_SEB_REQ",
    dlc: 8, data: [7, 0, 0, 0, 0, 0, 0, 0], sender: "sys",
  };
}

describe("Bbw", () => {
  let seb: Bbw;

  beforeEach(() => {
    seb = new Bbw();
    seb.init();
    seb.setActualStroke(0);
  });

  it("reports aligned on init", () => {
    const frames = seb.tick(10, [], [make0x7B9(0)], ctx());
    const f721 = frames.find(f => f.canId === "0x721");
    expect(f721).toBeDefined();
    expect(f721!.data[0] & 1).toBe(1); // aligned
  });

  it("produces 0x721 at 100Hz", () => {
    const frames = seb.tick(10, [], [make0x7B9(0)], ctx());
    expect(frames.some(f => f.canId === "0x721")).toBe(true);
    expect(frames.some(f => f.canId === "0x6FB")).toBe(true);
  });

  it("produces 0x731 at 10Hz", () => {
    const frames = seb.tick(100, [], [make0x7B9(0)], ctx());
    expect(frames.some(f => f.canId === "0x731")).toBe(true);
  });

  it("produces 0x741 at 1Hz", () => {
    const frames = seb.tick(1000, [], [make0x7B9(0)], ctx());
    const f741 = frames.find(f => f.canId === "0x741");
    expect(f741).toBeDefined();
    expect(f741!.dlc).toBe(8);
  });

  it("sets L3 error after 20ms without 0x7B9", () => {
    // Feed command at t=0, then tick past 500ms startup grace
    seb.tick(0, [], [make0x7B9(0)], ctx());
    seb.tick(510, [], [make0x7B9(0)], ctx());
    // Then 30ms without command (>20ms L3 timeout, aligned to 10ms tick)
    const frames = seb.tick(540, [], [], ctx());
    const f721 = frames.find(f => f.canId === "0x721");
    expect(f721).toBeDefined();  // L3 error must produce a 0x721 frame
    expect((f721!.data[0] >> 6) & 3).toBe(3);
  });

  it("reflects actual stroke in 0x721", () => {
    seb.setActualStroke(15); // 15mm → raw = (15+30)/0.05 = 900
    const frames = seb.tick(10, [], [make0x7B9(0)], ctx());
    const f721 = frames.find(f => f.canId === "0x721");
    // stroke raw value in bytes 2-3 (LE)
    const strokeRaw = f721!.data[2] | (f721!.data[3] << 8);
    expect(strokeRaw).toBe(900);
  });

  it("0x721 checksum is XOR ^ 0xFF", () => {
    const frames = seb.tick(10, [], [make0x7B9(0)], ctx());
    const f721 = frames.find(f => f.canId === "0x721");
    expect(f721).toBeDefined();
    if (f721) {
      let c = 0;
      for (let i = 0; i < 7; i++) c ^= f721.data[i];
      expect(f721.data[7]).toBe(c ^ 0xFF);
    }
  });

  it("0x721 byte 6 has security echo bits set", () => {
    const frames = seb.tick(10, [], [make0x7B9(0)], ctx());
    const f721 = frames.find(f => f.canId === "0x721");
    expect(f721).toBeDefined();
    if (f721) {
      // bit 0: RollCntEnStatus, bit 1: ChecksumEnStatus
      expect(f721.data[6] & 1).toBe(1);
      expect((f721.data[6] >> 1) & 1).toBe(1);
    }
  });
});
