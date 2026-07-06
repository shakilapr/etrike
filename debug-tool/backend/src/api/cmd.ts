import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { HardwareBridge } from "../bridge/types";
import type { DebugStore } from "../db/queries";
import { findMessage, INJECTION_TEMPLATES, normalizeBus, normalizeCanId, normalizeFrame, validateDataBytes } from "../types/can";

const busSchema = z.enum(["high", "low"]);

const sendSchema = z.object({
  bus: busSchema,
  id: z.string().min(1),
  dlc: z.number().int().min(0).max(8),
  data: z.array(z.number().int().min(0).max(255)),
  confirm_estop: z.boolean().optional()
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
    confirm_estop: z.boolean().optional()
  }),
  z.object({
    action: z.literal("stop"),
    bus: busSchema,
    id: z.string().min(1)
  })
]);

export function registerCommandRoutes(app: FastifyInstance, store: DebugStore, bridge: HardwareBridge): void {
  app.get("/api/templates", async () => ({ templates: INJECTION_TEMPLATES }));
  app.get("/api/cmd/history", async () => ({ injections: store.listInjections() }));

  app.post("/api/cmd/send", async (request, reply) => {
    const parsed = sendSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const bus = normalizeBus(parsed.data.bus);
    const id = normalizeCanId(parsed.data.id);
    const definition = findMessage(bus, id);
    if (!definition?.injectable) return reply.code(400).send({ error: `${id} is not injectable on ${bus} bus` });
    if (id === "0x001" && parsed.data.confirm_estop !== true) return reply.code(400).send({ error: "ESTOP injection requires confirm_estop=true" });

    let data: number[];
    try {
      data = validateDataBytes(parsed.data.data, parsed.data.dlc);
    } catch (error) {
      return reply.code(400).send({ error: String(error) });
    }

    const correlationId = crypto.randomUUID();
    store.insertInjection({ bus, can_id: id, dlc: parsed.data.dlc, data, status: "queued", correlation_id: correlationId });

    // Try physical bridge first. If it fails and sim engine is running,
    // inject into virtual CAN bus instead.
    let sent = false;
    try {
      bridge.sendCommand({ cmd: "send", bus, id, dlc: parsed.data.dlc, data, correlation_id: correlationId });
      sent = true;
    } catch {
      // Check for simulation engine fallback
      const simEngine = (app as any).__simEngine as { injectExternal(f: ReturnType<typeof normalizeFrame>): void } | undefined;
      if (simEngine) {
        const frame = normalizeFrame({ bus, id, dlc: parsed.data.dlc, data });
        simEngine.injectExternal(frame);
        store.updateInjectionByCorrelation(correlationId, "simulated");
        sent = true;
      }
    }
    if (!sent) {
      store.updateInjectionByCorrelation(correlationId, "error");
      return reply.code(503).send({ error: "no bridge connected and simulation not running" });
    }
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

    if (id === "0x001" && parsed.data.confirm_estop !== true) {
      return reply.code(400).send({ error: "ESTOP injection requires confirm_estop=true" });
    }

    let data: number[];
    try {
      data = validateDataBytes(parsed.data.data, parsed.data.dlc);
    } catch (error) {
      return reply.code(400).send({ error: String(error) });
    }

    const correlationId = crypto.randomUUID();
    store.insertInjection({ bus, can_id: id, dlc: parsed.data.dlc, data, status: "queued", correlation_id: correlationId });
    try {
      bridge.sendCommand({ cmd: "send_periodic", action: "start", bus, id, dlc: parsed.data.dlc, data, interval_ms: parsed.data.interval_ms, count: parsed.data.count, correlation_id: correlationId });
    } catch (error) {
      store.updateInjectionByCorrelation(correlationId, "error");
      return reply.code(503).send({ error: error instanceof Error ? error.message : String(error) });
    }
    return { cmd: "send_periodic", action: "start", bus, id, status: "queued" };
  });
}
