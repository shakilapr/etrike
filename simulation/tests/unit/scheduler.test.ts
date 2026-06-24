import { describe, it, expect, beforeEach, vi } from "vitest";
import { TimeSliceScheduler } from "../../src/core/scheduler.js";

describe("TimeSliceScheduler", () => {
  let sched: TimeSliceScheduler;

  beforeEach(() => {
    sched = new TimeSliceScheduler();
  });

  it("fires a task at its period", () => {
    const cb = vi.fn();
    sched.register("test", 10, cb);

    // First fire at t=0
    sched.run(0);
    expect(cb).toHaveBeenCalledTimes(1);

    // Not due again until t=10
    sched.run(5);
    expect(cb).toHaveBeenCalledTimes(1);

    sched.run(10);
    expect(cb).toHaveBeenCalledTimes(2);
  });

  it("catches up if behind by multiple periods", () => {
    const cb = vi.fn();
    sched.register("fast", 5, cb);

    // Jump ahead by 15ms — should fire 4 times (t=0,5,10,15)
    sched.run(15);
    expect(cb).toHaveBeenCalledTimes(4);
  });

  it("supports custom start time", () => {
    const cb = vi.fn();
    sched.register("delayed", 10, cb, 20);

    // Not due until t=20
    sched.run(10);
    expect(cb).toHaveBeenCalledTimes(0);

    sched.run(20);
    expect(cb).toHaveBeenCalledTimes(1);
  });

  it("unregister removes a task", () => {
    const cb = vi.fn();
    const task = sched.register("temp", 10, cb);

    sched.run(0);
    expect(cb).toHaveBeenCalledTimes(1);

    sched.unregister(task);
    sched.run(10);
    expect(cb).toHaveBeenCalledTimes(1); // no more calls
  });

  it("reset reschedules all tasks", () => {
    const cb = vi.fn();
    sched.register("r", 10, cb);

    sched.run(0);
    expect(cb).toHaveBeenCalledTimes(1);

    // Reset at t=5 — next fire moves to t=15
    sched.reset(5);
    sched.run(10);
    expect(cb).toHaveBeenCalledTimes(1); // not due yet

    sched.run(15);
    expect(cb).toHaveBeenCalledTimes(2);
  });

  it("count returns number of registered tasks", () => {
    expect(sched.count).toBe(0);
    sched.register("a", 10, () => {});
    sched.register("b", 20, () => {});
    expect(sched.count).toBe(2);
  });
});
