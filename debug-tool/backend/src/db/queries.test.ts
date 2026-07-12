import { ID_HOST_DRIVE_CMD } from "@etrike/debug-shared";
import { afterEach, describe, expect, it } from "vitest";
import { DebugStoreImpl } from "./queries";
import type { CanFrame } from "../types/can";

let stores: DebugStoreImpl[] = [];

function makeFrame(ts: number, id = ID_HOST_DRIVE_CMD): CanFrame {
  return {
    ts,
    ts_us: "1000",
    seq: 0,
    bus: "high",
    frame: {
      id,
      dlc: 8,
      data: [0, 0, 0, 0, 0, 0, 0, 1],
      ext: false,
      rtr: false
    },
    decoded: {
      name: "HOST_DRIVE_CMD",
      signals: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 }
    }
  };
}

function makeStore(maxFrames: number): DebugStoreImpl {
  const store = new DebugStoreImpl(":memory:", maxFrames);
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

  it("does not stop an already stopped recording again", () => {
    const store = makeStore(100);
    const recording = store.startRecording("one-shot");

    const stopped = store.stopRecording(recording.id);
    const secondStop = store.stopRecording(recording.id);

    expect(stopped?.stopped_at).toEqual(expect.any(Number));
    expect(secondStop).toBeNull();
    expect(store.getRecording(recording.id)?.stopped_at).toBe(stopped?.stopped_at);
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
