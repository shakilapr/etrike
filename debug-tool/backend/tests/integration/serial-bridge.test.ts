import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { MockParser, MockSerialPort } from "../helpers/fake-serial";

vi.mock("serialport", () => {
  return {
    SerialPort: MockSerialPort,
    ReadlineParser: MockParser
  };
});

import { SerialBridge } from "../../src/serial/reader";
import { type DebugStore, DebugStoreImpl } from "../../src/db/queries";
import { StreamHub } from "../../src/ws/stream";
import type { WriteQueue } from "../../src/db/write-queue";

describe("SerialBridge", () => {
  let store: DebugStore;
  let hub: StreamHub;
  let bridge: SerialBridge;
  
  beforeEach(() => {
    vi.useFakeTimers();
    store = new DebugStoreImpl(":memory:", 5000);
    hub = new StreamHub();
    const writeQueue = { enqueue: vi.fn((f) => store.insertFrame(f)), flush: vi.fn(), drain: vi.fn() } as unknown as WriteQueue;
    bridge = new SerialBridge(
      { serialPath: "/dev/ttyUSB0", serialBaudRate: 115200 } as any,
      store,
      hub,
      writeQueue
    );
  });

  afterEach(async () => {
    await bridge.close();
    store.close();
    hub.close();
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  it("reconnects with exponential backoff on port close (BUG-26 double open check)", async () => {
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    bridge.start();
    
    // Fast-forward to port opening (our mock is sync, but we'll advance anyway)
    await vi.advanceTimersByTimeAsync(10);
    expect(bridge.state.port_open).toBe(true);

    const port = (bridge as any).port as MockSerialPort;
    
    // Simulate close
    port.close();
    
    // Backoff is 1000ms for first attempt
    await vi.advanceTimersByTimeAsync(1100);
    expect(bridge.state.port_open).toBe(true); // Should have re-opened!
    
    expect(broadcastSpy).toHaveBeenCalled();
  });

  it("ignores duplicate start calls while the port is open", async () => {
    const openSpy = vi.spyOn(MockSerialPort.prototype, "open");

    bridge.start();
    bridge.start();
    await vi.advanceTimersByTimeAsync(10);

    expect(openSpy).toHaveBeenCalledTimes(1);
  });

  it("logs startup failures to the backend console", async () => {
    const warnSpy = vi.spyOn(console, "warn").mockImplementation(() => {});
    const writeQueue = { enqueue: vi.fn(), flush: vi.fn(), drain: vi.fn() } as unknown as WriteQueue;
    const disabled = new SerialBridge(
      { serialPath: "", serialBaudRate: 115200 } as any,
      store,
      hub,
      writeQueue
    );

    disabled.start();

    expect(warnSpy).toHaveBeenCalledWith("[serial] serial disabled: no serial path configured");
    warnSpy.mockRestore();
  });

  it("caps backoff and switches to 30s polling when attempts exhausted", async () => {
    // Initial start
    bridge.start();
    await vi.advanceTimersByTimeAsync(10);
    
    const originalOpen = MockSerialPort.prototype.open;
    MockSerialPort.prototype.open = function(cb?: (err?: Error) => void) {
        this.isOpen = false;
        if (cb) cb(new Error("Simulated failure"));
    };
    
    try {
        const currentPort = (bridge as any).port as MockSerialPort;
        currentPort.emit("error", new Error("Initial failure"));
        
        // Just advance time continuously in 1s increments until it hits the exhausted state
        // It takes at most 10 attempts, with max delay 30s, so 300s is way more than enough.
        for (let i = 0; i < 300; i++) {
            await vi.advanceTimersByTimeAsync(1000);
            if (bridge.state.last_error?.includes("exhausted")) {
                break;
            }
        }
        
        expect(bridge.state.last_error).toContain("exhausted");
    } finally {
        MockSerialPort.prototype.open = originalOpen;
    }
  });

  it("processes stats frames and broadcasts via StreamHub", async () => {
    bridge.start();
    await vi.advanceTimersByTimeAsync(10);
    
    const port = (bridge as any).port as MockSerialPort;
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    
    // Emit directly on port so pipe forwards it to parser
    port.emit("data", JSON.stringify({
      type: "stats",
      ts: 1000,
      uptime_s: 10,
      buses: { high: { active: true, total: 100, fps: 10, load_pct: 1, tec: 0, rec: 0, by_id: {} } }
    }));
    
    expect(broadcastSpy).toHaveBeenCalledWith(expect.objectContaining({
      type: "stats"
    }));
    expect(store.getStats().uptime_s).toBe(10);
  });

  it("processes CAN frames, inserts to DB, broadcasts via StreamHub", async () => {
    bridge.start();
    await vi.advanceTimersByTimeAsync(10);
    
    const port = (bridge as any).port as MockSerialPort;
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    
    port.emit("data", JSON.stringify({
      id: "0x300",
      bus: "high",
      dlc: 8,
      data: [0,0,0,0,0,0,0,0]
    }));
    
    const frames = store.queryFrames();
    expect(frames.length).toBe(1);
    expect(frames[0].id).toBe("0x300");
    
    expect(broadcastSpy).toHaveBeenCalledWith(expect.objectContaining({
      type: "can_frame"
    }));
  });

  it("debounces WS status floods on bus detection flip-flop (BUG-23 regression)", async () => {
    bridge.start();
    await vi.advanceTimersByTimeAsync(10);
    
    const port = (bridge as any).port as MockSerialPort;
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    broadcastSpy.mockClear();
    
    // High unique ID
    port.emit("data", JSON.stringify({ id: "0x300", data: [0,0,0,0,0,0,0,0] }));
    port.emit("data", JSON.stringify({ id: "0x300", data: [0,0,0,0,0,0,0,0] }));
    port.emit("data", JSON.stringify({ id: "0x300", data: [0,0,0,0,0,0,0,0] })); // Locks to high, should broadcast

    // Low unique ID
    port.emit("data", JSON.stringify({ id: "0x201", data: [0,0,0,0,0,0,0,0] })); // Still locked to high, confidence 'none'

    // The status should only be broadcast once when it transitions to 'high' confidence
    const statusBroadcasts = broadcastSpy.mock.calls.filter(args => args[0].type === "status");
    expect(statusBroadcasts.length).toBe(1);
  });
});
