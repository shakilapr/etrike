import { CanFrame } from "../types/can";
import { FrameSource } from "../sim/router";
import { DebugStore } from "./queries";

export class WriteQueue {
  private queue: Array<{ frame: CanFrame; source: FrameSource }> = [];
  private flushTimer: ReturnType<typeof setInterval> | null = null;
  private isDraining = false;
  public router: { resolve(frame: CanFrame, source: FrameSource): CanFrame | null } | null = null;

  constructor(
    private readonly store: DebugStore,
    private readonly maxBatchSize = 500,
    private readonly flushIntervalMs = 100
  ) {
    this.flushTimer = setInterval(() => {
      this.flush();
    }, this.flushIntervalMs).unref();
  }

  enqueue(frame: CanFrame, source: FrameSource = "physical"): void {
    if (this.isDraining) return;
    
    let finalFrame = frame;
    if (this.router) {
      const accepted = this.router.resolve(frame, source);
      if (!accepted) return;
      finalFrame = accepted;
    }
    
    this.queue.push({ frame: finalFrame, source });
    if (this.queue.length >= this.maxBatchSize) {
      this.flush();
    }
  }

  flush(): void {
    if (this.queue.length === 0) return;
    const batch = this.queue;
    this.queue = [];
    this.store.insertFrames(batch);
  }

  async drain(): Promise<void> {
    this.isDraining = true;
    if (this.flushTimer) {
      clearInterval(this.flushTimer);
      this.flushTimer = null;
    }
    this.flush();
  }
}
