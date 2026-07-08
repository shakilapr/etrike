import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { DebugStore } from "../db/queries";

const startRecordingSchema = z.object({
  label: z.string().max(120).optional()
});

export function registerRecordingRoutes(app: FastifyInstance, store: DebugStore): void {
  app.get("/api/recordings", async () => ({ recordings: await store.listRecordings() }));

  app.post("/api/recordings", async (request, reply) => {
    const parsed = startRecordingSchema.safeParse(request.body ?? {});
    if (!parsed.success) {
      return reply.code(400).send({ error: parsed.error.flatten() });
    }
    return { recording: await store.startRecording(parsed.data.label) };
  });

  app.put<{ Params: { id: string } }>("/api/recordings/:id/stop", async (request, reply) => {
    const id = Number(request.params.id);
    const existing = await store.getRecording(id);
    if (!existing) {
      return reply.code(404).send({ error: "recording not found" });
    }
    if (existing.stopped_at !== null) {
      return reply.code(409).send({ error: "recording already stopped" });
    }

    const recording = await store.stopRecording(id);
    if (!recording) {
      return reply.code(409).send({ error: "recording already stopped" });
    }
    return { recording };
  });

  app.get<{ Params: { id: string }; Querystring: { limit?: string } }>(
    "/api/recordings/:id/frames",
    async (request, reply) => {
      const id = Number(request.params.id);
      const limit = request.query.limit ? Number(request.query.limit) : undefined;
      const frames = await store.recordingFramesById(id, limit);
      if (!frames) {
        return reply.code(404).send({ error: "recording not found" });
      }
      return { frames };
    }
  );

  app.delete<{ Params: { id: string } }>("/api/recordings/:id", async (request, reply) => {
    const id = Number(request.params.id);
    if (!(await store.deleteRecording(id))) {
      return reply.code(404).send({ error: "recording not found" });
    }
    return { ok: true };
  });
}
