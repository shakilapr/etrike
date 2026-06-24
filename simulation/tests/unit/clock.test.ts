import { describe, it, expect, beforeEach } from "vitest";
import { SimulationClock } from "../../src/core/clock.js";

describe("SimulationClock", () => {
  let clock: SimulationClock;

  beforeEach(() => {
    clock = new SimulationClock(0); // step mode
  });

  it("starts at t=0", () => {
    expect(clock.nowMs).toBe(0);
    expect(clock.ticks).toBe(0);
  });

  it("advances by 1 ms per tick in step mode", () => {
    clock.tick();
    expect(clock.nowMs).toBe(1);
    expect(clock.ticks).toBe(1);

    clock.tick();
    expect(clock.nowMs).toBe(2);
    expect(clock.ticks).toBe(2);
  });

  it("step() advances by N ticks", () => {
    clock.step(100);
    expect(clock.nowMs).toBe(100);
    expect(clock.ticks).toBe(100);
  });

  it("paused clock does not advance", () => {
    clock.paused = true;
    clock.tick();
    expect(clock.nowMs).toBe(0);
    expect(clock.ticks).toBe(0);
  });

  it("reset() returns to zero", () => {
    clock.step(500);
    clock.reset();
    expect(clock.nowMs).toBe(0);
    expect(clock.ticks).toBe(0);
    expect(clock.paused).toBe(false);
  });

  it("nowSec returns seconds", () => {
    clock.step(1500);
    expect(clock.nowSec).toBe(1.5);
  });

  it("speed property is stored but does not affect step mode", () => {
    clock.speed = 100;
    clock.tick();
    expect(clock.nowMs).toBe(1); // still 1ms per tick
  });
});
