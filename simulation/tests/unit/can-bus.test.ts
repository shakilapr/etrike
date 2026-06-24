import { describe, it, expect, beforeEach } from "vitest";
import { CanBusModel } from "../../src/bus/can-bus.js";
import type { SimFrame } from "../../src/core/types.js";

function makeFrame(canId: string, bus: "high" | "low", simTimeMs: number): SimFrame {
  return {
    simTimeMs,
    bus,
    canId,
    name: `TEST_${canId}`,
    dlc: 2,
    data: [0, 0],
    sender: "jetson",
  };
}

describe("CanBusModel", () => {
  let bus: CanBusModel;

  beforeEach(() => {
    bus = new CanBusModel("high");
    bus.reset(0);
  });

  it("starts empty", () => {
    expect(bus.queueLength).toBe(0);
    expect(bus.totalDelivered).toBe(0);
  });

  it("delivers frames at their scheduled time", () => {
    const f1 = makeFrame("0x300", "high", 0);
    const f2 = makeFrame("0x301", "high", 0);

    bus.schedule(f1, 1);
    bus.schedule(f2, 1);

    // Not due yet at t=0
    let delivered = bus.deliver(0);
    expect(delivered).toHaveLength(0);

    // Due at t=1
    delivered = bus.deliver(1);
    expect(delivered).toHaveLength(2);
  });

  it("respects CAN ID priority ordering (lower ID first)", () => {
    // 0x120 should be delivered before 0x300 (lower CAN ID = higher priority)
    const lowPri = makeFrame("0x300", "high", 0);
    const highPri = makeFrame("0x120", "high", 0);

    bus.schedule(lowPri, 1);
    bus.schedule(highPri, 1);

    const delivered = bus.deliver(1);
    expect(delivered).toHaveLength(2);
    expect(delivered[0].canId).toBe("0x120"); // lower ID delivered first
    expect(delivered[1].canId).toBe("0x300");
  });

  it("0x001 has higher priority than 0x7FE", () => {
    const estop = makeFrame("0x001", "high", 0);
    const heartbeat = makeFrame("0x7FE", "high", 0);

    bus.schedule(heartbeat, 1);
    bus.schedule(estop, 1);

    const delivered = bus.deliver(1);
    expect(delivered[0].canId).toBe("0x001");
    expect(delivered[1].canId).toBe("0x7FE");
  });

  it("tracks per-ID count", () => {
    bus.schedule(makeFrame("0x300", "high", 0), 1);
    bus.schedule(makeFrame("0x300", "high", 0), 1);
    bus.schedule(makeFrame("0x120", "high", 0), 1);

    bus.deliver(1);
    const stats = bus.getStats(1, 0.001);
    expect(stats.byId["0x300"]).toBe(2);
    expect(stats.byId["0x120"]).toBe(1);
  });

  it("reports active when recent frames exist", () => {
    bus.schedule(makeFrame("0x300", "high", 0), 1);
    bus.deliver(1);

    const stats = bus.getStats(1, 0.001);
    expect(stats.active).toBe(true);
  });

  it("reports inactive after 5s silence", () => {
    bus.schedule(makeFrame("0x300", "high", 0), 1);
    bus.deliver(1);

    const stats = bus.getStats(6000, 6);
    expect(stats.active).toBe(false);
  });
});
