import { describe, it, expect, beforeEach } from "vitest";
import { SysSafetyMonitor } from "../../src/controllers/sys-safety.js";

describe("SysSafetyMonitor", () => {
  let m: SysSafetyMonitor;

  beforeEach(() => {
    m = new SysSafetyMonitor();
  });

  it("estop defaults to false", () => {
    expect(m.estop).toBe(false);
    expect(m.brakeLever).toBe(false);
  });

  it("setEstop toggles GPIO state", () => {
    m.setEstop(true);
    expect(m.estop).toBe(true);
  });

  it("setBrakeLever toggles lever state", () => {
    m.setBrakeLever(true);
    expect(m.brakeLever).toBe(true);
  });

  it("heartbeatOk is true during startup grace period", () => {
    // No heartbeat ever seen, should be OK for first 3s
    expect(m.heartbeatOk(0)).toBe(true);
    expect(m.heartbeatOk(2000)).toBe(true);
  });

  it("heartbeatOk is false after grace period with no heartbeat", () => {
    expect(m.heartbeatOk(3500)).toBe(false);
  });

  it("feeding heartbeat keeps it OK", () => {
    m.feedHeartbeatRt(500, 10);
    expect(m.heartbeatOk(500)).toBe(true);
    expect(m.heartbeatOk(1200)).toBe(true); // 700ms later — still within 1000ms
  });

  it("heartbeat times out after 1000ms", () => {
    m.feedHeartbeatRt(100, 5);
    expect(m.heartbeatOk(1099)).toBe(true);  // within timeout (<1000ms diff)
    expect(m.heartbeatOk(1100)).toBe(false); // exactly at timeout (exclusive)
  });

  it("frozen counter does not update timestamp", () => {
    m.feedHeartbeatRt(100, 5);
    m.feedHeartbeatRt(200, 5); // same counter — frozen, discarded
    // Last valid timestamp was at 100ms
    expect(m.heartbeatOk(1099)).toBe(true);  // still OK (<1000ms diff)
    expect(m.heartbeatOk(1100)).toBe(false); // times out at exactly 1000ms
  });

  it("EGAS L2 detects speed mismatch", () => {
    // First tick: mismatch detected but not yet persisted
    expect(m.checkEgasL2(100, 2000, 1000)).toBe(false); // 1000mm/s mismatch
    // Persists for 500ms
    expect(m.checkEgasL2(600, 2000, 1000)).toBe(true); // now it fires!
  });

  it("EGAS L2 clears if mismatch resolves", () => {
    m.checkEgasL2(100, 2000, 1000);
    expect(m.checkEgasL2(300, 2000, 1000)).toBe(false);
    // Mismatch resolves before 500ms
    m.checkEgasL2(400, 2000, 1950); // only 50mm/s difference
    // Timer should have been reset
    expect(m.checkEgasL2(800, 2000, 1950)).toBe(false);
  });

  it("gap #15: SYS detects MTR ESTOP_ACTIVE bit in 0x206 within 100ms", () => {
    // Trigger ESTOP
    m.setEstop(true);

    // Simulate MTR 0x206 with ESTOP_ACTIVE bit (bit0 = 1 = faultFlags)
    const mtrFbk = { actualSpeed: 0, gearState: 0, faultFlags: 0x01 };
    m.feedMtrFeedback(mtrFbk, 100); // t=100ms

    expect(m.mtrEstopAcked()).toBe(true);
  });
});
