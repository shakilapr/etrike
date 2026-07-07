import { describe, expect, it, vi } from "vitest";
import { StreamHub } from "./stream";
import type { CanFrame } from "../types/can";

const makeFrame = (bus: "high" | "low", id: string): CanFrame => ({
  ts: 1,
  bus,
  id,
  name: "TEST",
  dlc: 1,
  data: [0],
  decoded: {}
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
      .handleClientMessage(client, JSON.stringify({ type: "filter", keys: ["high:0x300"] }));
    hub.broadcast({ type: "can_frame", payload: makeFrame("high", "0x300") });
    hub.broadcast({ type: "can_frame", payload: makeFrame("low", "0x300") });
    (hub as unknown as { flushFrames(): void }).flushFrames();

    expect(socket.send).toHaveBeenCalledTimes(1);
    const message = JSON.parse(socket.send.mock.calls[0][0]);
    expect(message.payload).toEqual([expect.objectContaining({ bus: "high", id: "0x300" })]);
  });
});
