import Database from "better-sqlite3";
import { mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { SQLITE_SCHEMA } from "./schema";
import type { Bus, CanFrame, CanStats } from "../types/can";
import { defaultStats, normalizeBus, normalizeStats } from "../types/can";

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

  constructor(dbPath: string, private readonly maxFrames = 50000) {
    const filename = dbPath === ":memory:" ? dbPath : resolve(dbPath);
    if (filename !== ":memory:") mkdirSync(dirname(filename), { recursive: true });
    this.db = new Database(filename);
    this.db.pragma("journal_mode = WAL");
    this.db.exec(SQLITE_SCHEMA);
  }

  insertFrame(frame: CanFrame): StoredCanFrame {
    const tsReal = Date.now() / 1000;
    const tsDevice = Math.round(frame.ts);
    const result = this.db
      .prepare(
        `INSERT INTO can_frames (ts_real, ts_device, bus, can_id, can_name, dlc, data, decoded)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?)`
      )
      .run(tsReal, tsDevice, frame.bus, frame.id, frame.name, frame.dlc, Buffer.from(frame.data.slice(0, frame.dlc)), JSON.stringify(frame.decoded));

    const rowId = Number(result.lastInsertRowid);
    this.attachToActiveRecordings(rowId);
    this.pruneFrames();
    return { ...frame, row_id: rowId, ts_real: tsReal, ts_device: tsDevice };
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
    this.db
      .prepare("INSERT INTO runtime_state (key, value) VALUES ('stats', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
      .run(JSON.stringify(normalized));
  }

  getStats(): CanStats {
    const row = this.db.prepare("SELECT value FROM runtime_state WHERE key = 'stats'").get() as { value: string } | undefined;
    if (!row) return defaultStats();
    try {
      return normalizeStats(JSON.parse(row.value) as CanStats);
    } catch {
      return defaultStats();
    }
  }

  insertInjection(input: Omit<InjectedFrame, "row_id" | "ts_real" | "status"> & { status?: string }): InjectedFrame {
    const tsReal = Date.now() / 1000;
    const status = input.status ?? "queued";
    const result = this.db
      .prepare("INSERT INTO injected_frames (ts_real, bus, can_id, dlc, data, status) VALUES (?, ?, ?, ?, ?, ?)")
      .run(tsReal, input.bus, input.can_id, input.dlc, Buffer.from(input.data), status);
    return { row_id: Number(result.lastInsertRowid), ts_real: tsReal, bus: input.bus, can_id: input.can_id, dlc: input.dlc, data: input.data, status };
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
    return { id: Number(result.lastInsertRowid), label: cleanLabel, started_at: startedAt, stopped_at: null, frame_count: 0 };
  }

  stopRecording(id: number): Recording | null {
    this.db.prepare("UPDATE recordings SET stopped_at = COALESCE(stopped_at, ?) WHERE id = ?").run(Date.now() / 1000, id);
    return (this.db.prepare("SELECT * FROM recordings WHERE id = ?").get(id) as Recording | undefined) ?? null;
  }

  deleteRecording(id: number): boolean {
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
    this.db.close();
  }

  private attachToActiveRecordings(frameId: number): void {
    const active = this.db.prepare("SELECT id FROM recordings WHERE stopped_at IS NULL").all() as Array<{ id: number }>;
    const insert = this.db.prepare("INSERT OR IGNORE INTO recording_frames (recording_id, frame_id) VALUES (?, ?)");
    const update = this.db.prepare("UPDATE recordings SET frame_count = frame_count + 1 WHERE id = ?");
    this.db.transaction(() => {
      for (const recording of active) {
        insert.run(recording.id, frameId);
        update.run(recording.id);
      }
    })();
  }

  private pruneFrames(): void {
    const count = (this.db.prepare("SELECT COUNT(*) AS n FROM can_frames").get() as { n: number }).n;
    if (count <= this.maxFrames) return;
    const ids = this.db.prepare("SELECT id FROM can_frames ORDER BY id ASC LIMIT ?").all(count - this.maxFrames) as Array<{ id: number }>;
    const deleteRecordingFrame = this.db.prepare("DELETE FROM recording_frames WHERE frame_id = ?");
    const deleteFrame = this.db.prepare("DELETE FROM can_frames WHERE id = ?");
    this.db.transaction(() => {
      for (const row of ids) {
        deleteRecordingFrame.run(row.id);
        deleteFrame.run(row.id);
      }
    })();
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
