import { describe, expect, it, vi, beforeEach, afterEach } from "vitest";
import { DebugStore } from "../../src/db/queries";
import { StreamHub } from "../../src/ws/stream";

const childProcess = vi.hoisted(() => {
  const { EventEmitter } = require("node:events");
  const { PassThrough } = require("node:stream");
  const children: any[] = [];
  const spawn = vi.fn(() => {
    const child = new EventEmitter() as any;
    child.stdout = new PassThrough();
    child.stderr = new PassThrough();
    child.stdin = { writable: true, write: vi.fn() };
    child.kill = vi.fn(() => child.emit("exit", null, "SIGTERM"));
    children.push(child);
    return child;
  });
  return { children, spawn };
});

vi.mock("node:child_process", () => ({ spawn: childProcess.spawn }));
vi.mock("node:fs", () => ({ existsSync: vi.fn(() => true) }));

import { CanalystBridge } from "../../src/canalyst/bridge";

describe("CanalystBridge", () => {
  let store: DebugStore;
  let hub: StreamHub;

  beforeEach(() => {
    childProcess.children.length = 0;
    childProcess.spawn.mockClear();
    store = new DebugStore(":memory:", 5000);
    hub = new StreamHub();
  });

  afterEach(() => {
    store.close();
    hub.close();
  });

  it("can be started again after an abandoned detection attempt", async () => {
    const bridge = new CanalystBridge(
      {
        canalystPython: "python",
        canalystBitrate: 500000,
        canalystPollMs: 10,
        canalystDeviceIndex: 0,
        canalystChannel0Bus: "high",
        canalystChannel1Bus: "low"
      } as any,
      store,
      hub
    );

    bridge.start();
    await bridge.abandon();
    bridge.start();

    expect(childProcess.spawn).toHaveBeenCalledTimes(2);
    expect(bridge.state.link_open).toBe(true);
  });
});
