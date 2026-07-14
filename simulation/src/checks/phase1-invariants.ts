import { expect } from "vitest";
import type { SimFrame, SimulationResult } from "../core/types.js";
import { routeForSimFrame } from "../protocol.js";
import { MAX_BRAKE_KPA, MAX_SPEED_FWD_MMPS, MAX_SPEED_REV_MMPS } from "../physics/tricycle.js";

export interface Phase1Trace {
  result: SimulationResult;
  frames: SimFrame[];
  mode: "manual" | "auto" | "estop";
}

function frames(trace: Phase1Trace, id: string, bus?: "high" | "low"): SimFrame[] {
  return trace.frames.filter(f => f.canId === id && (bus === undefined || f.bus === bus));
}

function i32be(data: number[], offset = 0): number {
  return (data[offset] << 24 | data[offset + 1] << 16 | data[offset + 2] << 8 | data[offset + 3]) >> 0;
}

export function assertAutoAuthority(trace: Phase1Trace): void {
  if (trace.mode !== "auto") return;
  expect(frames(trace, "0x300", "high").length).toBeGreaterThan(0);
  expect(frames(trace, "0x204", "low").length).toBeGreaterThan(0);
}

export function assertManualSilence(trace: Phase1Trace): void {
  if (trace.mode !== "manual") return;
  expect(frames(trace, "0x204").length).toBe(0);
  expect(frames(trace, "0x205").length).toBe(0);
  expect(frames(trace, "0x169").length).toBe(0);
}

export function assertEstopPriority(trace: Phase1Trace): void {
  if (trace.mode !== "estop") return;
  const estopFrames = frames(trace, "0x001");
  const firstEstopMs = estopFrames[0]?.simTimeMs ?? 0;
  const driveAfterEstop = frames(trace, "0x204").filter(f => f.simTimeMs >= firstEstopMs + 100);
  for (const f of driveAfterEstop.slice(-5)) {
    expect(i32be(f.data)).toBe(0);
  }
}

export function assertNoStaleActuation(trace: Phase1Trace): void {
  const lateDrive = frames(trace, "0x204").filter(f => f.simTimeMs > 700);
  for (const f of lateDrive) {
    expect(Math.abs(i32be(f.data))).toBeLessThanOrEqual(MAX_SPEED_FWD_MMPS);
  }
}

export function assertOutputBounds(trace: Phase1Trace): void {
  for (const f of frames(trace, "0x204")) {
    const speed = i32be(f.data);
    expect(speed).toBeGreaterThanOrEqual(-MAX_SPEED_REV_MMPS);
    expect(speed).toBeLessThanOrEqual(MAX_SPEED_FWD_MMPS);
  }
  for (const f of frames(trace, "0x205")) {
    const brake = i32be(f.data);
    expect(brake).toBeGreaterThanOrEqual(0);
    expect(brake).toBeLessThanOrEqual(MAX_BRAKE_KPA);
  }
}

export function assertCanValidity(trace: Phase1Trace): void {
  expect(trace.result.validationErrors).toEqual([]);
  for (const f of trace.frames) {
    expect(f.dlc).toBe(f.data.length);
    const route = routeForSimFrame(f);
    expect(route, `${f.canId} route on ${f.bus}`).toBeDefined();
    expect(route?.message.dlc, `${f.canId} generated DLC`).toBe(f.dlc);
  }
}

export function assertReconnectSafety(trace: Phase1Trace): void {
  expect(trace.result.violations).toEqual([]);
}

export function assertRateCompliance(trace: Phase1Trace): void {
  const durationS = Math.max(trace.result.durationMs / 1000, 0.001);
  const driveHz = frames(trace, "0x204").length / durationS;
  if (trace.mode === "auto") {
    expect(driveHz).toBeGreaterThan(50);
    expect(driveHz).toBeLessThan(130);
  }
}

export function assertDiagnosticVisibility(trace: Phase1Trace): void {
  expect(frames(trace, "0x210", "high").length).toBeGreaterThan(0);
  expect(frames(trace, "0x600", "low").length).toBeGreaterThan(0);
}

export function assertBypassVisibility(trace: Phase1Trace): void {
  const diag = frames(trace, "0x600", "low");
  expect(diag.length).toBeGreaterThan(0);
}

export function assertPhase1Invariants(trace: Phase1Trace): void {
  assertAutoAuthority(trace);
  assertManualSilence(trace);
  assertEstopPriority(trace);
  assertNoStaleActuation(trace);
  assertOutputBounds(trace);
  assertCanValidity(trace);
  assertReconnectSafety(trace);
  assertRateCompliance(trace);
  assertDiagnosticVisibility(trace);
  assertBypassVisibility(trace);
}
