import { ID_RT_HEARTBEAT, ID_HOST_DRIVE_CMD } from "@etrike/debug-shared";
import { describe, expect, it } from "vitest";
import { normalizeFilter } from "./ws";

describe("normalizeFilter", () => {
  it("preserves bus-scoped frame keys separately from bare IDs", () => {
    expect(normalizeFilter([`high:${ID_HOST_DRIVE_CMD}`, `low:${ID_HOST_DRIVE_CMD}`, ID_RT_HEARTBEAT])).toEqual({
      buses: ["high", "low"],
      ids: [ID_RT_HEARTBEAT],
      keys: [`high:${ID_HOST_DRIVE_CMD}`, `low:${ID_HOST_DRIVE_CMD}`]
    });
  });
});
