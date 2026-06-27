import type { FastifyInstance } from "fastify";
import type { DebugStore } from "../db/queries";
import { CAN_MESSAGES, defaultStats, normalizeBus, normalizeCanId, type CanFrame } from "../types/can";

// ── Pipeline correlation ──
interface PipelineNode {
  bus: string;
  id: string;
  name: string;
  decoded: Record<string, unknown>;
  ts: number;
}

interface PipelineChain {
  trigger: PipelineNode;
  steps: PipelineNode[];
}

const CORRELATION_WINDOW_MS = 200;
const SPEED_MATCH_TOLERANCE = 50;
const ANGLE_MATCH_TOLERANCE = 50; // 0.1° units = 5°

function correlatePipeline(frames: CanFrame[]): PipelineChain[] {
  const chains: PipelineChain[] = [];

  for (const frame of frames) {
    if (frame.bus !== "high" || frame.id !== "0x300") continue;

    const trigger: PipelineNode = {
      bus: frame.bus, id: frame.id, name: frame.name,
      decoded: frame.decoded, ts: frame.ts
    };

    const steps: PipelineNode[] = [];
    let cursor = frame.ts;

    // Look for 0x204 (RT_DRIVE_CMD) on low bus within window
    const drive = frames.find((f) =>
      f.bus === "low" && f.id === "0x204" &&
      f.ts > cursor && f.ts - cursor < CORRELATION_WINDOW_MS / 1000 &&
      Math.abs((f.decoded.motor_speed_mmps as number ?? 0) - (frame.decoded.speed_mmps as number ?? 0)) <= SPEED_MATCH_TOLERANCE
    );
    if (drive) {
      steps.push({
        bus: drive.bus, id: drive.id, name: drive.name,
        decoded: drive.decoded, ts: drive.ts
      });
      cursor = drive.ts;
    }

    // Look for 0x169 (VCU_SES_REQ) on low bus
    const steer = frames.find((f) =>
      f.bus === "low" && f.id === "0x169" &&
      f.ts > cursor && f.ts - cursor < CORRELATION_WINDOW_MS / 1000
    );
    if (steer) {
      steps.push({
        bus: steer.bus, id: steer.id, name: steer.name,
        decoded: steer.decoded, ts: steer.ts
      });
      cursor = steer.ts;
    }

    // Look for 0x201 (SES_STATUS) response
    const status = frames.find((f) =>
      f.bus === "low" && f.id === "0x201" &&
      f.ts > cursor && f.ts - cursor < CORRELATION_WINDOW_MS / 1000 &&
      (steer ? Math.abs((f.decoded.str_angle as number ?? 0) - (steer.decoded.target_angle as number ?? 0)) <= ANGLE_MATCH_TOLERANCE : true)
    );
    if (status) {
      steps.push({
        bus: status.bus, id: status.id, name: status.name,
        decoded: status.decoded, ts: status.ts
      });
    }

    if (steps.length > 0) {
      chains.push({ trigger, steps });
    }
  }

  return chains.slice(-10);
}

export function registerCanRoutes(app: FastifyInstance, store: DebugStore): void {
  app.get("/api/can/ids", async () => ({ ids: CAN_MESSAGES }));

  app.get<{
    Querystring: { bus?: string; id?: string; since?: string; limit?: string };
  }>("/api/can/frames", async (request) => {
    const bus = request.query.bus ? normalizeBus(request.query.bus) : undefined;
    const id = request.query.id ? normalizeCanId(request.query.id) : undefined;
    const since = request.query.since ? Number(request.query.since) : undefined;
    const limit = request.query.limit ? Number(request.query.limit) : undefined;

    return {
      frames: store.queryFrames({ bus, id, since, limit })
    };
  });

  app.get("/api/can/latest", async () => ({ latest: store.latestById() }));

  app.get("/api/can/stats", async () => ({
    stats: store.getStats() ?? defaultStats()
  }));

  app.get("/api/can/pipeline", async () => {
    const recent = store.queryFrames({ limit: 2000 });
    const chains = correlatePipeline(recent);
    return { chains };
  });

  app.delete("/api/can/frames", async () => {
    store.clearFrames();
    return { cleared: true };
  });
}
