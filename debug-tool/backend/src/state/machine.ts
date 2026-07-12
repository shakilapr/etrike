export type ExecutionMode = "offline" | "monitor" | "simulation" | "replay";
export type PhysicalArmState = "disarmed" | "arming" | "armed";

export interface OperationalState {
  mode: ExecutionMode;
  arm: PhysicalArmState;
  profile?: string; // named bench/model configuration
  revision: bigint;
}

export interface TransitionCallbacks {
  onDisarm: () => Promise<void> | void;
  onModeSwitch: (mode: ExecutionMode, profile?: string) => Promise<void>;
}

export class OperationalStateMachine {
  private _state: OperationalState = {
    mode: "offline",
    arm: "disarmed",
    revision: 0n,
  };
  
  private lock: Promise<void> = Promise.resolve();

  constructor(private callbacks: TransitionCallbacks) {}

  get state(): OperationalState {
    return { ...this._state };
  }

  /**
   * Run a task strictly serialized.
   */
  private async serialize<T>(task: () => Promise<T>): Promise<T> {
    const prevLock = this.lock;
    let release!: () => void;
    this.lock = new Promise((resolve) => { release = resolve; });
    try {
      await prevLock;
      return await task();
    } finally {
      release();
    }
  }

  async transitionMode(mode: ExecutionMode, profile?: string): Promise<OperationalState> {
    return this.serialize(async () => {
      // 1. Immediately disarm on mode change
      if (this._state.arm !== "disarmed") {
        await this.callbacks.onDisarm();
        this._state.arm = "disarmed";
      }

      try {
        await this.callbacks.onModeSwitch(mode, profile);
        this._state.mode = mode;
        this._state.profile = profile;
        this._state.revision += 1n;
      } catch (err) {
        // Fallback to offline on partial failure
        this._state.mode = "offline";
        this._state.profile = undefined;
        this._state.revision += 1n;
        throw err;
      }

      return this.state;
    });
  }

  async arm(): Promise<OperationalState> {
    return this.serialize(async () => {
      if (this._state.mode === "offline") {
        throw new Error("Cannot arm in offline mode");
      }
      if (this._state.arm === "disarmed") {
        this._state.arm = "arming";
        this._state.revision += 1n;
        // The hardware would confirm armed here, but for now we'll simulate immediate armed
        // if this was an async physical request. Let's just set it to armed for now.
        this._state.arm = "armed";
        this._state.revision += 1n;
      }
      return this.state;
    });
  }

  async disarm(): Promise<OperationalState> {
    return this.serialize(async () => {
      if (this._state.arm !== "disarmed") {
        await this.callbacks.onDisarm();
        this._state.arm = "disarmed";
        this._state.revision += 1n;
      }
      return this.state;
    });
  }
}
