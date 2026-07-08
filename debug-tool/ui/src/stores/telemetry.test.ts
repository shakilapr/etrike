import { beforeEach, describe, expect, it, vi } from "vitest";
import { get } from "svelte/store";
import { frames } from "./can";
import { ecuPresence, now as telemetryNow, telemetry } from "./telemetry";
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

function makeDecodedFrame(ts: number, bus: "high" | "low", id: string, decoded: Record<string, unknown>): CanFrame {
  return {
    ts,
    bus,
    id,
    name: id,
    dlc: 8,
    data: [0, 0, 0, 0, 0, 0, 0, 0],
    decoded
  };
}

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date("2026-07-06T00:00:00Z"));
  frames.set([]);
});

describe("ecuPresence", () => {
  it("reports recent ECU frames as present", () => {
    const now = get(telemetryNow);
    frames.set([makeFrame(now - 1, "high", "0x7FD")]);

    expect(get(ecuPresence).rt).toBe(true);
  });

  it("reports SYS frames on the high bus as present", () => {
    const now = get(telemetryNow);
    frames.set([makeFrame(now - 1, "high", "0x011")]);

    expect(get(ecuPresence).sys).toBe(true);
  });

  it("reports stale ECU frames as absent", () => {
    const now = get(telemetryNow);
    frames.set([makeFrame(now - 4, "high", "0x7FD")]);

    expect(get(ecuPresence).rt).toBe(false);
  });

  it("normalizes millisecond WebSocket timestamps for staleness", () => {
    const now = get(telemetryNow) * 1000;
    frames.set([makeFrame(now - 4_000, "high", "0x7FD")]);

    expect(get(ecuPresence).rt).toBe(false);
  });
});

describe("telemetry staleness", () => {
  it("reports recent actual values", () => {
    const now = get(telemetryNow);
    frames.set([
      makeDecodedFrame(now - 1, "high", "0x120", { speed_mmps: 2000 }),
      makeDecodedFrame(now - 1, "low", "0x201", { str_angle: 125 }),
      makeDecodedFrame(now - 1, "high", "0x301", { brake_pressure_kpa: 5000 }),
      makeDecodedFrame(now - 1, "high", "0x206", { gear_state: 1 }),
      makeDecodedFrame(now - 1, "high", "0x210", { mode: 1, safety_state: 0 }),
    ]);

    expect(get(telemetry)).toMatchObject({
      motorSpeedKmh: 7.2,
      steerAngleDeg: 12.5,
      brakePressureMpa: 5,
      gear: "D",
      mode: "AUTO",
      safetyState: "Normal",
    });
  });

  it("hides stale actual values instead of showing old data", () => {
    const now = get(telemetryNow);
    frames.set([
      makeDecodedFrame(now - 4, "high", "0x120", { speed_mmps: 2000 }),
      makeDecodedFrame(now - 4, "low", "0x201", { str_angle: 125 }),
      makeDecodedFrame(now - 4, "high", "0x301", { brake_pressure_kpa: 5000 }),
      makeDecodedFrame(now - 4, "high", "0x206", { gear_state: 1 }),
      makeDecodedFrame(now - 4, "high", "0x210", { mode: 1, safety_state: 0 }),
    ]);

    expect(get(telemetry)).toMatchObject({
      motorSpeedKmh: null,
      steerAngleDeg: null,
      brakePressureMpa: null,
      gear: null,
      mode: null,
      safetyState: null,
    });
  });
});
