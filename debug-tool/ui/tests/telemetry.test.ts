import { describe, it, expect, vi } from "vitest";
import { get } from "svelte/store";
import { ecuPresence, telemetry } from "../../src/stores/telemetry";
import { latestById } from "../../src/stores/can";
import type { CanFrame } from "../../src/types/can";

// Mock out latestById so we can set it
vi.mock("../../src/stores/can", () => {
  const { writable } = require("svelte/store");
  return {
    latestById: writable<Record<string, CanFrame>>({})
  };
});

describe("telemetry store", () => {
  it("computes ECU presence based on recent heartbeats (BUG-02 regression)", () => {
    const now = Date.now();
    const frame: CanFrame = { ts: now, bus: "high", id: "0x7FD", name: "RT_HEARTBEAT", dlc: 2, data: [], decoded: {} };
    
    // @ts-ignore
    latestById.set({ "high:0x7FD": frame });
    
    const presence = get(ecuPresence);
    expect(presence.rt).toBe(true);
    expect(presence.sys).toBe(false);
  });

  it("handles stale ECU heartbeats", () => {
    const now = Date.now();
    // 5 seconds ago
    const frame: CanFrame = { ts: now - 5000, bus: "high", id: "0x7FD", name: "RT_HEARTBEAT", dlc: 2, data: [], decoded: {} };
    
    // @ts-ignore
    latestById.set({ "high:0x7FD": frame });
    
    const presence = get(ecuPresence);
    expect(presence.rt).toBe(false);
  });
  
  it("extracts correct motor speed in km/h", () => {
    // @ts-ignore
    latestById.set({
      "high:0x120": { decoded: { speed_mmps: 2000 } }
    });
    
    const telem = get(telemetry);
    // 2000 mm/s = 7.2 km/h
    expect(telem.motorSpeedKmh).toBe(7.2);
  });
  
  it("extracts correct brake pressure in MPa", () => {
    // @ts-ignore
    latestById.set({
      "low:0x205": { decoded: { brake_pressure_kpa: 10000 } }
    });
    
    const telem = get(telemetry);
    // 10000 kPa = 10 MPa
    expect(telem.brakePressureMpa).toBe(10);
  });
});
