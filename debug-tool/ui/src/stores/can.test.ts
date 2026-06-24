import { beforeEach, describe, expect, it } from "vitest";
import { get } from "svelte/store";
import {
  frames,
  stats,
  status,
  wsConnected,
  commandAcks,
  latestById,
  recentFrameRate,
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

const defaultStats = (): CanStats => ({
  ts: Date.now() / 1000,
  uptime_s: 0,
  buses: {
    high: { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} },
    low: { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} }
  }
});

beforeEach(() => {
  frames.set([]);
  stats.set(defaultStats());
  status.set({ backend_online: false, adapter_connected: false, esp32_connected: false });
  wsConnected.set(false);
  commandAcks.set([]);
});

describe("frames store", () => {
  it("starts empty", () => {
    expect(get(frames)).toHaveLength(0);
  });
});

describe("wsConnected store", () => {
  it("starts false", () => {
    expect(get(wsConnected)).toBe(false);
  });

  it("can be set to true", () => {
    wsConnected.set(true);
    expect(get(wsConnected)).toBe(true);
  });

  it("can be set back to false", () => {
    wsConnected.set(true);
    wsConnected.set(false);
    expect(get(wsConnected)).toBe(false);
  });
});

describe("ingestInitialFrames", () => {
  it("loads initial frames", () => {
    const f1 = makeFrame({ id: "0x300" });
    const f2 = makeFrame({ id: "0x301", bus: "low" });
    ingestInitialFrames([f1, f2]);
    expect(get(frames)).toHaveLength(2);
  });

  it("caps at 800 frames", () => {
    const many = Array.from({ length: 1000 }, (_, i) => makeFrame({ id: `0x${i.toString(16)}`, ts: i }));
    ingestInitialFrames(many);
    const kept = get(frames);
    expect(kept.length).toBe(800);
    // Should keep the LAST 800 (frames 200-999), discarding oldest
    expect(kept[0].ts).toBe(200);
    expect(kept[799].ts).toBe(999);
  });
});

describe("ingestMessage", () => {
  it("appends can_frame messages", () => {
    const frame = makeFrame();
    ingestMessage({ type: "can_frame", payload: frame });
    expect(get(frames)).toHaveLength(1);
    expect(get(frames)[0].id).toBe("0x300");
  });

  it("caps can_frame at 1000", () => {
    const many = Array.from({ length: 1100 }, (_, i) => makeFrame({ id: `0x${i.toString(16)}`, ts: i }));
    for (const f of many) {
      ingestMessage({ type: "can_frame", payload: f });
    }
    const kept = get(frames);
    expect(kept.length).toBe(1000);
    expect(kept[0].ts).toBe(100); // frames 0-99 dropped
    expect(kept[999].ts).toBe(1099);
  });

  it("does not crash on missing payload", () => {
    // Current code pushes undefined into frames array — verify no throw
    expect(() => ingestMessage({ type: "can_frame", payload: undefined as any })).not.toThrow();
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

  it("updates status with partial fields (merge)", () => {
    ingestMessage({ type: "status", payload: { backend_online: true } });
    expect(get(status).backend_online).toBe(true);
    // Second status update merges
    ingestMessage({ type: "status", payload: { esp32_connected: true } });
    expect(get(status).backend_online).toBe(true);
    expect(get(status).esp32_connected).toBe(true);
  });

  it("prepends cmd_ack messages", () => {
    ingestMessage({ type: "cmd_ack", payload: { request_id: "abc", status: "ok" } });
    expect(get(commandAcks)).toHaveLength(1);
    expect(get(commandAcks)[0].request_id).toBe("abc");
  });

  it("caps cmd_ack at 30 messages", () => {
    for (let i = 0; i < 35; i++) {
      ingestMessage({ type: "cmd_ack", payload: { request_id: String(i), status: "ok" } });
    }
    const acks = get(commandAcks);
    expect(acks).toHaveLength(30);
    // Newest first: item 34 should be at index 0
    expect(acks[0].request_id).toBe("34");
    expect(acks[29].request_id).toBe("5"); // item 5 is the oldest retained
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
  });

  it("same bus:id uses last (latest) frame", () => {
    const f1 = makeFrame({ id: "0x300", bus: "high", ts: 1000 });
    const f2 = makeFrame({ id: "0x300", bus: "high", ts: 2000 });
    frames.set([f1, f2]);
    const latest = get(latestById);
    expect(latest["high:0x300"].ts).toBe(2000);
  });

  it("returns empty after reset", () => {
    frames.set([makeFrame()]);
    frames.set([]);
    expect(Object.keys(get(latestById))).toHaveLength(0);
  });
});

describe("recentFrameRate", () => {
  it("returns 0 when empty", () => {
    frames.set([]);
    expect(get(recentFrameRate)).toBe(0);
  });

  it("returns 0 with only 1 frame", () => {
    frames.set([makeFrame({ ts: 1000 })]);
    expect(get(recentFrameRate)).toBe(0);
  });

  it("computes frames per second over 5-second window", () => {
    const now = Date.now() / 1000;
    const frameList = Array.from({ length: 50 }, (_, i) =>
      makeFrame({ ts: now - 4 + i * 0.1 }) // 50 frames over ~5s
    );
    frames.set(frameList);
    expect(get(recentFrameRate)).toBe(10); // 50 / 5
  });

  it("excludes frames older than 5 seconds", () => {
    const now = Date.now() / 1000;
    const frameList = [
      makeFrame({ ts: now - 10 }),  // too old
      makeFrame({ ts: now - 1 }),
      makeFrame({ ts: now - 2 }),
      makeFrame({ ts: now - 3 }),
    ];
    frames.set(frameList);
    expect(get(recentFrameRate)).toBe(0.6); // 3 / 5
  });
});
