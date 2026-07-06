import { beforeEach, describe, expect, it, vi } from "vitest";
import { get } from "svelte/store";
import { frames } from "./can";
import { ecuPresence } from "./telemetry";
import type { CanFrame } from "../lib/can-decoder";

function makeFrame(ts: number, bus: "high" | "low", id: string): CanFrame {
  return {
    ts,
    bus,
    id,
    name: id,
    dlc: 2,
    data: [1, 0],
    decoded: { alive_ctr: 1, health_flags: 0 }
  };
}

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date("2026-07-06T00:00:00Z"));
  frames.set([]);
});

describe("ecuPresence", () => {
  it("reports recent ECU frames as present", () => {
    const now = Date.now() / 1000;
    frames.set([makeFrame(now - 1, "high", "0x7FD")]);

    expect(get(ecuPresence).rt).toBe(true);
  });

  it("reports stale ECU frames as absent", () => {
    const now = Date.now() / 1000;
    frames.set([makeFrame(now - 4, "high", "0x7FD")]);

    expect(get(ecuPresence).rt).toBe(false);
  });

  it("normalizes millisecond WebSocket timestamps for staleness", () => {
    const now = Date.now();
    frames.set([makeFrame(now - 4_000, "high", "0x7FD")]);

    expect(get(ecuPresence).rt).toBe(false);
  });
});
