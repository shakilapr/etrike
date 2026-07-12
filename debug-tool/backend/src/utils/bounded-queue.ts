export interface QueueMetrics {
  depth: number;
  capacity: number;
  dropped: number;
  rejected: number;
}

export type OverloadPolicy = "drop_oldest" | "drop_newest" | "reject";

export class BoundedQueue<T> {
  private buffer: T[] = [];
  private metrics: QueueMetrics;

  constructor(
    capacity: number,
    private policy: OverloadPolicy = "drop_oldest",
    private onDrop?: (items: T[]) => void
  ) {
    this.metrics = { depth: 0, capacity, dropped: 0, rejected: 0 };
  }

  enqueue(item: T): boolean {
    if (this.buffer.length >= this.metrics.capacity) {
      if (this.policy === "drop_oldest") {
        const dropped = this.buffer.shift();
        this.metrics.dropped++;
        this.metrics.depth = this.buffer.length;
        if (this.onDrop && dropped !== undefined) this.onDrop([dropped]);
      } else if (this.policy === "drop_newest") {
        this.metrics.dropped++;
        if (this.onDrop) this.onDrop([item]);
        return false;
      } else if (this.policy === "reject") {
        this.metrics.rejected++;
        return false;
      }
    }

    this.buffer.push(item);
    this.metrics.depth = this.buffer.length;
    return true;
  }

  enqueueBatch(items: T[]): boolean {
    let allAccepted = true;
    for (const item of items) {
      if (!this.enqueue(item)) {
        allAccepted = false;
      }
    }
    return allAccepted;
  }

  dequeue(): T | undefined {
    const item = this.buffer.shift();
    this.metrics.depth = this.buffer.length;
    return item;
  }

  drain(): T[] {
    const batch = this.buffer;
    this.buffer = [];
    this.metrics.depth = 0;
    return batch;
  }

  getMetrics(): QueueMetrics {
    return { ...this.metrics };
  }

  get size(): number {
    return this.buffer.length;
  }

  clear(): void {
    this.buffer = [];
    this.metrics.depth = 0;
  }
}
