import { describe, it, expect, vi, beforeEach } from "vitest";
import { ActiveTransportManager } from "../../src/bridge/manager";
import type { AppConfig } from "../../src/config";
import type { DebugStore } from "../../src/db/queries";
import type { StreamHub } from "../../src/ws/stream";
import type { WriteQueue } from "../../src/db/write-queue";

describe("ActiveTransportManager", () => {
  let manager: ActiveTransportManager;
  let mockStore: DebugStore;
  let mockHub: StreamHub;
  let mockWriteQueue: WriteQueue;
  let mockConfig: AppConfig;

  beforeEach(() => {
    mockConfig = {
      port: 8080,
      host: "localhost",
      canTransport: "serial",
      canTransportPort: "/dev/ttyUSB0",
      canTransportBaud: 115200,
      databasePath: ":memory:"
    };
    mockStore = {} as DebugStore;
    mockHub = {} as StreamHub;
    mockWriteQueue = {} as WriteQueue;

    manager = new ActiveTransportManager(mockConfig, mockStore, mockHub, mockWriteQueue);
  });

  it("initializes with disabled state", () => {
    expect(manager.state.transport).toBe("disabled");
    expect(manager.state.connected).toBe(false);
  });

  // Just a basic structural test for now, as testing full serial/canalyst
  // interactions requires mocking their underlying hardware dependencies
  // which is already covered in their respective bridge unit tests.
  it("registers onFrame callback", () => {
    const cb = vi.fn();
    manager.onFrame(cb);
    // Should not throw
  });
});
