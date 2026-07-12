import { describe, it, expect, vi, beforeEach } from "vitest";
import { OperationalStateMachine, type ExecutionMode } from "../../src/state/machine";

describe("OperationalStateMachine", () => {
  let callbacks: { onDisarm: any; onModeSwitch: any };
  let machine: OperationalStateMachine;

  beforeEach(() => {
    callbacks = {
      onDisarm: vi.fn(),
      onModeSwitch: vi.fn(),
    };
    machine = new OperationalStateMachine(callbacks);
  });

  it("initializes in offline/disarmed", () => {
    expect(machine.state).toEqual({
      mode: "offline",
      arm: "disarmed",
      revision: 0n,
    });
  });

  it("arms if not offline", async () => {
    await machine.transitionMode("simulation", "full-sim");
    expect(machine.state.mode).toBe("simulation");
    
    await machine.arm();
    expect(machine.state.arm).toBe("armed");
  });

  it("throws when arming in offline mode", async () => {
    await expect(machine.arm()).rejects.toThrow(/Cannot arm in offline mode/);
    expect(machine.state.arm).toBe("disarmed");
  });

  it("disarms when transitioning mode", async () => {
    await machine.transitionMode("simulation", "bench");
    await machine.arm();
    expect(machine.state.arm).toBe("armed");

    await machine.transitionMode("monitor");
    expect(machine.state.arm).toBe("disarmed");
    expect(callbacks.onDisarm).toHaveBeenCalled();
  });

  it("falls back to offline on partial transition failure", async () => {
    callbacks.onModeSwitch.mockRejectedValue(new Error("Transition failed"));

    await expect(machine.transitionMode("simulation")).rejects.toThrow(/Transition failed/);
    expect(machine.state.mode).toBe("offline");
    expect(machine.state.arm).toBe("disarmed");
  });

  it("serializes concurrent requests correctly", async () => {
    let resolveFirst!: () => void;
    callbacks.onModeSwitch.mockImplementationOnce(() => {
      return new Promise<void>((resolve) => {
        resolveFirst = resolve;
      });
    });

    const t1 = machine.transitionMode("simulation", "1");
    const t2 = machine.transitionMode("simulation", "2");

    // Wait a tick for the microtasks to queue onModeSwitch
    await new Promise((resolve) => setTimeout(resolve, 0));

    // Let t1 finish
    resolveFirst();

    await t1;
    await t2;

    expect(machine.state.profile).toBe("2");
  });
});
