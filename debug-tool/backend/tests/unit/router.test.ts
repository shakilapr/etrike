import { describe, it, expect, beforeEach } from "vitest";
import { FrameRouter, type ProducerType, type RoutingContext } from "../../src/sim/router";
import type { CanFrame } from "../../src/types/can";

describe("FrameRouter", () => {
  let router: FrameRouter;
  
  beforeEach(() => {
    router = new FrameRouter();
  });

  const dummyFrame: CanFrame = {
    ts_us: "1000",
    seq: 0,
    bus: "high",
    frame: { id: "0100", dlc: 8, data: new Uint8Array(8) }
  };

  it("physical_rx cannot produce physical_tx", () => {
    const disp = router.route(dummyFrame, { producer: "physical_rx" });
    expect(disp.physical_tx).toBe(false);
    expect(disp.ui).toBe(true);
    expect(disp.recording).toBe(true);
  });

  it("simulation cannot produce physical_tx", () => {
    const disp = router.route(dummyFrame, { producer: "simulation" });
    expect(disp.physical_tx).toBe(false);
    expect(disp.ui).toBe(true);
    expect(disp.recording).toBe(true);
  });

  it("replay cannot produce physical_tx or sim_input", () => {
    const disp = router.route(dummyFrame, { producer: "replay" });
    expect(disp.physical_tx).toBe(false);
    expect(disp.sim_input).toBe(false);
    expect(disp.ui).toBe(true);
    expect(disp.recording).toBe(false);
  });

  it("user can produce physical_tx", () => {
    const disp = router.route(dummyFrame, { producer: "user" });
    expect(disp.physical_tx).toBe(true);
    expect(disp.ui).toBe(true);
    expect(disp.recording).toBe(true);
  });

  it("test cannot produce physical_tx, only isolated test engine", () => {
    const disp = router.route(dummyFrame, { producer: "test" });
    expect(disp.physical_tx).toBe(false);
    expect(disp.sim_input).toBe(true);
    expect(disp.ui).toBe(false);
    expect(disp.recording).toBe(false);
  });

  it("detects loop and rejects sim_input on collision", () => {
    router.setSource("high", "0100", "physical");
    
    // physical rx for 0100 should be allowed into sim_input
    const disp1 = router.route(dummyFrame, { producer: "physical_rx" });
    expect(disp1.sim_input).toBe(true);

    // simulation trying to send 0100 should collide and be rejected from sim_input
    const disp2 = router.route(dummyFrame, { producer: "simulation" });
    expect(disp2.sim_input).toBe(false);
  });
  
  it("assigns sequential sequence numbers", () => {
    const disp1 = router.route(dummyFrame, { producer: "physical_rx" });
    const disp2 = router.route(dummyFrame, { producer: "physical_rx" });
    
    expect(disp1.frame?.seq).toBe(1);
    expect(disp2.frame?.seq).toBe(2);
  });
});
