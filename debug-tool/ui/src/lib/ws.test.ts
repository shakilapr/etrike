import { describe, expect, it } from "vitest";
import { normalizeFilter } from "./ws";

describe("normalizeFilter", () => {
  it("preserves bus-scoped frame keys separately from bare IDs", () => {
    expect(normalizeFilter(["high:0x300", "low:0x300", "0x7FD"])).toEqual({
      buses: ["high", "low"],
      ids: ["0x7FD"],
      keys: ["high:0x300", "low:0x300"]
    });
  });
});
