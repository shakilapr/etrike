import { describe, it, expect, beforeEach } from "vitest";
import {
  RtSteeringController,
  SteerState,
} from "../../src/controllers/rt-steering.js";

describe("RtSteeringController", () => {
  let sc: RtSteeringController;

  beforeEach(() => {
    sc = new RtSteeringController();
  });

  // ── State machine ────────────────────────────────────────────────

  it("starts in BOOT_WAIT", () => {
    expect(sc.state).toBe(SteerState.BOOT_WAIT);
  });

  it("does not transmit during BOOT_WAIT", () => {
    const out = sc.tick(null, 0, 0);
    expect(out).toBeNull();
  });

  it("transitions to LISTEN_SYNC after 500ms", () => {
    // Fire 25 ticks at 50 Hz = 500ms
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    expect(sc.state).toBe(SteerState.LISTEN_SYNC);
  });

  it("transitions to ACTIVE on valid 0x201 data", () => {
    // Boot
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);

    expect(sc.state).toBe(SteerState.LISTEN_SYNC);

    // Valid EPS-C data: angle = 30000 (0°), aligned = 1
    const now = 500;
    const out = sc.tick(30000, 1, now);

    expect(sc.state).toBe(SteerState.ACTIVE);
    expect(out).not.toBeNull();
    expect(out!.targetAngle).toBe(30000);
    expect(out!.alignEnable).toBe(1);
    expect(out!.controlEnable).toBe(1);
  });

  it("stays in LISTEN_SYNC when angle_status is 0 (center-finding)", () => {
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);

    // EPS-C sends angle but not yet aligned
    const out = sc.tick(30000, 0, 500);
    expect(sc.state).toBe(SteerState.LISTEN_SYNC);
    expect(out).toBeNull();
  });

  it("transitions to FAULT after 5s of no valid 0x201", () => {
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    expect(sc.state).toBe(SteerState.LISTEN_SYNC);

    // Advance to 5500ms — past the 5s sync timeout
    sc.tick(null, 0, 6000);
    expect(sc.state).toBe(SteerState.FAULT);
  });

  // ── Target setting ───────────────────────────────────────────────

  it("setTarget updates active angle in ACTIVE state", () => {
    // Boot + sync
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    sc.tick(30000, 1, 500);

    // Set target to 15° (15000 millidegrees)
    // Raw angle = 15000/100 + 3000 = 3150
    sc.setTarget(15000, 2000);

    const out = sc.tick(null, 1, 520);
    expect(out!.targetAngle).toBe(3150);
  });

  // ── ESTOP behavior ───────────────────────────────────────────────

  it("ramps toward zero on non-obstacle ESTOP", () => {
    // Boot + sync + set angle to 20° (20000 millidegrees → raw = 20000/100+3000 = 3200)
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    sc.tick(30000, 1, 500);
    sc.setTarget(20000, 2000);

    // Trigger ESTOP (non-obstacle)
    sc.startEstop(false, 600);
    expect(sc.state).toBe(SteerState.ESTOP_RAMP_TO_ZERO);

    // After first ramp tick, angle should have decreased
    const out = sc.tick(null, 1, 620);
    expect(out!.targetAngle).toBeLessThan(3200);
    expect(out!.targetAngle).toBeGreaterThan(0); // Not yet zero
  });

  it("holds then silences on obstacle ESTOP", () => {
    // Boot + sync
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    sc.tick(30000, 1, 500);
    sc.setTarget(10000, 2000); // 10° → raw = 10000/100+3000 = 3100

    // Trigger obstacle ESTOP
    sc.startEstop(true, 600);
    expect(sc.state).toBe(SteerState.ESTOP_HOLD_THEN_SILENT);

    // Still transmitting during hold
    const out1 = sc.tick(null, 1, 620);
    expect(out1).not.toBeNull();

    // After 500ms hold, should go silent
    const out2 = sc.tick(null, 1, 1200);
    expect(out2).toBeNull();
    expect(sc.state).toBe(SteerState.FAULT);
  });

  // ── Rolling counter ──────────────────────────────────────────────

  it("rolling counter increments each transmission", () => {
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    sc.tick(30000, 1, 500);

    const o1 = sc.tick(null, 1, 520);
    const o2 = sc.tick(null, 1, 540);
    const o3 = sc.tick(null, 1, 560);

    // Counter wraps at 16 (0–15)
    expect(o2!.rollingCounter).toBe((o1!.rollingCounter + 1) & 0x0F);
    expect(o3!.rollingCounter).toBe((o2!.rollingCounter + 1) & 0x0F);
  });

  // ── Reset ────────────────────────────────────────────────────────

  it("resetToListen recovers from FAULT", () => {
    // Go to FAULT via sync timeout
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    sc.tick(null, 0, 6000);
    expect(sc.state).toBe(SteerState.FAULT);

    sc.resetToListen(7000);
    expect(sc.state).toBe(SteerState.LISTEN_SYNC);
  });

  it("exitEstop returns to ACTIVE", () => {
    for (let i = 0; i < 26; i++) sc.tick(null, 0, i * 20);
    sc.tick(30000, 1, 500);
    sc.startEstop(false, 600);
    expect(sc.state).toBe(SteerState.ESTOP_RAMP_TO_ZERO);

    sc.exitEstop();
    expect(sc.state).toBe(SteerState.ACTIVE);
  });
});
