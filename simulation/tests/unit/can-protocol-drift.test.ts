import { describe, expect, it } from "vitest";
import { BUS, DLC, NAME } from "../../../shared/can/generated/can_ids";

const requiredFrames = [
  ["0x001", "SAFETY_ESTOP", 0, "both"],
  ["0x110", "SYS_MODE_CMD", 1, "low"],
  ["0x011", "SYS_SAFETY_STS", 3, "both"],
  ["0x210", "RT_STATE_RPT", 6, "high"],
  ["0x204", "RT_DRIVE_CMD", 5, "low"],
  ["0x205", "RT_BRAKE_CMD", 4, "low"],
  ["0x300", "HOST_DRIVE_CMD", 8, "high"],
  ["0x301", "HOST_BRAKE_REQ", 4, "high"],
  ["0x169", "VCU_SES_REQ", 8, "low"],
  ["0x201", "SES_STATUS", 8, "low"],
  ["0x7B9", "VCU_SEB_REQ", 8, "low"],
  ["0x721", "SEB_STATUS", 8, "low"],
  ["0x7FD", "RT_HEARTBEAT", 2, "both"],
  ["0x7FE", "SYS_HEARTBEAT", 2, "low"],
  ["0x7FC", "HOST_HEARTBEAT", 1, "high"],
] as const;

describe("generated CAN contract drift", () => {
  for (const [id, name, dlc, bus] of requiredFrames) {
    it(`${id} ${name}`, () => {
      expect(NAME[id]).toBe(name);
      expect(DLC[id]).toBe(dlc);
      expect(BUS[id]).toBe(bus);
    });
  }
});
