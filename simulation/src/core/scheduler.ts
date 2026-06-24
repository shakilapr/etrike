/**
 * TimeSliceScheduler — fires periodic callbacks at their correct intervals.
 *
 * Each scheduled task has a period (ms) and a callback. The scheduler
 * tracks `nextFireMs` for each task and invokes the callback whenever
 * `nowMs >= nextFireMs`, then advances `nextFireMs += period`.
 */

export interface ScheduledTask {
  /** Unique name for debugging. */
  name: string;
  /** Period in milliseconds. */
  periodMs: number;
  /** The callback to fire. Receives the current nowMs. */
  callback: (nowMs: number) => void;
  /** Internal: next scheduled fire time (ms). */
  nextFireMs: number;
}

export class TimeSliceScheduler {
  private tasks: ScheduledTask[] = [];

  /** Register a periodic task. Fires at `nowMs` immediately, then every `periodMs`. */
  register(name: string, periodMs: number, callback: (nowMs: number) => void, startMs = 0): ScheduledTask {
    const task: ScheduledTask = {
      name,
      periodMs,
      callback,
      nextFireMs: startMs,
    };
    this.tasks.push(task);
    return task;
  }

  /** Remove a previously registered task. */
  unregister(task: ScheduledTask): void {
    this.tasks = this.tasks.filter((t) => t !== task);
  }

  /**
   * Run all tasks whose `nextFireMs <= nowMs`.
   * Each task that fires advances its `nextFireMs` by its period.
   * Returns the number of tasks that fired.
   */
  run(nowMs: number): number {
    let fired = 0;
    for (const task of this.tasks) {
      while (nowMs >= task.nextFireMs) {
        task.callback(nowMs);
        task.nextFireMs += task.periodMs;
        fired++;
      }
    }
    return fired;
  }

  /** Reset all tasks to fire at their next scheduled time from now. */
  reset(nowMs: number): void {
    for (const task of this.tasks) {
      task.nextFireMs = nowMs + task.periodMs;
    }
  }

  /** Number of registered tasks. */
  get count(): number {
    return this.tasks.length;
  }
}
