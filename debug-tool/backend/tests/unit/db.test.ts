import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { DebugStore } from "../../src/db/queries";
import type { CanFrame } from "../../src/types/can";
import { defaultStats } from "../../src/types/can";

describe("DebugStore — frame lifecycle", () => {
  let store: DebugStore;

  beforeEach(() => {
    store = new DebugStore(":memory:", 5000);
  });

  afterEach(() => {
    store.close();
  });

  const mockFrame: CanFrame = {
    ts: 1000,
    bus: "high",
    id: "0x300",
    name: "HOST_DRIVE_CMD",
    dlc: 8,
    data: [0, 1, 2, 3, 4, 5, 6, 7],
    decoded: { speed: 100 }
  };

  it("insertFrame returns StoredCanFrame with row_id", () => {
    const result = store.insertFrame(mockFrame);
    expect(result.row_id).toBeGreaterThan(0);
    expect(result.ts_real).toBeGreaterThan(0);
    expect(result.id).toBe("0x300");
  });

  it("insertFrame stores decoded JSON correctly", () => {
    store.insertFrame(mockFrame);
    const frames = store.queryFrames();
    expect(frames.length).toBe(1);
    expect(frames[0].decoded).toEqual({ speed: 100 });
  });

  it("queryFrames returns frames ordered newest-first", () => {
    store.insertFrame({ ...mockFrame, id: "0x111" });
    store.insertFrame({ ...mockFrame, id: "0x222" });
    const frames = store.queryFrames();
    expect(frames[0].id).toBe("0x222");
    expect(frames[1].id).toBe("0x111");
  });

  it("queryFrames filters by bus", () => {
    store.insertFrame({ ...mockFrame, bus: "high", id: "0x300" });
    store.insertFrame({ ...mockFrame, bus: "low", id: "0x204" });
    const frames = store.queryFrames({ bus: "high" });
    expect(frames.length).toBe(1);
    expect(frames[0].id).toBe("0x300");
  });

  it("latestById returns exactly one entry per (bus, id) chronologically newest", () => {
    store.insertFrame({ ...mockFrame, data: [1] });
    store.insertFrame({ ...mockFrame, data: [2] });
    const latest = store.latestById();
    expect(Object.keys(latest).length).toBe(1);
    expect(latest["high:0x300"].data[0]).toBe(2);
  });
});

describe("DebugStore — pruning", () => {
  let store: DebugStore;

  beforeEach(() => {
    store = new DebugStore(":memory:", 5); // max 5 frames
  });

  afterEach(() => {
    store.close();
  });

  const mockFrame: CanFrame = {
    ts: 1000,
    bus: "high",
    id: "0x300",
    name: "HOST",
    dlc: 8,
    data: [],
    decoded: {}
  };

  it("insertFrame does NOT prune when count < maxFrames", () => {
    for (let i = 0; i < 4; i++) store.insertFrame(mockFrame);
    store.runMaintenance();
    expect(store.counts().frames).toBe(4);
  });

  it("insertFrame prunes to maxFrames when limit is exceeded", () => {
    for (let i = 0; i < 10; i++) store.insertFrame(mockFrame);
    store.runMaintenance();
    expect(store.counts().frames).toBe(5);
  });

  it("pruneFrames skips frames referenced by active recordings (BUG-20 regression)", () => {
    store.startRecording("test-rec");
    for (let i = 0; i < 10; i++) store.insertFrame(mockFrame);
    store.runMaintenance();
    // All 10 frames are kept because they are attached to an active recording
    expect(store.counts().frames).toBe(10);
  });

  it("pruneFrames deletes frames from stopped recordings (BUG-20 fix)", () => {
    const rec = store.startRecording("test-rec");
    for (let i = 0; i < 10; i++) store.insertFrame(mockFrame);
    store.stopRecording(rec.id);
    // Now they are no longer protected by an active recording, BUT they are part of a stopped recording.
    // Wait, the test says "pruneFrames deletes frames from stopped recordings". 
    // Actually the logic keeps stopped recordings up to STOPPED_RECORDING_RETENTION (10).
    // Let's force pruneStoppedRecordings to drop it by adding 10 more stopped recordings.
    for (let i = 0; i < 10; i++) {
        const r = store.startRecording(`padding-${i}`);
        store.stopRecording(r.id);
    }
    store.runMaintenance(); // This will prune old recordings, releasing the frames.
    expect(store.counts().frames).toBeLessThan(10);
  });
});

describe("DebugStore — stats staleness", () => {
  let store: DebugStore;

  beforeEach(() => {
    store = new DebugStore(":memory:");
  });

  afterEach(() => {
    store.close();
  });

  it("getStats returns defaultStats when no stats have been set", () => {
    expect(store.getStats()).toEqual(defaultStats());
  });

  it("getStats returns stored stats when fresh", () => {
    const stats = defaultStats();
    stats.fps = 100;
    store.setStats(stats);
    expect(store.getStats().fps).toBe(100);
  });

  it("getStats returns defaultStats when stored stats are >5s stale (BUG-01 regression)", async () => {
    const stats = defaultStats();
    stats.fps = 100;
    store.setStats(stats);

    // Manually backdate the stats_updated_at in the DB
    const db = (store as any).db;
    db.prepare("UPDATE runtime_state SET value = ? WHERE key = 'stats_updated_at'").run(String(Date.now() / 1000 - 10));

    expect(store.getStats().fps).toBe(0); // Should return default stats
  });
});

describe("DebugStore — recordings", () => {
  let store: DebugStore;

  beforeEach(() => {
    store = new DebugStore(":memory:");
  });

  afterEach(() => {
    store.close();
  });

  const mockFrame: CanFrame = { ts: 1000, bus: "high", id: "0x300", name: "HOST", dlc: 8, data: [], decoded: {} };

  it("startRecording returns a recording with stopped_at = null", () => {
    const rec = store.startRecording("test1");
    expect(rec.id).toBeGreaterThan(0);
    expect(rec.label).toBe("test1");
    expect(rec.stopped_at).toBeNull();
  });

  it("frames inserted while recording is active attach to it", () => {
    const rec = store.startRecording();
    store.insertFrame(mockFrame);
    store.insertFrame(mockFrame);
    store.stopRecording(rec.id);
    
    const frames = store.recordingFramesById(rec.id);
    expect(frames?.length).toBe(2);
  });

  it("clearFrames resets frame_count to 0 on all recordings (BUG-28 regression)", () => {
    const rec = store.startRecording();
    store.insertFrame(mockFrame);
    store.stopRecording(rec.id);
    
    let dbRec = store.listRecordings()[0];
    expect(dbRec.frame_count).toBe(1);

    store.clearFrames();
    
    dbRec = store.listRecordings()[0];
    expect(dbRec.frame_count).toBe(0);
  });
});

describe("DebugStore — injection tracking", () => {
  let store: DebugStore;

  beforeEach(() => {
    store = new DebugStore(":memory:");
  });

  afterEach(() => {
    store.close();
  });

  it("insertInjection stores with status 'queued'", () => {
    const inj = store.insertInjection({ bus: "high", can_id: "0x300", dlc: 8, data: [1,2,3], correlation_id: "abc" });
    expect(inj.status).toBe("queued");
  });

  it("updateInjectionByCorrelation updates the correct row", () => {
    store.insertInjection({ bus: "high", can_id: "0x111", dlc: 0, data: [], correlation_id: "c1" });
    store.insertInjection({ bus: "high", can_id: "0x222", dlc: 0, data: [], correlation_id: "c2" });
    
    const updated = store.updateInjectionByCorrelation("c1", "ok");
    expect(updated?.can_id).toBe("0x111");
    expect(updated?.status).toBe("ok");
  });
});
