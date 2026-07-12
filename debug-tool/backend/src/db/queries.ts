import Database from "better-sqlite3";
import { mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { SQLITE_SCHEMA } from "./schema";
import type { Bus, CanFrame, CanStats } from "../types/can";
import { defaultStats, normalizeBus, normalizeStats } from "../types/can";
import type { FrameRouter, FrameSource } from "../sim/router";

export interface StoredCanFrame extends CanFrame {
  row_id: number;
  ts_us: string;
  seq: number;
  ts_real: number;
  ts_device: number;
}

export interface InjectedFrame {
  row_id: number;
  ts_us: string;
  seq: number;
  ts_real: number;
  bus: Bus;
  can_id: string;
  dlc: number;
  data: number[];
  status: string | null;
}

export interface Recording {
  id: number;
  label: string | null;
  started_at: number;
  stopped_at: number | null;
  frame_count: number;
}

export interface FrameQuery {
  bus?: Bus;
  id?: string;
  since?: number;
  limit?: number;
}

interface FrameRow {
  id: number;
  ts_us: bigint;
  seq: number;
  ts_real: number;
  ts_device: number;
  bus: Bus;
  can_id: string;
  can_name: string;
  dlc: number;
  data: Buffer;
  decoded: string;
}

interface InjectionRow {
  id: number;
  ts_us: bigint;
  seq: number;
  ts_real: number;
  bus: Bus;
  can_id: string;
  dlc: number;
  data: Buffer;
  status: string | null;
}

export interface DebugStore {
  init?(): Promise<void>;
  insertFrame?(frame: CanFrame, source?: FrameSource): void;
  insertFrames(batch: Array<{ frame: CanFrame; source: FrameSource }>): void;
  queryFrames(query?: FrameQuery): Promise<StoredCanFrame[]>;
  latestById(): Promise<Record<string, StoredCanFrame>>;
  setStats(stats: CanStats): void;
  getStatsUpdatedAt(): Promise<number | null>;
  getStats(): Promise<CanStats>;
  insertInjection(input: any): Promise<InjectedFrame>;
  updateInjectionByCorrelation(correlationId: string, status: string): Promise<InjectedFrame | null>;
  updateLatestInjectionStatus(status: string): Promise<InjectedFrame | null>;
  listInjections(limit?: number): Promise<InjectedFrame[]>;
  listRecordings(): Promise<Recording[]>;
  getRecording(id: number): Promise<Recording | null>;
  startRecording(label?: string): Promise<Recording>;
  stopRecording(id: number): Promise<Recording | null>;
  deleteRecording(id: number): Promise<boolean>;
  recordingFramesById(id: number, limit?: number): Promise<StoredCanFrame[] | null>;
  recordingFramesIterator(id: number): IterableIterator<StoredCanFrame> | null;
  recentFramesIterator(limit?: number): IterableIterator<StoredCanFrame>;
  counts(): Promise<{ frames: number; injected: number; recordings: number }>;
  clearFrames(): Promise<void>;
  close(): Promise<void>;
  runMaintenance(): Promise<void>;
}

export class DebugStoreImpl {
  private readonly db: Database.Database;
  private walTimer: ReturnType<typeof setInterval> | null = null;
  private maintenanceTimer: ReturnType<typeof setInterval> | null = null;
  private maintenanceRunning = false;
  private activeRecordingIds = new Set<number>();
  private activeRecordingsLoaded = false;

  private static readonly MAINTENANCE_INTERVAL_MS = 5000;
  private static readonly STOPPED_RECORDING_RETENTION = 10;

  // Cached prepared statements
  private readonly stmtInsertFrame: Database.Statement;
  private readonly stmtActiveRecordings: Database.Statement;
  private readonly stmtInsertRecordingFrame: Database.Statement;
  private readonly stmtUpdateRecordingCount: Database.Statement;
  private readonly stmtLatestById: Database.Statement;
  private readonly stmtSetStats: Database.Statement;
  private readonly stmtSetStatsTs: Database.Statement;
  private readonly stmtGetStatsAll: Database.Statement;
  private readonly stmtInsertInjection: Database.Statement;
  private readonly stmtUpdateInjectionCorr: Database.Statement;
  private readonly stmtSelectInjectionCorr: Database.Statement;
  private readonly stmtSelectInjectionId: Database.Statement;
  private readonly stmtSelectLatestInjection: Database.Statement;
  private readonly stmtListInjections: Database.Statement;
  private readonly stmtListRecordings: Database.Statement;
  private readonly stmtGetRecording: Database.Statement;
  private readonly stmtStartRecording: Database.Statement;
  private readonly stmtStopRecordingUpdateTs: Database.Statement;
  private readonly stmtStopRecordingUpdateCount: Database.Statement;
  private readonly stmtDeleteRecordingFrames: Database.Statement;
  private readonly stmtDeleteRecording: Database.Statement;
  private readonly stmtCheckRecording: Database.Statement;
  private readonly stmtRecordingFrames: Database.Statement;
  private readonly stmtCountFrames: Database.Statement;
  private readonly stmtCountInjected: Database.Statement;
  private readonly stmtCountRecordings: Database.Statement;
  private readonly stmtPruneFramesIds: Database.Statement;
  private readonly stmtPruneFramesDelete: Database.Statement;
  private readonly stmtPruneStoppedRecordingsIds: Database.Statement;

  constructor(dbPath: string, private readonly maxFrames = 50000) {
    const filename = dbPath === ":memory:" ? dbPath : resolve(dbPath);
    if (filename !== ":memory:") mkdirSync(dirname(filename), { recursive: true });
    this.db = new Database(filename);
    this.db.pragma("journal_mode = WAL");
    this.db.pragma("foreign_keys = ON");
    this.db.exec(SQLITE_SCHEMA);

    // Prepare all statements
    this.stmtInsertFrame = this.db.prepare(
      `INSERT INTO can_frames (ts_real, ts_us, seq, ts_device, bus, can_id, can_name, dlc, data, decoded) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
    );
    this.stmtActiveRecordings = this.db.prepare(`SELECT id FROM recordings WHERE stopped_at IS NULL`);
    this.stmtInsertRecordingFrame = this.db.prepare(`INSERT OR IGNORE INTO recording_frames (recording_id, frame_id) VALUES (?, ?)`);
    this.stmtUpdateRecordingCount = this.db.prepare(`UPDATE recordings SET frame_count = frame_count + ? WHERE id = ?`);
    this.stmtLatestById = this.db.prepare(
      `SELECT * FROM can_frames WHERE id IN (SELECT MAX(id) FROM can_frames GROUP BY bus, can_id)`
    );
    this.stmtSetStats = this.db.prepare(
      `INSERT INTO runtime_state (key, value) VALUES ('stats', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value`
    );
    this.stmtSetStatsTs = this.db.prepare(
      `INSERT INTO runtime_state (key, value) VALUES ('stats_updated_at', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value`
    );
    this.stmtGetStatsAll = this.db.prepare(`SELECT key, value FROM runtime_state WHERE key IN ('stats', 'stats_updated_at')`);
    this.stmtInsertInjection = this.db.prepare(
      `INSERT INTO injected_frames (ts_real, ts_us, seq, bus, can_id, dlc, data, status, correlation_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
    );
    this.stmtUpdateInjectionCorr = this.db.prepare(`UPDATE injected_frames SET status = ? WHERE id = ?`);
    this.stmtSelectInjectionCorr = this.db.prepare(`SELECT id FROM injected_frames WHERE correlation_id = ?`);
    this.stmtSelectInjectionId = this.db.prepare(`SELECT * FROM injected_frames WHERE id = ?`);
    this.stmtSelectLatestInjection = this.db.prepare(`SELECT id FROM injected_frames ORDER BY id DESC LIMIT 1`);
    this.stmtListInjections = this.db.prepare(`SELECT * FROM injected_frames ORDER BY ts_real DESC, id DESC LIMIT ?`);
    this.stmtListRecordings = this.db.prepare(`SELECT * FROM recordings ORDER BY started_at DESC`);
    this.stmtGetRecording = this.db.prepare(`SELECT * FROM recordings WHERE id = ?`);
    this.stmtStartRecording = this.db.prepare(`INSERT INTO recordings (label, started_at, stopped_at, frame_count) VALUES (?, ?, NULL, 0)`);
    this.stmtStopRecordingUpdateTs = this.db.prepare(`UPDATE recordings SET stopped_at = ? WHERE id = ? AND stopped_at IS NULL`);
    this.stmtStopRecordingUpdateCount = this.db.prepare(
      `UPDATE recordings SET frame_count = (SELECT COUNT(*) FROM recording_frames WHERE recording_id = ?) WHERE id = ?`
    );
    this.stmtDeleteRecordingFrames = this.db.prepare(`DELETE FROM recording_frames WHERE recording_id = ?`);
    this.stmtDeleteRecording = this.db.prepare(`DELETE FROM recordings WHERE id = ?`);
    this.stmtCheckRecording = this.db.prepare(`SELECT id FROM recordings WHERE id = ?`);
    this.stmtRecordingFrames = this.db.prepare(
      `SELECT f.* FROM recording_frames rf JOIN can_frames f ON f.id = rf.frame_id WHERE rf.recording_id = ? ORDER BY f.ts_real DESC, f.id DESC LIMIT ?`
    );
    this.stmtCountFrames = this.db.prepare(`SELECT COUNT(*) AS n FROM can_frames`);
    this.stmtCountInjected = this.db.prepare(`SELECT COUNT(*) AS n FROM injected_frames`);
    this.stmtCountRecordings = this.db.prepare(`SELECT COUNT(*) AS n FROM recordings`);
    this.stmtPruneFramesIds = this.db.prepare(
      `SELECT f.id FROM can_frames f WHERE NOT EXISTS (SELECT 1 FROM recording_frames rf WHERE rf.frame_id = f.id) ORDER BY f.id ASC LIMIT ?`
    );
    this.stmtPruneFramesDelete = this.db.prepare(`DELETE FROM can_frames WHERE id = ?`);
    this.stmtPruneStoppedRecordingsIds = this.db.prepare(
      `SELECT id FROM recordings WHERE stopped_at IS NOT NULL ORDER BY stopped_at DESC, id DESC LIMIT -1 OFFSET ?`
    );

    // Periodic WAL checkpoint to prevent unbounded WAL growth
    this.walTimer = setInterval(() => {
      this.db.pragma("wal_checkpoint(TRUNCATE)");
    }, 30000).unref();
    this.maintenanceTimer = setInterval(() => {
      this.runMaintenance();
    }, DebugStoreImpl.MAINTENANCE_INTERVAL_MS).unref();
  }

  /**
   * Insert a CAN frame (legacy individual insert).
   * Prefer insertFrames() for bulk ingestion.
   */
  insertFrame(frame: CanFrame, source: FrameSource = "physical"): StoredCanFrame {
    const res = this.insertFrames([{ frame, source }]);
    return res[0] ?? { ...frame, row_id: -1, ts_real: Date.now() / 1000, ts_device: Math.round(frame.ts ?? 0), ts_us: frame.ts_us, seq: frame.seq };
  }

  /**
   * Insert a batch of CAN frames in a single transaction.
   * This is the optimized hot-path for ingestion.
   */
  insertFrames(batch: Array<{ frame: CanFrame; source: FrameSource }>): StoredCanFrame[] {
    const results: StoredCanFrame[] = [];
    const insertedIds: number[] = [];

    // Load active recordings into memory once if not loaded
    if (!this.activeRecordingsLoaded) {
      const active = this.stmtActiveRecordings.all() as Array<{ id: number }>;
      for (const row of active) this.activeRecordingIds.add(row.id);
      this.activeRecordingsLoaded = true;
    }

    this.db.transaction(() => {
      for (const item of batch) {
        let frame = item.frame;

        const tsReal = Date.now() / 1000;
        const tsDevice = Math.round(frame.ts ?? 0);
        const tsUs = frame.ts_us ? BigInt(frame.ts_us) : BigInt(Math.floor(tsReal * 1_000_000));
        const seq = frame.seq ?? 0;
        
        try {
          const res = this.stmtInsertFrame.run(
            tsReal,
            tsUs,
            seq,
            tsDevice,
            frame.bus,
            frame.frame.id,
            frame.decoded?.name ?? "",
            frame.frame.dlc,
            Buffer.from(frame.frame.data),
            JSON.stringify(frame.decoded?.signals)
          );
          const rowId = Number(res.lastInsertRowid);
          insertedIds.push(rowId);
          results.push({ ...frame, row_id: rowId, ts_real: tsReal, ts_device: tsDevice, ts_us: tsUs.toString(), seq });
        } catch (err) {
          console.error("insertFrame failed:", String(err));
          results.push({ ...frame, row_id: -1, ts_real: tsReal, ts_device: tsDevice, ts_us: tsUs.toString(), seq });
        }
      }

      // Batch attach to recordings
      if (this.activeRecordingIds.size > 0 && insertedIds.length > 0) {
        for (const recId of this.activeRecordingIds) {
          for (const rowId of insertedIds) {
            this.stmtInsertRecordingFrame.run(recId, rowId);
          }
          this.stmtUpdateRecordingCount.run(insertedIds.length, recId);
        }
      }
    })();

    return results;
  }

  queryFrames(query: FrameQuery = {}): StoredCanFrame[] {
    const limit = Math.min(Math.max(query.limit ?? 500, 1), 5000);
    const clauses: string[] = [];
    const params: Array<string | number> = [];
    if (query.bus) {
      clauses.push("bus = ?");
      params.push(query.bus);
    }
    if (query.id) {
      clauses.push("can_id = ?");
      params.push(query.id);
    }
    if (typeof query.since === "number" && Number.isFinite(query.since)) {
      clauses.push("ts_real >= ?");
      params.push(query.since);
    }
    const where = clauses.length > 0 ? `WHERE ${clauses.join(" AND ")}` : "";
    const rows = this.db.prepare(`SELECT * FROM can_frames ${where} ORDER BY ts_real DESC, id DESC LIMIT ?`).all(...params, limit) as FrameRow[];
    return rows.map(rowToFrame);
  }

  latestById(): Record<string, StoredCanFrame> {
    const rows = this.stmtLatestById.all() as FrameRow[];
    const latest: Record<string, StoredCanFrame> = {};
    for (const row of rows) {
      latest[`${row.bus}:${row.can_id}`] = rowToFrame(row);
    }
    return latest;
  }

  setStats(stats: CanStats): void {
    const normalized = normalizeStats(stats);
    const now = Date.now() / 1000;
    this.db.transaction(() => {
      this.stmtSetStats.run(JSON.stringify(normalized));
      this.stmtSetStatsTs.run(String(now));
    })();
  }

  getStatsUpdatedAt(): number | null {
    const rows = this.stmtGetStatsAll.all() as Array<{ key: string; value: string }>;
    const tsRow = rows.find((r) => r.key === 'stats_updated_at');
    if (!tsRow) return null;
    const n = Number(tsRow.value);
    return Number.isFinite(n) ? n : null;
  }

  getStats(): CanStats {
    const rows = this.stmtGetStatsAll.all() as Array<{ key: string; value: string }>;
    const statsRow = rows.find((r) => r.key === 'stats');
    const tsRow = rows.find((r) => r.key === 'stats_updated_at');
    
    if (!statsRow) return defaultStats();
    if (tsRow) {
      const updatedAt = Number(tsRow.value);
      if (!Number.isFinite(updatedAt) || Date.now() / 1000 - updatedAt > 5) {
        return defaultStats();
      }
    }
    try {
      return normalizeStats(JSON.parse(statsRow.value) as CanStats);
    } catch {
      return defaultStats();
    }
  }

  insertInjection(input: Omit<InjectedFrame, "row_id" | "ts_real" | "status" | "ts_us" | "seq"> & { status?: string; correlation_id?: string; ts_us?: string; seq?: number }): InjectedFrame {
    const tsReal = Date.now() / 1000;
    const tsUs = input.ts_us ?? (BigInt(Date.now()) * 1000n).toString();
    const seq = input.seq ?? 0;
    const status = input.status ?? "queued";
    const correlationId = input.correlation_id ?? null;
    const result = this.stmtInsertInjection.run(tsReal, BigInt(tsUs), seq, input.bus, input.can_id, input.dlc, Buffer.from(input.data), status, correlationId);
    return { row_id: Number(result.lastInsertRowid), ts_real: tsReal, ts_us: tsUs, seq: seq, bus: input.bus, can_id: input.can_id, dlc: input.dlc, data: input.data, status };
  }

  updateInjectionByCorrelation(correlationId: string, status: string): InjectedFrame | null {
    const row = this.stmtSelectInjectionCorr.get(correlationId) as { id: number } | undefined;
    if (!row) return this.updateLatestInjectionStatus(status);
    this.stmtUpdateInjectionCorr.run(status, row.id);
    const updated = this.stmtSelectInjectionId.get(row.id) as InjectionRow;
    return rowToInjection(updated);
  }

  updateLatestInjectionStatus(status: string): InjectedFrame | null {
    const row = this.stmtSelectLatestInjection.get() as { id: number } | undefined;
    if (!row) return null;
    this.stmtUpdateInjectionCorr.run(status, row.id);
    const updated = this.stmtSelectInjectionId.get(row.id) as InjectionRow;
    return rowToInjection(updated);
  }

  listInjections(limit = 50): InjectedFrame[] {
    return (this.stmtListInjections.all(limit) as InjectionRow[]).map(rowToInjection);
  }

  listRecordings(): Recording[] {
    return this.stmtListRecordings.all() as Recording[];
  }

  getRecording(id: number): Recording | null {
    return (this.stmtGetRecording.get(id) as Recording | undefined) ?? null;
  }

  startRecording(label?: string): Recording {
    const startedAt = Date.now() / 1000;
    const cleanLabel = label?.trim() || null;
    const result = this.stmtStartRecording.run(cleanLabel, startedAt);
    const id = Number(result.lastInsertRowid);
    this.activeRecordingIds.add(id);
    return { id, label: cleanLabel, started_at: startedAt, stopped_at: null, frame_count: 0 };
  }

  stopRecording(id: number): Recording | null {
    const existing = this.getRecording(id);
    if (!existing || existing.stopped_at !== null) return null;

    this.activeRecordingIds.delete(id);
    this.db.transaction(() => {
      this.stmtStopRecordingUpdateTs.run(Date.now() / 1000, id);
      this.stmtStopRecordingUpdateCount.run(id, id);
    })();
    return this.getRecording(id);
  }

  deleteRecording(id: number): boolean {
    this.activeRecordingIds.delete(id);
    let changes = 0;
    this.db.transaction(() => {
      this.stmtDeleteRecordingFrames.run(id);
      changes = this.stmtDeleteRecording.run(id).changes;
    })();
    return changes > 0;
  }

  recordingFramesById(id: number, limit = 1000): StoredCanFrame[] | null {
    if (!this.stmtCheckRecording.get(id)) return null;
    const rows = this.stmtRecordingFrames.all(id, limit) as FrameRow[];
    return rows.map(rowToFrame);
  }

  recentFramesIterator(limit = 10000): IterableIterator<StoredCanFrame> {
    const stmt = this.db.prepare(
      `SELECT * FROM can_frames ORDER BY ts_real DESC, id DESC LIMIT ?`
    );
    const iterator = stmt.iterate(limit) as IterableIterator<FrameRow>;
    return (function* () {
      for (const row of iterator) {
        yield rowToFrame(row);
      }
    })();
  }

  recordingFramesIterator(id: number): IterableIterator<StoredCanFrame> | null {
    if (!this.stmtCheckRecording.get(id)) return null;
    const stmt = this.db.prepare(
      `SELECT f.* FROM recording_frames rf JOIN can_frames f ON f.id = rf.frame_id WHERE rf.recording_id = ? ORDER BY f.ts_real ASC, f.id ASC`
    );
    const iterator = stmt.iterate(id) as IterableIterator<FrameRow>;
    return (function* () {
      for (const row of iterator) {
        yield rowToFrame(row);
      }
    })();
  }

  counts(): { frames: number; injected: number; recordings: number } {
    return {
      frames: (this.stmtCountFrames.get() as { n: number }).n,
      injected: (this.stmtCountInjected.get() as { n: number }).n,
      recordings: (this.stmtCountRecordings.get() as { n: number }).n
    };
  }

  close(): void {
    if (this.walTimer) { clearInterval(this.walTimer); this.walTimer = null; }
    if (this.maintenanceTimer) { clearInterval(this.maintenanceTimer); this.maintenanceTimer = null; }
    this.db.close();
  }

  runMaintenance(): void {
    if (this.maintenanceRunning) return;
    this.maintenanceRunning = true;
    try {
      this.pruneStoppedRecordings();
      this.pruneFrames();
    } finally {
      this.maintenanceRunning = false;
    }
  }

  private pruneFrames(): void {
    const count = (this.stmtCountFrames.get() as { n: number }).n;
    if (count <= this.maxFrames) return;
    const ids = this.stmtPruneFramesIds.all(count - this.maxFrames) as Array<{ id: number }>;
    
    // Use transaction with individual deletes to avoid variable length IN limits, 
    // or batch into chunks. Doing it individually in a single transaction is still O(1) transaction overhead.
    this.db.transaction(() => {
      for (const row of ids) {
        this.stmtPruneFramesDelete.run(row.id);
      }
    })();
  }

  private pruneStoppedRecordings(): void {
    const oldStopped = this.stmtPruneStoppedRecordingsIds.all(DebugStoreImpl.STOPPED_RECORDING_RETENTION) as Array<{ id: number }>;
    if (oldStopped.length === 0) return;

    this.db.transaction(() => {
      for (const row of oldStopped) {
        this.activeRecordingIds.delete(row.id);
        this.stmtDeleteRecordingFrames.run(row.id);
        this.stmtDeleteRecording.run(row.id);
      }
    })();
  }

  clearFrames(): void {
    this.db.exec("DELETE FROM recording_frames; DELETE FROM can_frames; UPDATE recordings SET frame_count = 0;");
  }
}

function rowToFrame(row: FrameRow): StoredCanFrame {
  return {
    row_id: row.id,
    ts_real: row.ts_real,
    ts_us: row.ts_us != null ? row.ts_us.toString() : "",
    seq: row.seq != null ? row.seq : 0,
    ts_device: row.ts_device,
    ts: row.ts_device,
    bus: normalizeBus(row.bus),
    frame: {
      id: row.can_id,
      dlc: row.dlc,
      data: [...row.data],
      ext: false,
      rtr: false
    },
    decoded: {
      name: row.can_name,
      signals: safeJson(row.decoded)
    }
  };
}

function rowToInjection(row: InjectionRow): InjectedFrame {
  return { row_id: row.id, ts_real: row.ts_real, ts_us: row.ts_us != null ? row.ts_us.toString() : "", seq: row.seq != null ? row.seq : 0, bus: normalizeBus(row.bus), can_id: row.can_id, dlc: row.dlc, data: [...row.data], status: row.status };
}

function safeJson(input: string): Record<string, unknown> {
  try {
    const parsed = JSON.parse(input) as Record<string, unknown>;
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}
