import { describe, expect, it } from "vitest";
import { SessionTimebase, TimebaseMapper } from "../src/timebase";

describe("SessionTimebase", () => {
  it("generates monotonic timestamps", () => {
    const tb = new SessionTimebase();
    const t1 = tb.now();
    const t2 = tb.now();
    expect(BigInt(t2.ts_us)).toBeGreaterThan(BigInt(t1.ts_us));
    expect(t2.seq).toBeGreaterThan(t1.seq);
  });

  it("handles specific requested time", () => {
    const tb = new SessionTimebase(1000n);
    const t1 = tb.next(1000n);
    expect(t1.ts_us).toBe("1000");
    const t2 = tb.next(1000n);
    expect(t2.ts_us).toBe("1001");
  });
});

describe("TimebaseMapper", () => {
  it("maps external timestamps strictly monotonic", () => {
    const tb = new SessionTimebase(1000000n);
    const mapper = new TimebaseMapper(tb);
    
    // First map syncs to current session time
    const res1 = mapper.map(10.0);
    const us1 = BigInt(res1.ts_us);
    expect(us1).toBeGreaterThanOrEqual(1000000n);

    // 100ms later
    const res2 = mapper.map(10.1);
    const us2 = BigInt(res2.ts_us);
    expect(us2).toBeGreaterThanOrEqual(us1 + 100000n);

    // Negative clock jump - should still advance strictly by 1us
    const res3 = mapper.map(9.0);
    const us3 = BigInt(res3.ts_us);
    expect(us3).toBe(us2 + 1n);
  });
});
