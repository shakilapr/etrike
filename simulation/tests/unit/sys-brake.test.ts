import { describe, it, expect, beforeEach } from "vitest";
import {
  SysBrakeController,
  BrakeState,
} from "../../src/controllers/sys-brake.js";

describe("SysBrakeController", () => {
  let bc: SysBrakeController;

  beforeEach(() => {
    bc = new SysBrakeController();
  });

  it("starts in BOOT_WAIT", () => {
    expect(bc.state).toBe(BrakeState.BOOT_WAIT);
  });

  it("does not transmit during BOOT_WAIT", () => {
    expect(bc.tick(false, false, 0)).toBeNull();
  });

  it("transitions to LISTEN_SYNC after 500ms", () => {
    // 25 ticks at 50Hz = 500ms
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    expect(bc.state).toBe(BrakeState.LISTEN_SYNC);
  });

  it("transitions to ACTIVE when 0x721 reports aligned", () => {
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    expect(bc.state).toBe(BrakeState.LISTEN_SYNC);

    bc.feedSebStatus(1); // aligned
    const cmd = bc.tick(false, false, 0);
    expect(bc.state).toBe(BrakeState.ACTIVE);
    expect(cmd).not.toBeNull();
  });

  it("ESTOP commands max stroke (27mm → raw 1140)", () => {
    // Boot → ACTIVE
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    bc.feedSebStatus(1);
    bc.tick(false, false, 0);

    const cmd = bc.tick(false, true, 0);
    expect(cmd).not.toBeNull();
    expect(cmd!.controlMode).toBe(0); // Stroke mode
    expect(cmd!.strokeReq).toBe(1140); // 27mm
  });

  it("brake pressure converts to Pressure Mode", () => {
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    bc.feedSebStatus(1);
    bc.tick(false, false, 0);

    // 5000 kPa → raw = (5000+25)/50 = 100 (max)
    const cmd = bc.tick(false, false, 5000);
    expect(cmd!.controlMode).toBe(1); // Pressure mode
    expect(cmd!.pressureReq).toBe(100);
  });

  it("manual lever commands 15mm stroke", () => {
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    bc.feedSebStatus(1);
    bc.tick(false, false, 0);

    const cmd = bc.tick(true, false, 0);
    expect(cmd!.controlMode).toBe(0); // Stroke mode
    expect(cmd!.strokeReq).toBe(900); // 15mm
  });

  it("released commands 0mm stroke", () => {
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    bc.feedSebStatus(1);
    bc.tick(false, false, 0);

    const cmd = bc.tick(false, false, 0);
    expect(cmd!.strokeReq).toBe(600); // 0mm
  });

  it("rolling counter increments each transmission", () => {
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    bc.feedSebStatus(1);
    const c1 = bc.tick(false, false, 0)!;
    const c2 = bc.tick(false, false, 0)!;
    expect(c2.rollingCounter).toBe((c1.rollingCounter + 1) & 0x0F);
  });

  it("DEGRADED recovers when 0x721 arrives", () => {
    // Go to LISTEN_SYNC then let it time out to DEGRADED
    for (let i = 0; i < 25; i++) bc.tick(false, false, 0);
    // 2000ms / 20ms = 100 ticks to DEGRADED
    for (let i = 0; i < 100; i++) bc.tick(false, false, 0);
    expect(bc.state).toBe(BrakeState.DEGRADED);

    // Now 0x721 arrives
    bc.feedSebStatus(1);
    bc.tick(false, false, 0);
    expect(bc.state).toBe(BrakeState.ACTIVE);
  });
});
