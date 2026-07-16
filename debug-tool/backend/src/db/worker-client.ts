import { Worker } from "worker_threads";
import * as path from "path";
import type { CanFrame, CanStats } from "../types/can";
import type { FrameQuery, DebugStore, StoredCanFrame } from "./queries";

export class WorkerClient implements DebugStore {
  private worker: Worker;
  private messageIdCounter = 1;
  private pendingRequests = new Map<number, { resolve: (val: any) => void; reject: (err: any) => void }>();

  constructor(private readonly dbPath: string, private readonly maxFrames?: number) {
    // Determine the path to worker.ts/worker.js depending on whether we are in dev (ts-node) or prod (dist/js)
    const workerFile = __filename.endsWith(".ts")
      ? path.join(__dirname, "worker.ts")
      : path.join(__dirname, "worker.js");
      
    // In development, inherit the tsx loader used by the parent process.
    const execArgv = __filename.endsWith(".ts") ? process.execArgv : [];

    this.worker = new Worker(workerFile, { execArgv });

    this.worker.on("message", (msg: any) => {
      if (msg.id && this.pendingRequests.has(msg.id)) {
        const { resolve, reject } = this.pendingRequests.get(msg.id)!;
        this.pendingRequests.delete(msg.id);
        if (msg.success) {
          resolve(msg.result);
        } else {
          reject(new Error(msg.error));
        }
      }
    });

    this.worker.on("error", (err) => {
      console.error("[WorkerClient] Worker error:", err);
    });

    this.worker.on("exit", (code) => {
      if (code !== 0) console.error(`[WorkerClient] Worker stopped with exit code ${code}`);
    });
  }

  private request<T>(method: string, args: any[] = []): Promise<T> {
    return new Promise((resolve, reject) => {
      const id = this.messageIdCounter++;
      this.pendingRequests.set(id, { resolve, reject });
      this.worker.postMessage({ id, method, args });
    });
  }

  private notify(method: string, args: any[] = []): void {
    this.worker.postMessage({ method, args });
  }

  insertFrames(batch: Array<{ frame: CanFrame; source: string }>): void {
    this.notify("insertFrames", [batch]);
  }

  setStats(stats: CanStats): void {
    this.notify("setStats", [stats]);
  }

  async getStats(): Promise<CanStats> {
    return this.request<CanStats>("getStats");
  }

  async queryFrames(query: any): Promise<StoredCanFrame[]> {
    return this.request<StoredCanFrame[]>("queryFrames", [query]);
  }

  async latestById(): Promise<Record<string, StoredCanFrame>> {
    return this.request<Record<string, StoredCanFrame>>("latestById");
  }

  async init(): Promise<void> {
    return this.request<void>("init", [this.dbPath, this.maxFrames]);
  }
  
  async clearFrames(): Promise<void> {
    return this.request<void>("clearFrames");
  }
  
  async getStatsUpdatedAt(): Promise<number | null> { return this.request("getStatsUpdatedAt"); }
  async insertInjection(input: any): Promise<any> { return this.request("insertInjection", [input]); }
  async updateInjectionByCorrelation(id: string, status: string): Promise<any> { return this.request("updateInjectionByCorrelation", [id, status]); }
  async updateLatestInjectionStatus(status: string): Promise<any> { return this.request("updateLatestInjectionStatus", [status]); }
  async listInjections(limit?: number): Promise<any[]> { return this.request("listInjections", [limit]); }
  async listRecordings(): Promise<any[]> { return this.request("listRecordings"); }
  async getRecording(id: number): Promise<any | null> { return this.request("getRecording", [id]); }
  async startRecording(label?: string): Promise<any> { return this.request("startRecording", [label]); }
  async stopRecording(id: number): Promise<any | null> { return this.request("stopRecording", [id]); }
  async deleteRecording(id: number): Promise<boolean> { return this.request("deleteRecording", [id]); }
  async recordingFramesById(id: number, limit?: number): Promise<any[] | null> { return this.request("recordingFramesById", [id, limit]); }
  
  async *recentFramesIterator(limit = 10000): AsyncIterableIterator<StoredCanFrame> {
    const frames = await this.queryFrames({ limit });
    for (const frame of frames) yield frame;
  }

  async *recordingFramesIterator(id: number): AsyncIterableIterator<StoredCanFrame> {
    const frames = await this.recordingFramesById(id);
    if (!frames) return;
    for (const frame of frames) yield frame;
  }

  async counts(): Promise<any> { return this.request("counts"); }
  async runMaintenance(): Promise<void> { return this.request("runMaintenance"); }

  async close(): Promise<void> {
    return new Promise((resolve) => {
      const id = this.messageIdCounter++;
      this.pendingRequests.set(id, { resolve, reject: resolve });
      this.worker.postMessage({ id, type: "close" });
    });
  }
}
