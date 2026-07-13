import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { LeaseManager, LeaseResource } from "../state/leases";

const resourceSchema = z.enum(["steer", "motor", "brake", "sys"]);

const leaseSchema = z.object({
  resource: resourceSchema,
  ownerId: z.string().min(1),
  ttlMs: z.number().int().min(1000).max(60000).optional()
});

export function registerLeaseRoutes(
  app: FastifyInstance,
  leaseManager: LeaseManager
): void {
  app.get("/api/leases", async () => ({
    leases: leaseManager.list()
  }));

  app.post("/api/leases/acquire", async (request, reply) => {
    const parsed = leaseSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const success = leaseManager.acquire(parsed.data.resource, parsed.data.ownerId, parsed.data.ttlMs);
    if (!success) {
      return reply.code(409).send({ error: `Resource ${parsed.data.resource} is already leased to someone else` });
    }
    return { ok: true, action: "acquired" };
  });

  app.post("/api/leases/renew", async (request, reply) => {
    const parsed = leaseSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const success = leaseManager.renew(parsed.data.resource, parsed.data.ownerId, parsed.data.ttlMs);
    if (!success) {
      return reply.code(403).send({ error: `Cannot renew lease for ${parsed.data.resource} (not held or expired)` });
    }
    return { ok: true, action: "renewed" };
  });

  app.post("/api/leases/release", async (request, reply) => {
    const parsed = leaseSchema.safeParse(request.body);
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    leaseManager.release(parsed.data.resource, parsed.data.ownerId);
    return { ok: true, action: "released" };
  });
}
