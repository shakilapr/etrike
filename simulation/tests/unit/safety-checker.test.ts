import { describe, it, expect, beforeEach } from "vitest";
import { SafetyChecker } from "../../src/checks/safety-checker.js";

describe("SafetyChecker", () => {
  let sc: SafetyChecker;

  beforeEach(() => {
    sc = new SafetyChecker();
  });

  it("starts with zero violations", () => {
    expect(sc.getAllViolations()).toHaveLength(0);
  });

  it("records violations", () => {
    sc.add(100, "test", "something went wrong");
    expect(sc.getAllViolations()).toHaveLength(1);
    expect(sc.getAllViolations()[0].type).toBe("test");
  });

  it("detects slow ESTOP response", () => {
    const ctx = { nowMs: 600, ticks: 600, mode: "auto" as const, estopActive: true, brakeLeverPressed: false };
    sc.triggerEstop(0);
    sc.checkEstopResponse(600, ctx, true, true);
    expect(sc.getAllViolations()[0]?.type).toBe("estop_response");
  });

  it("does not flag ESTOP response under 500ms", () => {
    const ctx = { nowMs: 300, ticks: 300, mode: "auto" as const, estopActive: true, brakeLeverPressed: false };
    sc.triggerEstop(0);
    sc.checkEstopResponse(300, ctx, true, true);
    expect(sc.getAllViolations()).toHaveLength(0);
  });

  it("gap #14: rate-limits ESTOP frames to 2 per 500ms window", () => {
    // Flood 10 ESTOPs in 100ms (10ms apart)
    let estopCount = 0;
    for (let i = 0; i < 10; i++) {
      if (sc.processEstop(100 + i * 10)) estopCount++;
    }

    // Only first 2 should be accepted within the 500ms window
    expect(estopCount).toBeLessThanOrEqual(2);
  });
});
