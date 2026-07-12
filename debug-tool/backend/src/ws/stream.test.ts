import { ID_HOST_DRIVE_CMD } from "@etrike/debug-shared";
import { describe, expect, it, vi } from "vitest";
import { StreamHub } from "./stream";
import type { CanFrame } from "../types/can";

const makeFrame = (bus: "high" | "low", id: string): CanFrame => ({
  ts: 1,
  ts_us: "1000000",
  seq: 1,
  bus,
  frame: {
    id,
    dlc: 1,
    data: [0],
    ext: false,
    rtr: false
  },
  decoded: {
    name: "TEST",
    signals: {}
  }
});

function makeSocket() {
  return {
    readyState: 1,
    send: vi.fn(),
    ping: vi.fn(),
    on: vi.fn(),
    close: vi.fn()
  };
}

type TestClient = {
  socket: ReturnType<typeof makeSocket>;
  buses: null;
  ids: null;
  keys: null;
  lastPong: number;
};

describe("StreamHub filters", () => {
  it("matches bus-scoped keys without leaking same ID from another bus", () => {
    const hub = new StreamHub();
    const socket = makeSocket();
    const client: TestClient = { socket, buses: null, ids: null, keys: null, lastPong: Date.now() };

    (hub as unknown as { clients: Set<TestClient> }).clients.add(client);
    (hub as unknown as { handleClientMessage(client: TestClient, payload: string): void })
      .handleClientMessage(client, JSON.stringify({ type: "filter", keys: [`high:${ID_HOST_DRIVE_CMD}`] }));
    hub.broadcast({ type: "can_frame", payload: makeFrame("high", ID_HOST_DRIVE_CMD) });
    hub.broadcast({ type: "can_frame", payload: makeFrame("low", ID_HOST_DRIVE_CMD) });
    (hub as unknown as { flushFrames(): void }).flushFrames();

    expect(socket.send).toHaveBeenCalledTimes(1);
    const message = JSON.parse(socket.send.mock.calls[0][0]);
    expect(message.payload).toEqual([expect.objectContaining({ bus: "high", frame: expect.objectContaining({ id: ID_HOST_DRIVE_CMD }) })]);
  });
});
