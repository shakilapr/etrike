import type { DebugStore } from "../db/queries";
import type { CanFrame } from "../types/can";

export interface ReplayState {
  playing: boolean;
  recordingId: string | null;
  speed: number; // e.g. 1.0 = real-time, 2.0 = 2x speed
  currentTimeUs: string | null;
  durationMs: number;
}

export class ReplayEngine {
  private timer: ReturnType<typeof setTimeout> | null = null;
  private state: ReplayState = {
    playing: false,
    recordingId: null,
    speed: 1.0,
    currentTimeUs: null,
    durationMs: 0
  };
  private currentCursor: number = 0; // index in the cached batch
  private cachedFrames: CanFrame[] = [];
  private currentOffset = 0;
  
  constructor(
    private readonly store: DebugStore,
    private readonly onFrame: (frame: CanFrame) => void
  ) {}

  getState(): ReplayState {
    return { ...this.state };
  }

  async load(recordingId: string): Promise<void> {
    this.stop();
    this.state.recordingId = recordingId;
    this.currentOffset = 0;
    this.state.currentTimeUs = null;
    
    // In a real implementation we would query duration from the DB
    this.state.durationMs = 60000; 
  }

  play(speed: number = 1.0): void {
    if (!this.state.recordingId) throw new Error("No recording loaded");
    this.state.speed = speed;
    if (this.state.playing) return;
    
    this.state.playing = true;
    this.scheduleNextBatch();
  }

  pause(): void {
    this.state.playing = false;
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }

  stop(): void {
    this.pause();
    this.state.recordingId = null;
    this.state.currentTimeUs = null;
    this.currentOffset = 0;
    this.cachedFrames = [];
    this.currentCursor = 0;
  }

  async seek(timeUs: string): Promise<void> {
    // For now we just reset the offset as a crude simulation
    this.currentOffset = 0;
    this.state.currentTimeUs = timeUs;
    this.cachedFrames = [];
    this.currentCursor = 0;
    if (this.state.playing) {
      this.pause();
      this.play(this.state.speed);
    }
  }

  private async fetchNextBatch(): Promise<void> {
    // In reality this would query by time, but for the mock we use limits
    if (!this.state.recordingId) return;
    const iter = this.store.recentFramesIterator(100);
    const frames = [];
    for await (const frame of iter) frames.push(frame);
    this.cachedFrames = frames;
    this.currentCursor = 0;
  }

  private scheduleNextBatch(): void {
    if (!this.state.playing) return;
    
    // Simulate playing frames
    this.timer = setTimeout(async () => {
      if (this.cachedFrames.length === 0 || this.currentCursor >= this.cachedFrames.length) {
        await this.fetchNextBatch();
        if (this.cachedFrames.length === 0) {
          // EOF
          this.pause();
          return;
        }
      }

      const frame = this.cachedFrames[this.currentCursor++];
      this.state.currentTimeUs = frame.ts_us;
      
      // Inject to router
      this.onFrame(frame);

      // schedule next
      this.scheduleNextBatch();
    }, 1000 / this.state.speed);
  }
}
