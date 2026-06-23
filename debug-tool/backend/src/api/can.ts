import type { FastifyInstance } from "fastify";
import type { DebugStore } from "../db/queries";
import { CAN_MESSAGES, defaultStats, normalizeCanId } from "../types/can";

export function registerCanRoutes(app: FastifyInstance, store: DebugStore): void {
  app.get("/api/can/ids", async () => ({ ids: CAN_MESSAGES }));

  app.get<{
    Querystring: { id?: string; since?: string; limit?: string };
  }>("/api/can/frames", async (request) => {
    const id = request.query.id ? normalizeCanId(request.query.id) : undefined;
    const since = request.query.since ? Number(request.query.since) : undefined;
    const limit = request.query.limit ? Number(request.query.limit) : undefined;

    return {
      frames: store.queryFrames({ id, since, limit })
    };
  });

  app.get("/api/can/latest", async () => ({ latest: store.latestById() }));

  app.get("/api/can/stats", async () => ({
    stats: store.getStats() ?? defaultStats()
  }));
}
