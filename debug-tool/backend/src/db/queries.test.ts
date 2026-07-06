import { afterEach, describe, expect, it } from "vitest";
import { DebugStore } from "./queries";
import type { CanFrame } from "../types/can";

let stores: DebugStore[] = [];

function makeFrame(ts: number, id = "0x300"): CanFrame {
  return {
    ts,
    bus: "high",
    id,
    name: "HOST_DRIVE_CMD",
    dlc: 8,
    data: [0, 0, 0, 0, 0, 0, 0, 1],
    decoded: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 }
  };
}

function makeStore(maxFrames: number): DebugStore {
  const store = new DebugStore(":memory:", maxFrames);
  stores.push(store);
  return store;
}

afterEach(() => {
  for (const store of stores) store.close();
  stores = [];
});

describe("DebugStore maintenance", () => {
  it("does not prune synchronously during insertFrame", () => {
    const store = makeStore(2);
    store.insertFrame(makeFrame(1));
    store.insertFrame(makeFrame(2));
    store.insertFrame(makeFrame(3));

    expect(store.counts().frames).toBe(3);
  });

  it("prunes unrecorded frames during maintenance", () => {
    const store = makeStore(2);
    store.insertFrame(makeFrame(1));
    store.insertFrame(makeFrame(2));
    store.insertFrame(makeFrame(3));

    store.runMaintenance();

    const frames = store.queryFrames({ limit: 10 });
    expect(frames).toHaveLength(2);
    expect(frames.map((frame) => frame.ts).sort()).toEqual([2, 3]);
  });

  it("keeps frames referenced by retained recordings", () => {
    const store = makeStore(1);
    const recording = store.startRecording("active");
    store.insertFrame(makeFrame(1));
    store.stopRecording(recording.id);
    store.insertFrame(makeFrame(2));

    store.runMaintenance();

    expect(store.recordingFramesById(recording.id, 10)).toHaveLength(1);
    expect(store.queryFrames({ limit: 10 }).map((frame) => frame.ts).sort()).toEqual([1]);
  });

  it("retains only the latest 10 stopped recordings", () => {
    const store = makeStore(100);
    for (let i = 0; i < 12; i++) {
      const recording = store.startRecording(`rec-${i}`);
      store.stopRecording(recording.id);
    }

    store.runMaintenance();

    const recordings = store.listRecordings();
    expect(recordings).toHaveLength(10);
    expect(recordings.map((recording) => recording.label)).not.toContain("rec-0");
    expect(recordings.map((recording) => recording.label)).not.toContain("rec-1");
  });

  it("creates a reverse index for recording frame lookups", () => {
    const store = makeStore(10);
    const db = (store as unknown as { db: { prepare(sql: string): { all(): Array<{ name: string }> } } }).db;
    const indexes = db
      .prepare("PRAGMA index_list('recording_frames')")
      .all();

    expect(indexes.some((index) => index.name === "idx_recording_frames_frame")).toBe(true);
  });
});
