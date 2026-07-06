import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { DebugStore } from "../db/queries";
import { normalizeCanId, normalizeBus, normalizeFrame } from "../types/can";
import type { StreamHub } from "../ws/stream";

const injectSchema = z.object({
  bus: z.enum(["high", "low"]),
  id: z.string().min(1),
  dlc: z.number().int().min(0).max(8),
  data: z.array(z.number().int().min(0).max(255)),
});

export function registerSimRoutes(app: FastifyInstance, store: DebugStore, hub: StreamHub): void {
  // One-shot injection
  app.post("/api/sim/inject", async (request, reply) => {
    const parsed = injectSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const bus = normalizeBus(parsed.data.bus);
    const id = normalizeCanId(parsed.data.id);
    const data = parsed.data.data.slice(0, parsed.data.dlc);

    const frame = normalizeFrame({ bus, id, data, dlc: parsed.data.dlc });
    store.insertFrame(frame, "emulated");
    hub.broadcast({ type: "can_frame", payload: frame });

    return { ok: true, id, bus };
  });

  // Periodic start
  app.post("/api/sim/periodic/start", async (request, reply) => {
    const parsed = injectSchema.extend({
      interval_ms: z.number().int().min(1).max(10000),
    }).safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const { bus: rawBus, id: rawId, dlc, data, interval_ms } = parsed.data;
    const bus = normalizeBus(rawBus);
    const id = normalizeCanId(rawId);
    const key = `sim:${bus}:${id}`;
    const timers: Map<string, ReturnType<typeof setInterval>> = (app as any).__simTimers;
    if (!timers) return reply.code(500).send({ error: "sim subsystem not initialized" });

    if (timers.has(key)) clearInterval(timers.get(key)!);

    timers.set(key, setInterval(() => {
      const frame = normalizeFrame({ bus, id, data: [...data], dlc });
      store.insertFrame(frame, "emulated");
      hub.broadcast({ type: "can_frame", payload: frame });
    }, interval_ms));

    return { ok: true, action: "start", id, bus, interval_ms };
  });

  // Periodic stop
  app.post("/api/sim/periodic/stop", async (request, reply) => {
    const body = (request.body ?? {}) as Record<string, unknown>;
    const bus = normalizeBus(String(body.bus ?? "high"));
    const id = normalizeCanId(String(body.id ?? "0x000"));
    const key = `sim:${bus}:${id}`;
    const timers: Map<string, ReturnType<typeof setInterval>> = (app as any).__simTimers;
    if (timers?.has(key)) { clearInterval(timers.get(key)!); timers.delete(key); }
    return { ok: true, action: "stop", id, bus };
  });
}
