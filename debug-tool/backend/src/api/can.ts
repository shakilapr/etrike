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
const PRESSURE_MATCH_TOLERANCE = 500; // kPa tolerance for brake pressure

function correlatePipeline(frames: CanFrame[]): PipelineChain[] {
  // Pre-index frames by bus:id for O(1) lookup
  const byKey: Record<string, CanFrame[]> = {};
  for (const f of frames) {
    const key = `${f.bus}:${f.id}`;
    (byKey[key] ??= []).push(f);
  }

  const driveFrames = byKey["low:0x204"] ?? [];
  const steerFrames = byKey["low:0x169"] ?? [];
  const statusFrames = byKey["low:0x201"] ?? [];
  const triggerFrames = byKey["high:0x300"] ?? [];

  // Brake pipeline frames
  const brakeCmdFrames = byKey["low:0x205"] ?? [];
  const sebReqFrames = byKey["low:0x7B9"] ?? [];
  const sebStatusFrames = byKey["low:0x721"] ?? [];
  const brakeTriggerFrames = byKey["high:0x301"] ?? [];

  const chains: PipelineChain[] = [];
  const winSec = CORRELATION_WINDOW_MS / 1000;

  // ── Steering pipeline: 0x300 -> 0x204 -> 0x169 -> 0x201 ──────────
  for (const frame of triggerFrames) {
    const trigger: PipelineNode = {
      bus: frame.bus, id: frame.id, name: frame.name,
      decoded: frame.decoded, ts: frame.ts
    };

    const steps: PipelineNode[] = [];
    let cursor = frame.ts;

    // Look for matching 0x204 (RT_DRIVE_CMD) in the window
    const drive = driveFrames.find((f) =>
      f.ts > cursor && f.ts - cursor < winSec &&
      Math.abs((f.decoded.motor_speed_mmps as number ?? 0) - (frame.decoded.speed_mmps as number ?? 0)) <= SPEED_MATCH_TOLERANCE
    );
    if (drive) {
      steps.push({ bus: drive.bus, id: drive.id, name: drive.name, decoded: drive.decoded, ts: drive.ts });
      cursor = drive.ts;
    }

    // Look for matching 0x169 (VCU_STEER_CMD)
    const steer = steerFrames.find((f) =>
      f.ts > cursor && f.ts - cursor < winSec
    );
    if (steer) {
      steps.push({ bus: steer.bus, id: steer.id, name: steer.name, decoded: steer.decoded, ts: steer.ts });
      cursor = steer.ts;
    }

    // Look for matching 0x201 (SES_STEER_STATUS)
    const status = statusFrames.find((f) =>
      f.ts > cursor && f.ts - cursor < winSec &&
      (steer ? Math.abs((f.decoded.str_angle as number ?? 0) - (steer.decoded.target_angle as number ?? 0)) <= ANGLE_MATCH_TOLERANCE : true)
    );
    if (status) {
      steps.push({ bus: status.bus, id: status.id, name: status.name, decoded: status.decoded, ts: status.ts });
    }

    if (steps.length > 0) {
      chains.push({ trigger, steps });
    }
  }

  // ── Brake pipeline: 0x301 -> 0x205 -> 0x7B9 -> 0x721 ────────────
  for (const frame of brakeTriggerFrames) {
    const trigger: PipelineNode = {
      bus: frame.bus, id: frame.id, name: frame.name,
      decoded: frame.decoded, ts: frame.ts
    };

    const steps: PipelineNode[] = [];
    let cursor = frame.ts;

    // Look for matching 0x205 (RT_BRAKE_CMD)
    const brakeCmd = brakeCmdFrames.find((f) =>
      f.ts > cursor && f.ts - cursor < winSec &&
      Math.abs((f.decoded.brake_pressure_kpa as number ?? 0) - (frame.decoded.brake_pressure_kpa as number ?? 0)) <= PRESSURE_MATCH_TOLERANCE
    );
    if (brakeCmd) {
      steps.push({ bus: brakeCmd.bus, id: brakeCmd.id, name: brakeCmd.name, decoded: brakeCmd.decoded, ts: brakeCmd.ts });
      cursor = brakeCmd.ts;
    }

    // Look for matching 0x7B9 (VCU_SEB_REQ)
    const sebReq = sebReqFrames.find((f) =>
      f.ts > cursor && f.ts - cursor < winSec
    );
    if (sebReq) {
      steps.push({ bus: sebReq.bus, id: sebReq.id, name: sebReq.name, decoded: sebReq.decoded, ts: sebReq.ts });
      cursor = sebReq.ts;
    }

    // Look for matching 0x721 (SEB_STATUS)
    const sebStatus = sebStatusFrames.find((f) =>
      f.ts > cursor && f.ts - cursor < winSec
    );
    if (sebStatus) {
      steps.push({ bus: sebStatus.bus, id: sebStatus.id, name: sebStatus.name, decoded: sebStatus.decoded, ts: sebStatus.ts });
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
