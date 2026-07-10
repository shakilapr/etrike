import { ID_RT_DRIVE_CMD, ID_VCU_SES_REQ, ID_SES_STATUS, ID_HOST_DRIVE_CMD, ID_RT_BRAKE_CMD, ID_VCU_SEB_REQ, ID_SEB_STATUS, ID_HOST_BRAKE_REQ } from "@etrike/debug-shared";
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

let cachedChains: PipelineChain[] | null = null;
let cacheInvalidated = false;

const CORRELATION_WINDOW_MS = 200;
const SPEED_MATCH_TOLERANCE = 50;
const ANGLE_MATCH_TOLERANCE = 50; // 0.1° units = 5°
const PRESSURE_MATCH_TOLERANCE = 500; // kPa tolerance for brake pressure

type FramePredicate = (frame: CanFrame) => boolean;

function lowerBoundByTs(frames: CanFrame[], ts: number): number {
  let lo = 0;
  let hi = frames.length;
  while (lo < hi) {
    const mid = Math.floor((lo + hi) / 2);
    if (frames[mid].ts <= ts) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

function firstMatchingAfter(frames: CanFrame[], cursor: number, winSec: number, predicate: FramePredicate = () => true): CanFrame | undefined {
  for (let i = lowerBoundByTs(frames, cursor); i < frames.length; i++) {
    const frame = frames[i];
    if (frame.ts - cursor >= winSec) return undefined;
    if (predicate(frame)) return frame;
  }
  return undefined;
}

export function correlatePipeline(frames: CanFrame[]): PipelineChain[] {
  // Pre-index frames by bus:id, sorted oldest-to-newest for binary search.
  const byKey: Record<string, CanFrame[]> = {};
  for (const f of frames) {
    const key = `${f.bus}:${f.id}`;
    (byKey[key] ??= []).push(f);
  }
  for (const list of Object.values(byKey)) {
    list.sort((a, b) => a.ts - b.ts);
  }

  const driveFrames = byKey[`low:${ID_RT_DRIVE_CMD}`] ?? [];
  const steerFrames = byKey[`low:${ID_VCU_SES_REQ}`] ?? [];
  const statusFrames = byKey[`low:${ID_SES_STATUS}`] ?? [];
  const triggerFrames = byKey[`high:${ID_HOST_DRIVE_CMD}`] ?? [];

  // Brake pipeline frames
  const brakeCmdFrames = byKey[`low:${ID_RT_BRAKE_CMD}`] ?? [];
  const sebReqFrames = byKey[`low:${ID_VCU_SEB_REQ}`] ?? [];
  const sebStatusFrames = byKey[`low:${ID_SEB_STATUS}`] ?? [];
  const brakeTriggerFrames = byKey[`high:${ID_HOST_BRAKE_REQ}`] ?? [];

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
    const drive = firstMatchingAfter(driveFrames, cursor, winSec, (f) =>
      Math.abs(((f.decoded.motor_speed_mmps as number) ?? 0) - ((frame.decoded.speed_mmps as number) ?? 0)) <= SPEED_MATCH_TOLERANCE
    );
    if (drive) {
      steps.push({ bus: drive.bus, id: drive.id, name: drive.name, decoded: drive.decoded, ts: drive.ts });
      cursor = drive.ts;
    }

    // Look for matching 0x169 (VCU_STEER_CMD)
    const steer = firstMatchingAfter(steerFrames, cursor, winSec);
    if (steer) {
      steps.push({ bus: steer.bus, id: steer.id, name: steer.name, decoded: steer.decoded, ts: steer.ts });
      cursor = steer.ts;
    }

    // Look for matching 0x201 (SES_STEER_STATUS)
    const status = firstMatchingAfter(statusFrames, cursor, winSec, (f) =>
      steer ? Math.abs(((f.decoded.str_angle as number) ?? 0) - ((steer.decoded.target_angle as number) ?? 0)) <= ANGLE_MATCH_TOLERANCE : true
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
    const brakeCmd = firstMatchingAfter(brakeCmdFrames, cursor, winSec, (f) =>
      Math.abs(((f.decoded.brake_pressure_kpa as number) ?? 0) - ((frame.decoded.brake_pressure_kpa as number) ?? 0)) <= PRESSURE_MATCH_TOLERANCE
    );
    if (brakeCmd) {
      steps.push({ bus: brakeCmd.bus, id: brakeCmd.id, name: brakeCmd.name, decoded: brakeCmd.decoded, ts: brakeCmd.ts });
      cursor = brakeCmd.ts;
    }

    // Look for matching 0x7B9 (VCU_SEB_REQ)
    const sebReq = firstMatchingAfter(sebReqFrames, cursor, winSec);
    if (sebReq) {
      steps.push({ bus: sebReq.bus, id: sebReq.id, name: sebReq.name, decoded: sebReq.decoded, ts: sebReq.ts });
      cursor = sebReq.ts;
    }

    // Look for matching 0x721 (SEB_STATUS)
    const sebStatus = firstMatchingAfter(sebStatusFrames, cursor, winSec);
    if (sebStatus) {
      steps.push({ bus: sebStatus.bus, id: sebStatus.id, name: sebStatus.name, decoded: sebStatus.decoded, ts: sebStatus.ts });
    }

    if (steps.length > 0) {
      chains.push({ trigger, steps });
    }
  }

  return chains.slice(-10);
}

import type { WriteQueue } from "../db/write-queue";

export function registerCanRoutes(app: FastifyInstance, store: DebugStore, writeQueue: WriteQueue): void {
  // Setup cache invalidation on write flush
  writeQueue.onFlush = () => {
    cacheInvalidated = true;
  };

  app.get("/api/can/ids", async () => ({ ids: CAN_MESSAGES }));

  app.get<{
    Querystring: { bus?: string; id?: string; since?: string; limit?: string };
  }>("/api/can/frames", async (request, reply) => {
    let bus: ReturnType<typeof normalizeBus> | undefined;
    try {
      bus = request.query.bus ? normalizeBus(request.query.bus) : undefined;
    } catch (error) {
      return reply.code(400).send({ error: error instanceof Error ? error.message : String(error) });
    }
    const id = request.query.id ? normalizeCanId(request.query.id) : undefined;
    const since = request.query.since ? Number(request.query.since) : undefined;
    const limit = request.query.limit ? Number(request.query.limit) : undefined;

    return {
      frames: await store.queryFrames({ bus, id, since, limit })
    };
  });

  app.get("/api/can/latest", async () => ({ latest: await store.latestById() }));

  app.get("/api/can/stats", async () => ({
    stats: (await store.getStats()) ?? defaultStats()
  }));

  app.get("/api/can/pipeline", async () => {
    if (cachedChains && !cacheInvalidated) {
      return { chains: cachedChains };
    }
    
    const recent = await store.queryFrames({ limit: 2000 });
    cachedChains = correlatePipeline(recent);
    cacheInvalidated = false;
    
    return { chains: cachedChains };
  });

  app.delete("/api/can/frames", async () => {
    await store.clearFrames();
    return { cleared: true };
  });
}
