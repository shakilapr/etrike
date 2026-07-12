import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { HardwareBridge } from "../bridge/types";
import type { DebugStore } from "../db/queries";
import { findMessage, INJECTION_TEMPLATES, normalizeBus, normalizeCanId, validateDataBytes, ID_SAFETY_ESTOP, normalizeFrame } from "../types/can";

const busSchema = z.enum(["high", "low"]);

const sendSchema = z.object({
  bus: busSchema,
  id: z.string().min(1),
  dlc: z.number().int().min(0).max(8),
  data: z.array(z.number().int().min(0).max(255)),
  confirm_estop: z.boolean().optional(),
  owner_id: z.string().optional()
}).refine(data => data.data.length === data.dlc, {
  message: "data array length must match dlc",
  path: ["data"]
});

const periodicSchema = z.discriminatedUnion("action", [
  z.object({
    action: z.literal("start"),
    bus: busSchema,
    id: z.string().min(1),
    dlc: z.number().int().min(0).max(8),
    data: z.array(z.number().int().min(0).max(255)),
    interval_ms: z.number().int().min(1).max(10000),
    count: z.number().int().min(1).max(50000).optional(),
    confirm_estop: z.boolean().optional(),
    owner_id: z.string().optional()
  }),
  z.object({
    action: z.literal("stop"),
    bus: busSchema,
    id: z.string().min(1)
  })
]).refine(val => val.action === "stop" || val.data.length === val.dlc, {
  message: "data array length must match dlc",
  path: ["data"]
});

export function registerCommandRoutes(app: FastifyInstance, store: DebugStore, bridge: HardwareBridge): void {
  app.get("/api/templates", async () => ({ templates: INJECTION_TEMPLATES }));
  app.get("/api/cmd/history", async () => ({ injections: await store.listInjections() }));

  app.post("/api/cmd/send", async (request, reply) => {
    const parsed = sendSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const bus = normalizeBus(parsed.data.bus);
    const id = normalizeCanId(parsed.data.id);
    const definition = findMessage(bus, id);
    if (!definition?.injectable) return reply.code(400).send({ error: `${id} is not injectable on ${bus} bus` });

    let data: number[];
    try {
      data = validateDataBytes(parsed.data.data, parsed.data.dlc);
    } catch (error) {
      return reply.code(400).send({ error: String(error) });
    }

    const correlationId = crypto.randomUUID();
    await store.insertInjection({ bus, can_id: id, dlc: parsed.data.dlc, data, status: "queued", correlation_id: correlationId });

    const frame = normalizeFrame({ bus, id, dlc: parsed.data.dlc, data });
    const validation = app.ctx.injectionService.validate(frame, { ownerId: parsed.data.owner_id, confirmEstop: parsed.data.confirm_estop });
    if (!validation.allowed) {
      await store.updateInjectionByCorrelation(correlationId, "error");
      return reply.code(403).send({ error: validation.error });
    }

    const disp = app.ctx.router.route(frame, { producer: "user" });

    if (!disp.accepted || !disp.frame) {
      await store.updateInjectionByCorrelation(correlationId, "error");
      return reply.code(403).send({ error: "Routing policy rejected user injection" });
    }

    if (disp.sim_input && app.ctx.simEngine?.state?.running) {
      app.ctx.simEngine.injectExternal(disp.frame);
      await store.updateInjectionByCorrelation(correlationId, "simulated");
      return { cmd: "send", bus, id, status: "queued" };
    }

    if (disp.physical_tx) {
      try {
        bridge.sendCommand({ cmd: "send", bus, id, dlc: parsed.data.dlc, data, correlation_id: correlationId });
        return { cmd: "send", bus, id, status: "queued" };
      } catch {
        await store.updateInjectionByCorrelation(correlationId, "error");
        return reply.code(503).send({ error: "bridge not connected" });
      }
    }

    await store.updateInjectionByCorrelation(correlationId, "error");
    return reply.code(503).send({ error: "no destination allowed by router" });
    return { cmd: "send", bus, id, status: "queued" };
  });

  app.post("/api/cmd/periodic", async (request, reply) => {
    const parsed = periodicSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const bus = normalizeBus(parsed.data.bus);
    const id = normalizeCanId(parsed.data.id);
    const definition = findMessage(bus, id);
    if (!definition?.injectable) return reply.code(400).send({ error: `${id} is not injectable on ${bus} bus` });
    if (parsed.data.action === "stop") {
      try {
        bridge.sendCommand({ cmd: "send_periodic", action: "stop", bus, id });
      } catch (error) {
        return reply.code(503).send({ error: error instanceof Error ? error.message : String(error) });
      }
      return { cmd: "send_periodic", action: "stop", bus, id, status: "queued" };
    }

    let data: number[];
    try {
      data = validateDataBytes(parsed.data.data, parsed.data.dlc);
    } catch (error) {
      return reply.code(400).send({ error: String(error) });
    }

    const frame = normalizeFrame({ bus, id, dlc: parsed.data.dlc, data });
    const validation = app.ctx.injectionService.validate(frame, { ownerId: parsed.data.owner_id, confirmEstop: parsed.data.confirm_estop });
    if (!validation.allowed) {
      return reply.code(403).send({ error: validation.error });
    }

    const correlationId = crypto.randomUUID();
    await store.insertInjection({ bus, can_id: id, dlc: parsed.data.dlc, data, status: "queued", correlation_id: correlationId });
    try {
      bridge.sendCommand({ cmd: "send_periodic", action: "start", bus, id, dlc: parsed.data.dlc, data, interval_ms: parsed.data.interval_ms, count: parsed.data.count, correlation_id: correlationId });
    } catch (error) {
      await store.updateInjectionByCorrelation(correlationId, "error");
      return reply.code(503).send({ error: error instanceof Error ? error.message : String(error) });
    }
    return { cmd: "send_periodic", action: "start", bus, id, status: "queued" };
  });
}
