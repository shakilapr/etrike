import { describe, it, expect, beforeEach } from "vitest";
import { RtEcu } from "../../src/ecus/rt.js";
import { MtrEcu } from "../../src/ecus/mtr.js";
import { SyntreeEpsc } from "../../src/ecus/epsc.js";
import { SyntreeSeb } from "../../src/ecus/seb.js";
import type { SimulationContext } from "../../src/ecus/base.js";
import type { SimFrame } from "../../src/core/types.js";

function makeFrame(canId: string, bus: "high" | "low", data: number[]): SimFrame {
  return { simTimeMs: 0, bus, canId, name: canId, dlc: data.length, data, sender: "jetson" };
}

function autoCtx(nowMs: number): SimulationContext {
  return { nowMs, ticks: nowMs, mode: "auto", estopActive: false, brakeLeverPressed: false };
}

describe("RT to low-bus pipeline", () => {
  let rt: RtEcu;
  let mtr: MtrEcu;
  let epsc: SyntreeEpsc;
  let seb: SyntreeSeb;

  beforeEach(() => {
    rt = new RtEcu();
    rt.init();
    mtr = new MtrEcu();
    mtr.init();
    epsc = new SyntreeEpsc();
    epsc.init();
    seb = new SyntreeSeb();
    seb.init();
  });

  it("RT produces 0x204 when Jetson sends 0x300", () => {
    // Boot RT steering to ACTIVE
    for (let i = 0; i < 30; i++) {
      rt.tick(i * 20, [], [], autoCtx(i * 20));
    }
    // Inject EPS-C data to sync
    rt.tick(600, [], [
      { simTimeMs: 600, bus: "low", canId: "0x201", name: "SES_STATUS",
        dlc: 8, data: [1, 0, 0, 0, 0, 0, 0, 0], sender: "epsc" },
    ], autoCtx(600));

    // Now send Jetson drive command
    const driveCmd = makeFrame("0x300", "high", [0, 0, 0x07, 0xD0, 0, 0, 0, 1]); // 2000mm/s, D
    const frames = rt.tick(610, [driveCmd], [], autoCtx(610));

    const f204 = frames.find(f => f.canId === "0x204");
    expect(f204).toBeDefined();
    expect(f204!.bus).toBe("low");
    // Speed should be 2000 = 0x000007D0
    const speed = (f204!.data[0] << 24 | f204!.data[1] << 16 | f204!.data[2] << 8 | f204!.data[3]) >> 0;
    expect(speed).toBe(2000);
    expect(f204!.data[4]).toBe(1); // gear=D
  });

  it("RT forwards 0x001 ESTOP bidirectionally", () => {
    const estopFrame = makeFrame("0x001", "high", []);
    const frames = rt.tick(1, [estopFrame], [], autoCtx(1));

    const forwarded = frames.find(f => f.canId === "0x001" && f.bus === "low");
    expect(forwarded).toBeDefined();
  });

  it("RT forwards low-bus category 1 messages to high", () => {
    // 0x011 SYS_SAFETY_STS should be forwarded low→high
    const safetyFrame = makeFrame("0x011", "low", [0, 1]);
    const frames = rt.tick(1, [], [safetyFrame], autoCtx(1));

    const forwarded = frames.find(f => f.canId === "0x011" && f.bus === "high");
    expect(forwarded).toBeDefined();
  });

  it("EPS-C produces 0x201 after receiving 0x169", () => {
    const cmd169 = {
      simTimeMs: 0, bus: "low" as const, canId: "0x169", name: "VCU_SES_REQ",
      dlc: 8, data: [3, 0, 0x88, 0x13, 0, 0, 0, 0], sender: "rt" as const,
    };
    const frames = epsc.tick(10, [], [cmd169], autoCtx(10));
    expect(frames.some(f => f.canId === "0x201")).toBe(true);
  });
});
