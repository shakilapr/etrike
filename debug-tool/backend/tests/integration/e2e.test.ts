import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { FrameRouter } from "../../src/sim/router";
import { OperationalStateMachine } from "../../src/state/machine";
import { InjectionService } from "../../src/api/injection";
import { LeaseManager } from "../../src/state/leases";
import { DebugStoreImpl } from "../../src/db/queries";
import { WriteQueue } from "../../src/db/write-queue";

describe("E2E System Test", () => {
  let router: FrameRouter;
  let stateMachine: OperationalStateMachine;
  let leaseManager: LeaseManager;
  let store: DebugStoreImpl;
  let writeQueue: WriteQueue;
  let injectionService: InjectionService;

  beforeEach(() => {
    store = new DebugStoreImpl(":memory:");
    writeQueue = new WriteQueue(store, 10);
    stateMachine = new OperationalStateMachine({
      onModeSwitch: async () => {},
      onArmingSwitch: async () => {},
      onDisarm: async () => {}
    });
    leaseManager = new LeaseManager();
    
    router = new FrameRouter(
      stateMachine,
      () => {}, // physical TX
      (frame) => writeQueue.enqueue(frame, "physical")
    );

    const appCtx = {
      stateMachine,
      leaseManager,
      router,
      store
    } as any;

    injectionService = new InjectionService(appCtx);
  });

  afterEach(() => {
    store.close();
  });

  it("completes full injection loop", async () => {
    // 1. Initial state
    expect(stateMachine.state.mode).toBe("offline");

    // 2. Set to simulation mode
    await stateMachine.transitionMode("simulation");
    expect(stateMachine.state.mode).toBe("simulation");
    
    await stateMachine.arm();
    expect(stateMachine.state.arm).toBe("armed");

    leaseManager.acquire("motor", "test-user");

    // 3. Inject drive command
    const frame: any = {
      bus: "high",
      frame: { id: "0x300", dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0] },
      decoded: { signals: {} },
      ts: Date.now() * 1000,
      ts_real: Date.now() / 1000
    };

    const validation = injectionService.validate(frame, { ownerId: "test-user" });
    if (!validation.allowed) console.log(validation.error);
    expect(validation.allowed).toBe(true);

    const disp = router.route(frame, { producer: "user" });
    expect(disp.accepted).toBe(true);

    if (disp.recording) {
      writeQueue.enqueue(disp.frame, "user");
    }

    // 4. Verify routing and db queue
    // 4. Verify routing and db queue
    const metrics = writeQueue.getMetrics();
    expect(metrics.depth).toBe(1);

    // Flush to DB
    await writeQueue.flush();
    const frames = store.queryFrames();
    expect(frames.length).toBe(1);
    expect(frames[0].frame.id).toBe("0x300");
  });
});
