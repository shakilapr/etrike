import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import Fastify, { type FastifyInstance } from "fastify";
import { registerCommandRoutes } from "../../src/api/cmd";
import { DebugStore } from "../../src/db/queries";
import type { HardwareBridge } from "../../src/bridge/types";

describe("POST /api/cmd/send", () => {
  let app: FastifyInstance;
  let store: DebugStore;
  let mockBridge: HardwareBridge;

  beforeEach(() => {
    app = Fastify();
    app.decorate("ctx", { simEngine: null });
    store = new DebugStore(":memory:", 5000);
    
    mockBridge = {
      sendCommand: vi.fn(),
      close: vi.fn(),
      start: vi.fn(),
      waitForConnection: vi.fn(),
      abandon: vi.fn()
    } as unknown as HardwareBridge;

    registerCommandRoutes(app, store, mockBridge);
  });

  afterEach(async () => {
    store.close();
    await app.close();
  });

  it("400 when missing fields", async () => {
    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high" } // missing id, dlc, data
    });
    expect(res.statusCode).toBe(400);
  });

  it("400 when DLC does not match data array length", async () => {
    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high", id: "0x300", dlc: 8, data: [1, 2, 3] } // dlc 8, but array has 3 elements
    });
    expect(res.statusCode).toBe(400);
    expect(JSON.parse(res.payload).error).toMatch(/length must match dlc/i);
  });

  it("400 when data byte > 255", async () => {
    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high", id: "0x300", dlc: 1, data: [256] }
    });
    expect(res.statusCode).toBe(400);
    // Could fail Zod validation or validateDataBytes validation
    expect(res.payload).toMatch(/255/);
  });

  it("400 when ID is not marked injectable (BUG-21 regression)", async () => {
    // 0x220 RT_PID_RPT is injectable: false
    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high", id: "0x220", dlc: 6, data: [0, 0, 0, 0, 0, 0] }
    });
    expect(res.statusCode).toBe(400);
    expect(JSON.parse(res.payload).error).toMatch(/not injectable/i);
  });

  it("400 when injecting ESTOP without confirm_estop=true", async () => {
    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high", id: "0x001", dlc: 0, data: [] } // missing confirm_estop
    });
    expect(res.statusCode).toBe(400);
    expect(JSON.parse(res.payload).error).toMatch(/confirm_estop/i);
  });

  it("200 writes to DB with correlation_id and status=queued", async () => {
    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high", id: "0x300", dlc: 8, data: [0,0,0,0,0,0,0,0] }
    });
    
    expect(res.statusCode).toBe(200);
    
    // Bridge should have been called
    expect(mockBridge.sendCommand).toHaveBeenCalled();
    const callArgs = (mockBridge.sendCommand as any).mock.calls[0][0];
    expect(callArgs.correlation_id).toBeDefined();

    // Verify DB entry
    const injections = store.listInjections();
    expect(injections.length).toBe(1);
    expect(injections[0].status).toBe("queued");
  });

  it("503 updates DB status=error when bridge throws exception", async () => {
    mockBridge.sendCommand = vi.fn().mockImplementation(() => {
      throw new Error("Bridge offline");
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/cmd/send",
      payload: { bus: "high", id: "0x300", dlc: 8, data: [0,0,0,0,0,0,0,0] }
    });
    
    expect(res.statusCode).toBe(503);
    
    // Verify DB entry status updated to error
    const injections = store.listInjections();
    expect(injections.length).toBe(1);
    expect(injections[0].status).toBe("error");
  });
});
