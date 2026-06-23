import type { CanFrame, CanStats } from "../types/can";

export interface StoredCanFrame extends CanFrame {
  row_id: number;
  ts_real: number;
  ts_device: number;
}

export interface InjectedFrame {
  row_id: number;
  ts_real: number;
  can_id: string;
  dlc: number;
  data: number[];
  request_id: string;
  response: Record<string, unknown> | null;
}

export interface Recording {
  id: number;
  label: string | null;
  started_at: number;
  stopped_at: number | null;
  frame_count: number;
}

export interface FrameQuery {
  id?: string;
  since?: number;
  limit?: number;
}

export class DebugStore {
  private nextFrameId = 1;
  private nextInjectionId = 1;
  private nextRecordingId = 1;
  private frames: StoredCanFrame[] = [];
  private injected: InjectedFrame[] = [];
  private recordings: Recording[] = [];
  private recordingFrames = new Map<number, number[]>();
  private latestStats: CanStats | null = null;

  constructor(private readonly maxFrames = 50000) {}

  insertFrame(frame: CanFrame): StoredCanFrame {
    const stored: StoredCanFrame = {
      ...frame,
      row_id: this.nextFrameId++,
      ts_real: Date.now() / 1000,
      ts_device: frame.ts
    };

    this.frames.push(stored);
    this.trimFrames();

    for (const recording of this.recordings) {
      if (recording.stopped_at === null) {
        const ids = this.recordingFrames.get(recording.id) ?? [];
        ids.push(stored.row_id);
        this.recordingFrames.set(recording.id, ids);
        recording.frame_count = ids.length;
      }
    }

    return stored;
  }

  queryFrames(query: FrameQuery = {}): StoredCanFrame[] {
    const limit = Math.min(Math.max(query.limit ?? 500, 1), 5000);
    let rows = this.frames;

    if (query.id) {
      rows = rows.filter((frame) => frame.id === query.id);
    }

    if (typeof query.since === "number" && Number.isFinite(query.since)) {
      rows = rows.filter((frame) => frame.ts_real >= query.since! || frame.ts >= query.since!);
    }

    return rows.slice(-limit).reverse();
  }

  latestById(): Record<string, StoredCanFrame> {
    const latest: Record<string, StoredCanFrame> = {};
    for (const frame of this.frames) {
      latest[String(frame.id)] = frame;
    }
    return latest;
  }

  setStats(stats: CanStats): void {
    this.latestStats = stats;
  }

  getStats(): CanStats | null {
    return this.latestStats;
  }

  insertInjection(input: Omit<InjectedFrame, "row_id" | "ts_real" | "response">): InjectedFrame {
    const row: InjectedFrame = {
      ...input,
      row_id: this.nextInjectionId++,
      ts_real: Date.now() / 1000,
      response: null
    };
    this.injected.push(row);
    return row;
  }

  updateInjectionResponse(requestId: string, response: Record<string, unknown>): InjectedFrame | null {
    let row: InjectedFrame | undefined;
    for (let index = this.injected.length - 1; index >= 0; index -= 1) {
      if (this.injected[index].request_id === requestId) {
        row = this.injected[index];
        break;
      }
    }
    if (!row) return null;
    row.response = response;
    return row;
  }

  listInjections(limit = 50): InjectedFrame[] {
    return this.injected.slice(-limit).reverse();
  }

  listRecordings(): Recording[] {
    return [...this.recordings].sort((a, b) => b.started_at - a.started_at);
  }

  startRecording(label?: string): Recording {
    const recording: Recording = {
      id: this.nextRecordingId++,
      label: label?.trim() || null,
      started_at: Date.now() / 1000,
      stopped_at: null,
      frame_count: 0
    };
    this.recordings.push(recording);
    this.recordingFrames.set(recording.id, []);
    return recording;
  }

  stopRecording(id: number): Recording | null {
    const recording = this.recordings.find((item) => item.id === id);
    if (!recording) return null;
    if (recording.stopped_at === null) {
      recording.stopped_at = Date.now() / 1000;
    }
    return recording;
  }

  deleteRecording(id: number): boolean {
    const before = this.recordings.length;
    this.recordings = this.recordings.filter((item) => item.id !== id);
    this.recordingFrames.delete(id);
    return this.recordings.length !== before;
  }

  recordingFramesById(id: number, limit = 1000): StoredCanFrame[] | null {
    if (!this.recordingFrames.has(id)) return null;

    const ids = new Set(this.recordingFrames.get(id)!.slice(-limit));
    return this.frames.filter((frame) => ids.has(frame.row_id));
  }

  counts(): { frames: number; injected: number; recordings: number } {
    return {
      frames: this.frames.length,
      injected: this.injected.length,
      recordings: this.recordings.length
    };
  }

  private trimFrames(): void {
    if (this.frames.length <= this.maxFrames) return;
    const dropCount = this.frames.length - this.maxFrames;
    const dropped = new Set(this.frames.slice(0, dropCount).map((frame) => frame.row_id));
    this.frames = this.frames.slice(dropCount);

    for (const [recordingId, ids] of this.recordingFrames) {
      this.recordingFrames.set(
        recordingId,
        ids.filter((id) => !dropped.has(id))
      );
    }
  }
}
