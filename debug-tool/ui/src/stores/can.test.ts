import { describe, expect, it } from "vitest";
import { get } from "svelte/store";
import {
  frames,
  stats,
  status,
  wsConnected,
  commandAcks,
  latestById,
  ingestInitialFrames,
  ingestMessage
} from "./can";
import type { CanFrame, CanStats } from "../lib/can-decoder";

const makeFrame = (overrides: Partial<CanFrame> = {}): CanFrame => ({
  ts: 1000,
  bus: "high",
  id: "0x300",
  name: "HOST_DRIVE_CMD",
  dlc: 8,
  data: [0, 0, 0, 0, 0, 0, 0, 1],
  decoded: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 },
  ...overrides
});

describe("frames store", () => {
  it("starts empty", () => {
    expect(get(frames)).toHaveLength(0);
  });
});

describe("ingestInitialFrames", () => {
  it("loads initial frames", () => {
    const f1 = makeFrame({ id: "0x300" });
    const f2 = makeFrame({ id: "0x301", bus: "low" });
    ingestInitialFrames([f1, f2]);
    expect(get(frames)).toHaveLength(2);
    // Reset
    frames.set([]);
  });

  it("caps at 800 frames", () => {
    const many = Array.from({ length: 1000 }, (_, i) => makeFrame({ id: `0x${i.toString(16)}` }));
    ingestInitialFrames(many);
    expect(get(frames).length).toBeLessThanOrEqual(800);
    frames.set([]);
  });
});

describe("ingestMessage", () => {
  it("appends can_frame messages", () => {
    const frame = makeFrame();
    ingestMessage({ type: "can_frame", payload: frame });
    expect(get(frames)).toHaveLength(1);
    expect(get(frames)[0].id).toBe("0x300");
    frames.set([]);
  });

  it("updates stats on stats message", () => {
    const statsPayload: CanStats = {
      ts: 2000,
      uptime_s: 3600,
      buses: {
        high: { active: true, total: 100, fps: 50, load_pct: 10, tec: 0, rec: 0, by_id: { "0x300": 50 } },
        low: { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} }
      }
    };
    ingestMessage({ type: "stats", payload: statsPayload });
    expect(get(stats).buses.high.fps).toBe(50);
  });

  it("updates status on status message", () => {
    ingestMessage({ type: "status", payload: { backend_online: true } });
    expect(get(status).backend_online).toBe(true);
  });

  it("prepends cmd_ack messages", () => {
    ingestMessage({ type: "cmd_ack", payload: { request_id: "abc", status: "ok" } });
    expect(get(commandAcks)).toHaveLength(1);
  });
});

describe("latestById", () => {
  it("keys latest frame by bus:id", () => {
    const f1 = makeFrame({ id: "0x300", bus: "high" });
    const f2 = makeFrame({ id: "0x300", bus: "low" });
    frames.set([f1, f2]);
    const latest = get(latestById);
    expect(latest["high:0x300"]).toBeDefined();
    expect(latest["low:0x300"]).toBeDefined();
    frames.set([]);
  });
});
