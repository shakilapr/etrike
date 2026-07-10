import { ID_HOST_DRIVE_CMD, ID_SAFETY_ESTOP } from "@etrike/debug-shared";
import { beforeEach, describe, expect, it } from "vitest";
import { get } from "svelte/store";
import {
  injectorBus,
  injectorConfirmEstop,
  injectorCount,
  injectorIntervalMs,
  injectorSelectedId,
  injectorValues
} from "./injector";

describe("injector state", () => {
  beforeEach(() => {
    injectorBus.set("high");
    injectorSelectedId.set(ID_HOST_DRIVE_CMD);
    injectorValues.set({ speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 });
    injectorConfirmEstop.set(false);
    injectorIntervalMs.set(20);
    injectorCount.set(500);
  });

  it("persists the selected injector bus outside component instances", () => {
    injectorBus.set("low");

    expect(get(injectorBus)).toBe("low");
  });

  it("persists selected ID and form values outside component instances", () => {
    injectorSelectedId.set(ID_SAFETY_ESTOP);
    injectorValues.set({ estop_active: true });
    injectorConfirmEstop.set(true);
    injectorIntervalMs.set(50);
    injectorCount.set(20);

    expect(get(injectorSelectedId)).toBe(ID_SAFETY_ESTOP);
    expect(get(injectorValues)).toEqual({ estop_active: true });
    expect(get(injectorConfirmEstop)).toBe(true);
    expect(get(injectorIntervalMs)).toBe(50);
    expect(get(injectorCount)).toBe(20);
  });
});
