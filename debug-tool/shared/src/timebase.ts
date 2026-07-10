export class SessionTimebase {
  private _seq = 0;
  private _lastUs = 0n;

  constructor(initialUs?: bigint) {
    if (initialUs !== undefined) {
      this._lastUs = initialUs - 1n; // So the first call to next(initialUs) doesn't advance it unnecessarily
    } else {
      this._lastUs = BigInt(Date.now()) * 1000n;
    }
  }

  /**
   * Generates the next monotonic canonical timestamp and sequence number.
   * @param requestedUs Optional requested time in microseconds. If omitted, uses current Date.now().
   */
  next(requestedUs?: bigint): { ts_us: string; seq: number } {
    let nextUs = requestedUs;
    if (nextUs === undefined) {
      nextUs = BigInt(Date.now()) * 1000n;
    }
    
    // Enforce strict monotonicity
    if (nextUs <= this._lastUs) {
      nextUs = this._lastUs + 1n;
    }
    
    this._lastUs = nextUs;
    return {
      ts_us: nextUs.toString(),
      seq: ++this._seq,
    };
  }

  now(): { ts_us: string; seq: number } {
    return this.next();
  }

  get lastUs(): bigint {
    return this._lastUs;
  }
}

export class TimebaseMapper {
  private timebase: SessionTimebase;
  private lastExternalTs: number | null = null;
  private lastMappedUs: bigint = 0n;

  constructor(timebase: SessionTimebase) {
    this.timebase = timebase;
  }

  /**
   * Maps an external hardware/device timestamp (in seconds) to the session timebase.
   * Handles relative deltas to prevent moving canonical time backward.
   * @param externalTsSeconds External timestamp in seconds
   */
  map(externalTsSeconds: number): { ts_us: string; seq: number } {
    if (this.lastExternalTs === null) {
      // First frame from this device. Sync to the current canonical time.
      this.lastExternalTs = externalTsSeconds;
      const res = this.timebase.now();
      this.lastMappedUs = BigInt(res.ts_us);
      return res;
    }

    const deltaSec = externalTsSeconds - this.lastExternalTs;
    this.lastExternalTs = externalTsSeconds;

    // If external time went backward (e.g., wrap-around or clock jump),
    // we just use a small strictly positive delta (1us) rather than moving canonical time back.
    let deltaUs = 0n;
    if (deltaSec > 0) {
      deltaUs = BigInt(Math.round(deltaSec * 1_000_000));
    }

    let requestedUs = this.lastMappedUs + deltaUs;
    
    // Ensure we don't fall behind the global session timebase either, 
    // but typically we just ask the timebase to advance from our mapped time.
    // However, if we just use `requestedUs`, we might lag behind global time if we don't sync.
    // For now, mapping directly guarantees monotonic relative spacing for this adapter.
    
    const res = this.timebase.next(requestedUs);
    this.lastMappedUs = BigInt(res.ts_us);
    return res;
  }
}

export const defaultTimebase = new SessionTimebase();
