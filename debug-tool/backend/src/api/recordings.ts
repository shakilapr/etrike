import type { FastifyInstance } from "fastify";
import { z } from "zod";
import type { AppContext } from "../app-context";
import type { DebugStore } from "../db/queries";

const startRecordingSchema = z.object({
  label: z.string().max(120).optional()
});

import { Readable } from "node:stream";

export function registerRecordingRoutes(app: FastifyInstance, store: DebugStore): void {
  // Replay endpoints
  app.post("/api/replay/load", async (request, reply) => {
    const { recording_id } = request.body as any;
    try {
      await app.ctx.replayEngine.load(recording_id);
      return reply.send(app.ctx.replayEngine.getState());
    } catch (e: any) {
      return reply.code(400).send({ error: e.message });
    }
  });

  app.post("/api/replay/play", async (request, reply) => {
    const body = (request.body || {}) as any;
    try {
      app.ctx.replayEngine.play(body.speed || 1.0);
      return reply.send(app.ctx.replayEngine.getState());
    } catch (e: any) {
      return reply.code(400).send({ error: e.message });
    }
  });

  app.post("/api/replay/pause", async (request, reply) => {
    app.ctx.replayEngine.pause();
    return reply.send(app.ctx.replayEngine.getState());
  });

  app.post("/api/replay/stop", async (request, reply) => {
    app.ctx.replayEngine.stop();
    return reply.send(app.ctx.replayEngine.getState());
  });

  app.post("/api/replay/seek", async (request, reply) => {
    const { time_us } = request.body as any;
    await app.ctx.replayEngine.seek(time_us);
    return reply.send(app.ctx.replayEngine.getState());
  });

  // Export endpoint
  app.get<{ Params: { id: string }; Querystring: { format?: string } }>(
    "/api/recordings/:id/export",
    async (request, reply) => {
      const id = Number(request.params.id);
      const format = request.query.format === "csv" ? "csv" : "json";
      const iter = store.recordingFramesIterator?.(id) || null;
      if (!iter) {
        return reply.code(404).send({ error: "recording not found" });
      }

      if (format === "csv") {
        reply.header("Content-Disposition", `attachment; filename="recording-${id}.csv"`);
        reply.type("text/csv");
      } else {
        reply.header("Content-Disposition", `attachment; filename="recording-${id}.json"`);
        reply.type("application/json");
      }

      const stream = new Readable({
        read() {}
      });

      let first = true;
      if (format === "csv") {
        stream.push("ts_real,ts_us,ts_device,bus,can_id,can_name,dlc,data\n");
      } else {
        stream.push("[\n");
      }

      // We read from the synchronous sqlite iterator asynchronously using setImmediate 
      // to avoid blocking the event loop on huge exports.
      const produce = async () => {
        let count = 0;
        while (count < 100) {
          const { value, done } = await iter.next();
          if (done) {
            if (format === "json") stream.push("\n]\n");
            stream.push(null);
            return;
          }

          let chunk = "";
          if (format === "csv") {
            const dataHex = Buffer.from(value.frame.data).toString("hex");
            chunk = `${value.ts_real},${value.ts_us},${value.ts_device},${value.bus},${value.frame.id},${value.decoded?.name || ""},${value.frame.dlc},${dataHex}\n`;
          } else {
            chunk = (first ? "" : ",\n") + JSON.stringify(value);
            first = false;
          }
          
          const keepGoing = stream.push(chunk);
          count++;
          if (!keepGoing) {
            // Backpressure
            stream.once("drain", produce);
            return;
          }
        }
        setImmediate(produce);
      };

      setImmediate(produce);
      return reply.send(stream);
    }
  );

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
