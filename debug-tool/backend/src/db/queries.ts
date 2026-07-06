import Database from "better-sqlite3";
import { mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { SQLITE_SCHEMA } from "./schema";
import type { Bus, CanFrame, CanStats } from "../types/can";
import { defaultStats, normalizeBus, normalizeStats } from "../types/can";
import type { FrameRouter, FrameSource } from "../sim/router";

export interface StoredCanFrame extends CanFrame {
  row_id: number;
  ts_real: number;
  ts_device: number;
}

export interface InjectedFrame {
  row_id: number;
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
  ts_real: number;
  bus: Bus;
  can_id: string;
  dlc: number;
  data: Buffer;
  status: string | null;
}

export class DebugStore {
  private readonly db: Database.Database;
  private walTimer: ReturnType<typeof setInterval> | null = null;
  private maintenanceTimer: ReturnType<typeof setInterval> | null = null;
  private maintenanceRunning = false;
  private activeRecordingIds = new Set<number>();
  private activeRecordingsLoaded = false;
  /** Optional FrameRouter — when set, all insertFrame calls route through it. */
  router: { resolve(frame: CanFrame, source: FrameSource): CanFrame | null } | null = null;

  private static readonly MAINTENANCE_INTERVAL_MS = 5000;
  private static readonly STOPPED_RECORDING_RETENTION = 10;

  constructor(dbPath: string, private readonly maxFrames = 50000) {
    const filename = dbPath === ":memory:" ? dbPath : resolve(dbPath);
    if (filename !== ":memory:") mkdirSync(dirname(filename), { recursive: true });
    this.db = new Database(filename);
    this.db.pragma("journal_mode = WAL");
    this.db.pragma("foreign_keys = ON");
    this.db.exec(SQLITE_SCHEMA);
    // Periodic WAL checkpoint to prevent unbounded WAL growth
    this.walTimer = setInterval(() => {
      this.db.pragma("wal_checkpoint(TRUNCATE)");
    }, 30000).unref();
    this.maintenanceTimer = setInterval(() => {
      this.runMaintenance();
    }, DebugStore.MAINTENANCE_INTERVAL_MS).unref();
  }

  /**
   * Insert a CAN frame. If a FrameRouter is set, the frame is routed
   * through it — if the router rejects (collision), the frame is dropped.
   * Physical frames use source="physical", emulated use "emulated".
   */
  insertFrame(frame: CanFrame, source: FrameSource = "physical"): StoredCanFrame {
    // Route through FrameRouter if available
    if (this.router) {
      const accepted = this.router.resolve(frame, source);
      if (!accepted) return { ...frame, row_id: -1, ts_real: Date.now() / 1000, ts_device: Math.round(frame.ts) };
      frame = accepted;
    }
    const tsReal = Date.now() / 1000;
    const tsDevice = Math.round(frame.ts);
    try {
      const result = this.db
        .prepare(
          `INSERT INTO can_frames (ts_real, ts_device, bus, can_id, can_name, dlc, data, decoded)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?)`
        )
        .run(tsReal, tsDevice, frame.bus, frame.id, frame.name, frame.dlc, Buffer.from(frame.data.slice(0, frame.dlc)), JSON.stringify(frame.decoded));

      const rowId = Number(result.lastInsertRowid);
      this.attachToActiveRecordings(rowId);
      return { ...frame, row_id: rowId, ts_real: tsReal, ts_device: tsDevice };
    } catch (err) {
      console.error("insertFrame failed:", String(err));
      return { ...frame, row_id: -1, ts_real: tsReal, ts_device: tsDevice };
    }
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
    const rows = this.db.prepare("SELECT * FROM can_frames ORDER BY ts_real DESC, id DESC LIMIT 5000").all() as FrameRow[];
    const latest: Record<string, StoredCanFrame> = {};
    for (const row of rows) {
      const key = `${row.bus}:${row.can_id}`;
      if (!latest[key]) latest[key] = rowToFrame(row);
    }
    return latest;
  }

  setStats(stats: CanStats): void {
    const normalized = normalizeStats(stats);
    const now = Date.now() / 1000;
    this.db
      .prepare("INSERT INTO runtime_state (key, value) VALUES ('stats', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
      .run(JSON.stringify(normalized));
    this.db
      .prepare("INSERT INTO runtime_state (key, value) VALUES ('stats_updated_at', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
      .run(String(now));
  }

  getStatsUpdatedAt(): number | null {
    const row = this.db.prepare("SELECT value FROM runtime_state WHERE key = 'stats_updated_at'").get() as { value: string } | undefined;
    if (!row) return null;
    const n = Number(row.value);
    return Number.isFinite(n) ? n : null;
  }

  getStats(): CanStats {
    const statsRow = this.db.prepare("SELECT value FROM runtime_state WHERE key = 'stats'").get() as { value: string } | undefined;
    const tsRow = this.db.prepare("SELECT value FROM runtime_state WHERE key = 'stats_updated_at'").get() as { value: string } | undefined;
    if (!statsRow) return defaultStats();
    // Return zeroed stats if older than 5 seconds
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

  insertInjection(input: Omit<InjectedFrame, "row_id" | "ts_real" | "status"> & { status?: string; correlation_id?: string }): InjectedFrame {
    const tsReal = Date.now() / 1000;
    const status = input.status ?? "queued";
    const correlationId = input.correlation_id ?? null;
    const result = this.db
      .prepare("INSERT INTO injected_frames (ts_real, bus, can_id, dlc, data, status, correlation_id) VALUES (?, ?, ?, ?, ?, ?, ?)")
      .run(tsReal, input.bus, input.can_id, input.dlc, Buffer.from(input.data), status, correlationId);
    return { row_id: Number(result.lastInsertRowid), ts_real: tsReal, bus: input.bus, can_id: input.can_id, dlc: input.dlc, data: input.data, status };
  }

  updateInjectionByCorrelation(correlationId: string, status: string): InjectedFrame | null {
    const row = this.db.prepare("SELECT id FROM injected_frames WHERE correlation_id = ?").get(correlationId) as { id: number } | undefined;
    if (!row) {
      // Fallback to the old behavior: update the most recent injection
      return this.updateLatestInjectionStatus(status);
    }
    this.db.prepare("UPDATE injected_frames SET status = ? WHERE id = ?").run(status, row.id);
    const updated = this.db.prepare("SELECT * FROM injected_frames WHERE id = ?").get(row.id) as InjectionRow;
    return rowToInjection(updated);
  }

  updateLatestInjectionStatus(status: string): InjectedFrame | null {
    const row = this.db.prepare("SELECT id FROM injected_frames ORDER BY id DESC LIMIT 1").get() as { id: number } | undefined;
    if (!row) return null;
    this.db.prepare("UPDATE injected_frames SET status = ? WHERE id = ?").run(status, row.id);
    const updated = this.db.prepare("SELECT * FROM injected_frames WHERE id = ?").get(row.id) as InjectionRow;
    return rowToInjection(updated);
  }

  listInjections(limit = 50): InjectedFrame[] {
    return (this.db.prepare("SELECT * FROM injected_frames ORDER BY ts_real DESC, id DESC LIMIT ?").all(limit) as InjectionRow[]).map(rowToInjection);
  }

  listRecordings(): Recording[] {
    return this.db.prepare("SELECT * FROM recordings ORDER BY started_at DESC").all() as Recording[];
  }

  startRecording(label?: string): Recording {
    const startedAt = Date.now() / 1000;
    const cleanLabel = label?.trim() || null;
    const result = this.db.prepare("INSERT INTO recordings (label, started_at, stopped_at, frame_count) VALUES (?, ?, NULL, 0)").run(cleanLabel, startedAt);
    const id = Number(result.lastInsertRowid);
    this.activeRecordingIds.add(id);
    return { id, label: cleanLabel, started_at: startedAt, stopped_at: null, frame_count: 0 };
  }

  stopRecording(id: number): Recording | null {
    this.activeRecordingIds.delete(id);
    this.db.prepare("UPDATE recordings SET stopped_at = COALESCE(stopped_at, ?) WHERE id = ?").run(Date.now() / 1000, id);
    this.db.prepare("UPDATE recordings SET frame_count = (SELECT COUNT(*) FROM recording_frames WHERE recording_id = ?) WHERE id = ?").run(id, id);
    return (this.db.prepare("SELECT * FROM recordings WHERE id = ?").get(id) as Recording | undefined) ?? null;
  }

  deleteRecording(id: number): boolean {
    this.activeRecordingIds.delete(id);
    this.db.prepare("DELETE FROM recording_frames WHERE recording_id = ?").run(id);
    return this.db.prepare("DELETE FROM recordings WHERE id = ?").run(id).changes > 0;
  }

  recordingFramesById(id: number, limit = 1000): StoredCanFrame[] | null {
    if (!this.db.prepare("SELECT id FROM recordings WHERE id = ?").get(id)) return null;
    const rows = this.db
      .prepare(
        `SELECT f.* FROM recording_frames rf
         JOIN can_frames f ON f.id = rf.frame_id
         WHERE rf.recording_id = ?
         ORDER BY f.ts_real DESC, f.id DESC
         LIMIT ?`
      )
      .all(id, limit) as FrameRow[];
    return rows.map(rowToFrame);
  }

  counts(): { frames: number; injected: number; recordings: number } {
    return {
      frames: (this.db.prepare("SELECT COUNT(*) AS n FROM can_frames").get() as { n: number }).n,
      injected: (this.db.prepare("SELECT COUNT(*) AS n FROM injected_frames").get() as { n: number }).n,
      recordings: (this.db.prepare("SELECT COUNT(*) AS n FROM recordings").get() as { n: number }).n
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

  private attachToActiveRecordings(frameId: number): void {
    if (!this.activeRecordingsLoaded) {
      const active = this.db.prepare("SELECT id FROM recordings WHERE stopped_at IS NULL").all() as Array<{ id: number }>;
      for (const row of active) this.activeRecordingIds.add(row.id);
      this.activeRecordingsLoaded = true;
    }
    if (this.activeRecordingIds.size === 0) return;

    const insert = this.db.prepare("INSERT OR IGNORE INTO recording_frames (recording_id, frame_id) VALUES (?, ?)");
    const update = this.db.prepare("UPDATE recordings SET frame_count = frame_count + 1 WHERE id = ?");
    this.db.transaction(() => {
      for (const id of this.activeRecordingIds) {
        insert.run(id, frameId);
        update.run(id);
      }
    })();
  }

  private pruneFrames(): void {
    const count = (this.db.prepare("SELECT COUNT(*) AS n FROM can_frames").get() as { n: number }).n;
    if (count <= this.maxFrames) return;
    const ids = this.db
      .prepare(
        `SELECT f.id FROM can_frames f
         WHERE NOT EXISTS (
           SELECT 1 FROM recording_frames rf WHERE rf.frame_id = f.id
         )
         ORDER BY f.id ASC
         LIMIT ?`
      )
      .all(count - this.maxFrames) as Array<{ id: number }>;
    const deleteFrame = this.db.prepare("DELETE FROM can_frames WHERE id = ?");
    this.db.transaction(() => {
      for (const row of ids) {
        deleteFrame.run(row.id);
      }
    })();
  }

  private pruneStoppedRecordings(): void {
    const oldStopped = this.db
      .prepare(
        `SELECT id FROM recordings
         WHERE stopped_at IS NOT NULL
         ORDER BY stopped_at DESC, id DESC
         LIMIT -1 OFFSET ?`
      )
      .all(DebugStore.STOPPED_RECORDING_RETENTION) as Array<{ id: number }>;
    if (oldStopped.length === 0) return;

    const deleteFrames = this.db.prepare("DELETE FROM recording_frames WHERE recording_id = ?");
    const deleteRecording = this.db.prepare("DELETE FROM recordings WHERE id = ?");
    this.db.transaction(() => {
      for (const row of oldStopped) {
        this.activeRecordingIds.delete(row.id);
        deleteFrames.run(row.id);
        deleteRecording.run(row.id);
      }
    })();
  }

  clearFrames(): void {
    this.db.exec("DELETE FROM can_frames; DELETE FROM recording_frames; UPDATE recordings SET frame_count = 0;");
  }
}

function rowToFrame(row: FrameRow): StoredCanFrame {
  return {
    row_id: row.id,
    ts_real: row.ts_real,
    ts_device: row.ts_device,
    ts: row.ts_device,
    bus: normalizeBus(row.bus),
    id: row.can_id,
    name: row.can_name,
    dlc: row.dlc,
    data: [...row.data],
    decoded: safeJson(row.decoded)
  };
}

function rowToInjection(row: InjectionRow): InjectedFrame {
  return { row_id: row.id, ts_real: row.ts_real, bus: normalizeBus(row.bus), can_id: row.can_id, dlc: row.dlc, data: [...row.data], status: row.status };
}

function safeJson(input: string): Record<string, unknown> {
  try {
    const parsed = JSON.parse(input) as Record<string, unknown>;
    return parsed && typeof parsed === "object" ? parsed : {};
  } catch {
    return {};
  }
}
