import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { DebugStore } from "../db/queries";
import type { SerialBridge } from "../serial/reader";
import { findMessage, INJECTION_TEMPLATES, normalizeBus, normalizeCanId, validateDataBytes } from "../types/can";

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

export function registerCommandRoutes(app: FastifyInstance, store: DebugStore, serial: SerialBridge): void {
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

    store.insertInjection({ bus, can_id: id, dlc: parsed.data.dlc, data, status: "queued" });
    try {
      serial.sendCommand({ cmd: "send", bus, id, dlc: parsed.data.dlc, data });
    } catch (error) {
      store.updateLatestInjectionStatus("error");
      return reply.code(503).send({ error: error instanceof Error ? error.message : String(error) });
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
    if (id === "0x001" && "confirm_estop" in parsed.data && parsed.data.confirm_estop !== true) {
      return reply.code(400).send({ error: "ESTOP injection requires confirm_estop=true" });
    }

    if (parsed.data.action === "stop") {
      try {
        serial.sendCommand({ cmd: "send_periodic", action: "stop", bus, id });
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

    store.insertInjection({ bus, can_id: id, dlc: parsed.data.dlc, data, status: "queued" });
    try {
      serial.sendCommand({ cmd: "send_periodic", action: "start", bus, id, dlc: parsed.data.dlc, data, interval_ms: parsed.data.interval_ms, count: parsed.data.count });
    } catch (error) {
      store.updateLatestInjectionStatus("error");
      return reply.code(503).send({ error: error instanceof Error ? error.message : String(error) });
    }
    return { cmd: "send_periodic", action: "start", bus, id, status: "queued" };
  });
}
