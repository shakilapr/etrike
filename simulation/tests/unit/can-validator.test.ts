import { describe, it, expect, beforeEach } from "vitest";
import { CanValidator } from "../../src/checks/can-validator.js";

describe("CanValidator", () => {
  let v: CanValidator;

  beforeEach(() => {
    v = new CanValidator();
  });

  it("starts with zero errors", () => {
    expect(v.getAllErrors()).toHaveLength(0);
  });

  it("reports DLC mismatch", () => {
    v.validate(0, "0x300", "high", 4, 4, "host"); // expected 8
    expect(v.getAllErrors()).toHaveLength(1);
    expect(v.getAllErrors()[0].error).toContain("DLC mismatch");
  });

  it("accepts correct DLC", () => {
    v.validate(0, "0x300", "high", 8, 8, "host");
    v.validate(0, "0x204", "low", 5, 5, "rt");
    v.validate(0, "0x001", "high", 0, 0, "rt");
    expect(v.getAllErrors()).toHaveLength(0);
  });

  it("reports data length vs DLC mismatch", () => {
    v.validate(0, "0x011", "low", 2, 1, "sys"); // data shorter than DLC
    expect(v.getAllErrors()).toHaveLength(1);
  });
});
