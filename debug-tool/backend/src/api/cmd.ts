import crypto from "node:crypto";
import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { DebugStore } from "../db/queries";
import type { MqttBridge } from "../mqtt/client";
import { findMessage, INJECTION_TEMPLATES, normalizeCanId, validateDataBytes } from "../types/can";

const sendSchema = z.object({
  request_id: z.string().min(1).optional(),
  bus: z.enum(["high", "low"]).optional(),
  id: z.string().min(1),
  dlc: z.number().int().min(0).max(8),
  data: z.array(z.number().int().min(0).max(255)),
  confirm_estop: z.boolean().optional()
});

const periodicSchema = z.discriminatedUnion("action", [
  z.object({
    request_id: z.string().min(1).optional(),
    action: z.literal("start"),
    bus: z.enum(["high", "low"]).optional(),
    id: z.string().min(1),
    dlc: z.number().int().min(0).max(8),
    data: z.array(z.number().int().min(0).max(255)),
    interval_ms: z.number().int().min(1).max(60000),
    count: z.number().int().min(1).max(50000).optional(),
    confirm_estop: z.boolean().optional()
  }),
  z.object({
    request_id: z.string().min(1).optional(),
    action: z.literal("stop"),
    id: z.string().min(1)
  })
]);

export function registerCommandRoutes(app: FastifyInstance, store: DebugStore, bridge: MqttBridge): void {
  app.get("/api/templates", async () => ({ templates: INJECTION_TEMPLATES }));

  app.get("/api/cmd/history", async () => ({ injections: store.listInjections() }));

  app.post("/api/cmd/send", async (request, reply) => {
    const parsed = sendSchema.safeParse(request.body);
    if (!parsed.success) {
      return reply.code(400).send({ error: parsed.error.flatten() });
    }

    const id = normalizeCanId(parsed.data.id);
    const definition = findMessage(parsed.data.bus ?? "high", id);
    if (!definition?.injectable) {
      return reply.code(400).send({ error: `${id} is not injectable` });
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

    const requestId = parsed.data.request_id ?? crypto.randomUUID();
    const payload = {
      request_id: requestId,
      bus: definition.bus,
      id,
      dlc: parsed.data.dlc,
      data
    };

    store.insertInjection({
      request_id: requestId,
      can_id: id,
      dlc: parsed.data.dlc,
      data
    });

    bridge.publishJson("etrike/debug/cmd/send", payload);
    return { request_id: requestId, status: "queued" };
  });

  app.post("/api/cmd/periodic", async (request, reply) => {
    const parsed = periodicSchema.safeParse(request.body);
    if (!parsed.success) {
      return reply.code(400).send({ error: parsed.error.flatten() });
    }

    const id = normalizeCanId(parsed.data.id);
    const definition = findMessage(parsed.data.action === "stop" ? "high" : (parsed.data.bus ?? "high"), id);
    if (!definition?.injectable) {
      return reply.code(400).send({ error: `${id} is not injectable` });
    }
    if (id === "0x001" && "confirm_estop" in parsed.data && parsed.data.confirm_estop !== true) {
      return reply.code(400).send({ error: "ESTOP injection requires confirm_estop=true" });
    }

    const requestId = parsed.data.request_id ?? crypto.randomUUID();

    if (parsed.data.action === "stop") {
      const payload = { request_id: requestId, action: "stop", id };
      bridge.publishJson("etrike/debug/cmd/send/periodic", payload);
      return { request_id: requestId, status: "queued" };
    }

    let data: number[];
    try {
      data = validateDataBytes(parsed.data.data, parsed.data.dlc);
    } catch (error) {
      return reply.code(400).send({ error: String(error) });
    }

    const payload = {
      request_id: requestId,
      action: "start" as const,
      bus: definition.bus,
      id,
      dlc: parsed.data.dlc,
      data,
      interval_ms: parsed.data.interval_ms,
      count: parsed.data.count
    };

    store.insertInjection({
      request_id: requestId,
      can_id: id,
      dlc: parsed.data.dlc,
      data
    });

    bridge.publishJson("etrike/debug/cmd/send/periodic", payload);
    return { request_id: requestId, status: "queued" };
  });
}
