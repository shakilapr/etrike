import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { ReplayEngine } from "../../src/sim/replay";
import type { DebugStore } from "../../src/db/queries";
import type { CanFrame } from "../../src/types/can";

describe("ReplayEngine", () => {
  let engine: ReplayEngine;
  let mockStore: DebugStore;
  let onFrame: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    vi.useFakeTimers();
    mockStore = {
      recentFramesIterator: vi.fn().mockImplementation(async function* () {
        yield { bus: "high", id: "0x123", dlc: 8, data: [1,2,3,4,5,6,7,8], ts_us: "100" } as CanFrame;
        yield { bus: "high", id: "0x456", dlc: 8, data: [8,7,6,5,4,3,2,1], ts_us: "200" } as CanFrame;
      })
    } as unknown as DebugStore;
    onFrame = vi.fn();
    engine = new ReplayEngine(mockStore, onFrame);
  });

  afterEach(() => {
    engine.stop();
    vi.useRealTimers();
  });

  it("loads and plays a recording", async () => {
    await engine.load("rec1");
    expect(engine.getState().recordingId).toBe("rec1");
    
    engine.play(1.0);
    expect(engine.getState().playing).toBe(true);

    await vi.advanceTimersByTimeAsync(2000);
    expect(onFrame).toHaveBeenCalledTimes(2);
    expect(engine.getState().currentTimeUs).toBe("200");
  });

  it("pauses and stops", async () => {
    await engine.load("rec1");
    engine.play();
    engine.pause();
    expect(engine.getState().playing).toBe(false);

    engine.stop();
    expect(engine.getState().recordingId).toBeNull();
    expect(engine.getState().currentTimeUs).toBeNull();
  });
});
