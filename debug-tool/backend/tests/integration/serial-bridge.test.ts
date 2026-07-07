import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { MockParser, MockSerialPort } from "../helpers/fake-serial";

// Mock serialport BEFORE importing SerialBridge
vi.mock("serialport", () => {
  return {
    SerialPort: MockSerialPort,
    ReadlineParser: MockParser
  };
});

import { SerialBridge } from "../../src/serial/reader";
import { DebugStore } from "../../src/db/queries";
import { StreamHub } from "../../src/ws/stream";

describe("SerialBridge", () => {
  let store: DebugStore;
  let hub: StreamHub;
  let bridge: SerialBridge;
  
  beforeEach(() => {
    vi.useFakeTimers();
    store = new DebugStore(":memory:", 5000);
    hub = new StreamHub();
    bridge = new SerialBridge(
      { serialPath: "/dev/ttyUSB0", serialBaudRate: 115200 } as any,
      store,
      hub
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
    
    // Fast-forward to port opening
    await vi.runAllTimersAsync();
    expect(bridge.state.port_open).toBe(true);

    // Get internal port instance
    const port = (bridge as any).port as MockSerialPort;
    
    // Simulate close
    port.close();
    await vi.runAllTimersAsync();
    expect(bridge.state.port_open).toBe(true); // Should have re-opened!
    
    // The delay should have been 1000ms for first attempt
    expect(broadcastSpy).toHaveBeenCalled();
  });

  it("caps backoff and switches to 30s polling when attempts exhausted", async () => {
    bridge.start();
    await vi.runAllTimersAsync();
    
    const port = (bridge as any).port as MockSerialPort;
    
    // Simulate failing opens to exhaust attempts (MAX_RECONNECT_ATTEMPTS = 10)
    for (let i = 0; i < 11; i++) {
        port.emit("error", new Error("Simulated failure"));
        // Move timer forward just enough for the next backoff
        await vi.runOnlyPendingTimersAsync(); 
    }
    
    expect(bridge.state.last_error).toContain("exhausted");
  });

  it("processes stats frames and broadcasts via StreamHub", async () => {
    bridge.start();
    await vi.runAllTimersAsync();
    
    const port = (bridge as any).port as MockSerialPort;
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    
    // Pipe the parser so we can emit data
    const parser = new MockParser();
    port.pipe(parser);
    
    parser.emit("data", JSON.stringify({
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
    await vi.runAllTimersAsync();
    
    const port = (bridge as any).port as MockSerialPort;
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    
    const parser = new MockParser();
    port.pipe(parser);
    
    parser.emit("data", JSON.stringify({
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
    await vi.runAllTimersAsync();
    
    const port = (bridge as any).port as MockSerialPort;
    const broadcastSpy = vi.spyOn(hub, "broadcast");
    broadcastSpy.mockClear();
    
    const parser = new MockParser();
    port.pipe(parser);
    
    // High unique ID
    parser.emit("data", JSON.stringify({ id: "0x300", data: [0,0,0,0,0,0,0,0] }));
    parser.emit("data", JSON.stringify({ id: "0x300", data: [0,0,0,0,0,0,0,0] }));
    parser.emit("data", JSON.stringify({ id: "0x300", data: [0,0,0,0,0,0,0,0] })); // Locks to high, should broadcast

    // Low unique ID
    parser.emit("data", JSON.stringify({ id: "0x201", data: [0,0,0,0,0,0,0,0] })); // Still locked to high, confidence 'none'

    // The status should only be broadcast once when it transitions to 'high' confidence
    const statusBroadcasts = broadcastSpy.mock.calls.filter(args => args[0].type === "status");
    expect(statusBroadcasts.length).toBe(1);
  });
});
