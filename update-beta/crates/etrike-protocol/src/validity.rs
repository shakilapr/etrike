//! Stream supervision: rolling-counter validity and freshness.
//!
//! Faithful port of `shared/stream_validity.h` + `protocol/core/supervision.hpp`.
//! Each supervised stream owns its own `StreamValidity`; unlike the C++ map-based
//! tracker, no shared table or allocation is required.
//!
//! Counter rules:
//! - delta == 1 -> accept, refresh freshness
//! - delta == 2 -> accept with one missed frame, refresh freshness
//! - delta == 0 -> reject duplicate/frozen, do NOT refresh freshness
//! - delta  > 2 -> sequence fault, authority invalid
//!
//! Reacquisition after a fault: the first frame establishes a baseline (still
//! invalid); the next correctly-advancing frame restores authority.

/// Monotonic time in milliseconds since boot.
pub type Instant = u64;

/// A value with the time it was observed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Timestamped<T> {
    pub value: T,
    pub at: Instant,
}

impl<T> Timestamped<T> {
    pub const fn new(value: T, at: Instant) -> Self {
        Self { value, at }
    }
}

/// True when `ts` is within `window_ms` of `now`.
pub fn fresh<T>(ts: &Timestamped<T>, now: Instant, window_ms: u64) -> bool {
    now.wrapping_sub(ts.at) <= window_ms
}

/// A message that has passed frame verification and stream validity.
///
/// This type cannot be produced by decoding a raw frame directly; only the
/// validation pipeline constructs it. The core accepts only `Valid<T>`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Valid<T> {
    pub value: T,
    pub at: Instant,
    pub counter: u8,
}

impl<T> Valid<T> {
    pub const fn new(value: T, at: Instant, counter: u8) -> Self {
        Self { value, at, counter }
    }

    pub fn into_inner(self) -> T {
        self.value
    }
}

/// Classification of a counter observation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CounterEvent {
    First,
    Increment,
    Wrap,
    Duplicate,
    Frozen,
    Gap,
    Reorder,
    Reset,
    Recovery,
    InvalidValue,
}

/// Classification of a freshness observation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FreshnessEvent {
    First,
    Refresh,
    Fresh,
    Expired,
    Stale,
    TimeReorder,
    Recovery,
    NeverSeen,
}

/// Result of observing a frame on a supervised stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StreamStatus {
    /// Baseline established but not yet authoritative.
    Init,
    /// Authoritative, contiguous counter (delta 1).
    Valid,
    /// Authoritative, one frame missed (delta 2).
    ValidGap,
    /// Duplicate/frozen frame with expired freshness, or expired freshness.
    Stale,
    /// Sequence fault or invalid counter.
    Fault,
}

impl StreamStatus {
    pub const fn is_valid(self) -> bool {
        matches!(self, StreamStatus::Valid | StreamStatus::ValidGap)
    }
}

/// Supervises one rolling-counter stream.
#[derive(Debug, Clone)]
pub struct StreamValidity {
    modulus: u32,
    max_forward_delta: u32,
    timeout_ms: u64,
    valid: bool,
    // counter tracker
    initialized: bool,
    reset_pending: bool,
    degraded: bool,
    last: u32,
    duplicate_count: u32,
    // freshness tracker
    seen: bool,
    last_seen: Instant,
    stale: bool,
}

impl StreamValidity {
    /// `modulus` is the counter wrap modulus (256 for an 8-bit counter).
    /// `max_forward_delta` is the largest accepted forward jump (2 tolerates one
    /// missed frame). `timeout_ms` is the freshness window.
    pub const fn new(modulus: u32, max_forward_delta: u32, timeout_ms: u64) -> Self {
        Self {
            modulus,
            max_forward_delta,
            timeout_ms,
            valid: false,
            initialized: false,
            reset_pending: false,
            degraded: false,
            last: 0,
            duplicate_count: 0,
            seen: false,
            last_seen: 0,
            stale: false,
        }
    }

    /// Convenience constructor for an 8-bit counter tolerating one missed frame.
    pub const fn u8_counter(timeout_ms: u64) -> Self {
        Self::new(256, 2, timeout_ms)
    }

    pub const fn is_valid(&self) -> bool {
        self.valid
    }

    /// Force into the invalid/reacquiring state.
    pub fn invalidate(&mut self) {
        self.initialized = false;
        self.reset_pending = true;
        self.degraded = false;
        self.duplicate_count = 0;
        self.seen = false;
        self.stale = false;
        self.valid = false;
    }

    /// Observe a new frame with `counter` at time `now`; returns the status.
    pub fn observe(&mut self, counter: u8, now: Instant) -> StreamStatus {
        let ce = self.counter_observe(counter as u32);

        if !self.valid {
            // Reacquiring: a clean advancing frame restores authority; a
            // baseline (First/Reset) does not.
            match ce {
                CounterEvent::Increment | CounterEvent::Wrap | CounterEvent::Recovery => {
                    self.valid = true;
                    StreamStatus::Valid
                }
                CounterEvent::Gap => {
                    self.valid = true;
                    StreamStatus::ValidGap
                }
                _ => StreamStatus::Init,
            }
        } else {
            match ce {
                CounterEvent::Reorder | CounterEvent::InvalidValue => {
                    self.invalidate();
                    StreamStatus::Fault
                }
                CounterEvent::Duplicate | CounterEvent::Frozen => {
                    // Duplicate: reject but do NOT refresh freshness.
                    let fe = self.fresh_check(now);
                    if matches!(fe, FreshnessEvent::Expired | FreshnessEvent::Stale) {
                        self.invalidate();
                        StreamStatus::Stale
                    } else {
                        StreamStatus::Valid
                    }
                }
                _ => {
                    let fe = self.fresh_observe(now);
                    if matches!(fe, FreshnessEvent::Expired | FreshnessEvent::Stale) {
                        self.invalidate();
                        StreamStatus::Stale
                    } else if ce == CounterEvent::Gap {
                        StreamStatus::ValidGap
                    } else {
                        StreamStatus::Valid
                    }
                }
            }
        }
    }

    fn counter_observe(&mut self, value: u32) -> CounterEvent {
        if self.modulus < 2 || self.max_forward_delta == 0 || self.max_forward_delta >= self.modulus
        {
            return CounterEvent::InvalidValue;
        }
        if value >= self.modulus {
            return CounterEvent::InvalidValue;
        }
        if self.reset_pending {
            self.initialized = true;
            self.reset_pending = false;
            self.degraded = false;
            self.last = value;
            self.duplicate_count = 0;
            return CounterEvent::Reset;
        }
        if !self.initialized {
            self.initialized = true;
            self.last = value;
            self.duplicate_count = 0;
            return CounterEvent::First;
        }
        if value == self.last {
            self.degraded = true;
            self.duplicate_count += 1;
            return if self.duplicate_count == 1 {
                CounterEvent::Duplicate
            } else {
                CounterEvent::Frozen
            };
        }
        self.duplicate_count = 0;
        let delta = (value + self.modulus - self.last) % self.modulus;
        if delta > self.max_forward_delta {
            self.degraded = true;
            return CounterEvent::Reorder;
        }
        let wrapped = value < self.last;
        self.last = value;
        if delta > 1 {
            self.degraded = true;
            return CounterEvent::Gap;
        }
        if self.degraded {
            self.degraded = false;
            CounterEvent::Recovery
        } else if wrapped {
            CounterEvent::Wrap
        } else {
            CounterEvent::Increment
        }
    }

    fn fresh_observe(&mut self, now: Instant) -> FreshnessEvent {
        if self.timeout_ms == 0 {
            return FreshnessEvent::Expired;
        }
        if self.reset_pending {
            self.initialized = true;
            self.reset_pending = false;
            self.stale = false;
            self.last_seen = now;
            return FreshnessEvent::First;
        }
        if !self.seen {
            self.seen = true;
            self.last_seen = now;
            return FreshnessEvent::First;
        }
        if now < self.last_seen {
            self.degraded = true;
            return FreshnessEvent::TimeReorder;
        }
        let elapsed = now - self.last_seen;
        let recovering = self.stale || elapsed > self.timeout_ms;
        self.last_seen = now;
        self.stale = false;
        if recovering {
            FreshnessEvent::Recovery
        } else {
            FreshnessEvent::Refresh
        }
    }

    fn fresh_check(&self, now: Instant) -> FreshnessEvent {
        if !self.seen {
            return FreshnessEvent::NeverSeen;
        }
        if self.timeout_ms == 0 {
            return FreshnessEvent::Expired;
        }
        if now < self.last_seen {
            return FreshnessEvent::TimeReorder;
        }
        let elapsed = now - self.last_seen;
        if elapsed <= self.timeout_ms {
            FreshnessEvent::Fresh
        } else if self.stale {
            FreshnessEvent::Stale
        } else {
            FreshnessEvent::Expired
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn acquires_on_second_advancing_frame() {
        let mut v = StreamValidity::u8_counter(200);
        assert_eq!(v.observe(10, 0), StreamStatus::Init);
        assert!(!v.is_valid());
        assert_eq!(v.observe(11, 10), StreamStatus::Valid);
        assert!(v.is_valid());
    }

    #[test]
    fn accepts_one_missed_frame() {
        let mut v = StreamValidity::u8_counter(200);
        v.observe(0, 0);
        assert_eq!(v.observe(2, 10), StreamStatus::ValidGap);
        assert!(v.is_valid());
    }

    #[test]
    fn duplicate_is_rejected_but_stays_valid_within_timeout() {
        let mut v = StreamValidity::u8_counter(200);
        v.observe(0, 0);
        v.observe(1, 10);
        assert_eq!(v.observe(1, 20), StreamStatus::Valid);
        assert!(v.is_valid());
    }

    #[test]
    fn duplicate_after_timeout_stales() {
        let mut v = StreamValidity::u8_counter(100);
        v.observe(0, 0);
        v.observe(1, 10);
        // a normal advancing frame seeds freshness
        assert_eq!(v.observe(2, 20), StreamStatus::Valid);
        // long silence, then a duplicate arrives -> freshness expired
        assert_eq!(v.observe(2, 10_000), StreamStatus::Stale);
        assert!(!v.is_valid());
    }

    #[test]
    fn reorder_is_a_fault() {
        let mut v = StreamValidity::u8_counter(200);
        v.observe(0, 0);
        v.observe(1, 10);
        assert_eq!(v.observe(9, 20), StreamStatus::Fault);
        assert!(!v.is_valid());
    }

    #[test]
    fn reorder_requires_reacquisition() {
        let mut v = StreamValidity::u8_counter(200);
        v.observe(0, 0);
        v.observe(1, 10);
        v.observe(9, 20); // fault
        assert!(!v.is_valid());
        assert_eq!(v.observe(10, 30), StreamStatus::Init); // baseline
        assert_eq!(v.observe(11, 40), StreamStatus::Valid);
        assert!(v.is_valid());
    }

    #[test]
    fn wrap_around_is_valid() {
        let mut v = StreamValidity::u8_counter(200);
        v.observe(254, 0);
        assert_eq!(v.observe(255, 10), StreamStatus::Valid);
        assert_eq!(v.observe(0, 20), StreamStatus::Valid);
        assert!(v.is_valid());
    }

    #[test]
    fn invalidate_forces_reacquisition() {
        let mut v = StreamValidity::u8_counter(200);
        v.observe(0, 0);
        v.observe(1, 10);
        v.invalidate();
        assert!(!v.is_valid());
        assert_eq!(v.observe(5, 20), StreamStatus::Init);
        assert_eq!(v.observe(6, 30), StreamStatus::Valid);
    }

    #[test]
    fn fresh_helper() {
        let ts = Timestamped::new(42u16, 1000);
        assert!(fresh(&ts, 1100, 200));
        assert!(!fresh(&ts, 1300, 200));
    }
}
