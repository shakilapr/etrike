import { CanFrame } from "../types/can";
import { FrameSource } from "../sim/router";
import { DebugStore } from "./queries";
import { BoundedQueue } from "../utils/bounded-queue";

export class WriteQueue {
  private queue = new BoundedQueue<{ frame: CanFrame; source: FrameSource }>(50000, "drop_newest", (dropped) => {
    // If DB is backlogged, we log an error once per batch
    console.error(`WriteQueue full, dropped ${dropped.length} newest frames. DB recording is incomplete.`);
  });
  private flushTimer: ReturnType<typeof setInterval> | null = null;
  private isDraining = false;
  public onFlush?: () => void;

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
    
    this.queue.enqueue({ frame, source });
    if (this.queue.size >= this.maxBatchSize) {
      this.flush();
    }
  }

  flush(): void {
    if (this.queue.size === 0) return;
    const batch = this.queue.drain();
    this.store.insertFrames(batch);
    if (this.onFlush) this.onFlush();
  }

  getMetrics() {
    return this.queue.getMetrics();
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
