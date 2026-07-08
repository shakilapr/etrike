import { beforeEach, describe, expect, it } from "vitest";
import { get } from "svelte/store";
import { injectorBus } from "./injector";

describe("injectorBus", () => {
  beforeEach(() => {
    injectorBus.set("high");
  });

  it("persists the selected injector bus outside component instances", () => {
    injectorBus.set("low");

    expect(get(injectorBus)).toBe("low");
  });
});
