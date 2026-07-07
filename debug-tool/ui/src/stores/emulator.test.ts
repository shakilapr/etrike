import { beforeEach, describe, expect, it } from "vitest";
import { get } from "svelte/store";
import { softwareSimEnabled } from "./emulator";

describe("softwareSimEnabled", () => {
  beforeEach(() => {
    softwareSimEnabled.set(false);
  });

  it("persists emulator mode outside component instances", () => {
    softwareSimEnabled.set(true);

    expect(get(softwareSimEnabled)).toBe(true);
  });
});
